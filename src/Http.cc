/*
 * lftp and utils
 *
 * Copyright (c) 1999-2006 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include <config.h>
#include "trio.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include <fnmatch.h>
#include <locale.h>
#include "Http.h"
#include "ResMgr.h"
#include "log.h"
#include "url.h"
#include "HttpDir.h"
#include "misc.h"
#include "buffer_ssl.h"

#include "ascii_ctype.h"

#if !HAVE_DECL_STRPTIME
CDECL char *strptime(const char *buf, const char *format, struct tm *tm);
#endif

#define super NetAccess

#define max_buf 0x10000

#define HTTP_DEFAULT_PORT	 "80"
#define HTTP_DEFAULT_PROXY_PORT	 "3128"
#define HTTPS_DEFAULT_PORT	 "443"

/* Some status code validation macros: */
#define H_20X(x)        (((x) >= 200) && ((x) < 300))
#define H_PARTIAL(x)    ((x) == 206)
#define H_REDIRECTED(x) (((x) == 301) || ((x) == 302) || ((x) == 303) || ((x) == 307))
#define H_EMPTY(x)	(((x) == 204) || ((x) == 205))
#define H_CONTINUE(x)	((x) == 100 || (x) == 102)
#define H_REQUESTED_RANGE_NOT_SATISFIABLE(x) ((x) == 416)

#ifndef EINPROGRESS
#define EINPROGRESS -1
#endif

void Http::Init()
{
   state=DISCONNECTED;
   tunnel_state=NO_TUNNEL;
   sock=-1;
   send_buf=0;
   recv_buf=0;
   body_size=-1;
   bytes_received=0;
   status_code=0;
   status_consumed=0;
   proto_version=0x10;
   sent_eot=false;
   last_method=0;

   default_cwd="/";

   keep_alive=false;
   keep_alive_max=-1;

   array_send=0;

   chunked=false;
   chunk_size=-1;
   chunk_pos=0;
   chunked_trailer=false;

   no_ranges=false;
   seen_ranges_bytes=false;

   no_cache_this=false;
   no_cache=false;

   use_propfind_now=true;

   hftp=false;
   https=false;
   use_head=true;

   user_agent=0;
   special=HTTP_NONE;
}

Http::Http() : super()
{
   Init();
   Reconfig();
}
Http::Http(const Http *f) : super(f)
{
   Init();
   Reconfig();
}

Http::~Http()
{
   Close();
   Disconnect();
}

void Http::MoveConnectionHere(Http *o)
{
   send_buf=o->send_buf.borrow();
   recv_buf=o->recv_buf.borrow();
   sock=o->sock; o->sock=-1;
   rate_limit=o->rate_limit.borrow();
   last_method=o->last_method; o->last_method=0;
   timeout_timer.Reset(o->timeout_timer);
   state=CONNECTED;
   o->Disconnect();
   ResumeInternal();
}

void Http::Disconnect()
{
   send_buf=0;
   recv_buf=0;
   rate_limit=0;
   if(sock!=-1)
   {
      LogNote(7,_("Closing HTTP connection"));
      close(sock);
      sock=-1;
   }
   if((mode==STORE && state!=DONE && real_pos>0)
   && !Error())
   {
      if(last_method && !strcmp(last_method,"POST"))
	 SetError(FATAL,_("POST method failed"));
      else
	 SetError(STORE_FAILED,0);
   }
   last_method=0;
   ResetRequestData();
   state=DISCONNECTED;
}

void Http::ResetRequestData()
{
   body_size=-1;
   bytes_received=0;
   real_pos=no_ranges?0:-1;
   status.set(0);
   status_consumed=0;
   line.set(0);
   sent_eot=false;
   keep_alive=false;
   keep_alive_max=-1;
   array_send=array_ptr;
   chunked=false;
   chunk_size=-1;
   chunk_pos=0;
   chunked_trailer=false;
   seen_ranges_bytes=false;
}

void Http::Close()
{
   if(mode==CLOSED)
      return;
   if(recv_buf)
      recv_buf->Roll();	// try to read any remaining data
   if(sock!=-1 && keep_alive && (keep_alive_max>0 || keep_alive_max==-1)
   && mode!=STORE && !recv_buf->Eof() && (state==RECEIVING_BODY || state==DONE))
   {
      recv_buf->Resume();
      recv_buf->Roll();
      if(xstrcmp(last_method,"HEAD"))
      {
	 // check if all data are in buffer
	 if(!chunked)	// chunked is a bit complex, so don't handle it
	 {
	    bytes_received+=recv_buf->Size();
	    recv_buf->Skip(recv_buf->Size());
	 }
	 if(!(body_size>=0 && bytes_received==body_size))
	    goto disconnect;
      }
      // can reuse the connection.
      state=CONNECTED;
      ResetRequestData();
      rate_limit=0;
   }
   else
   {
   disconnect:
      try_time=0;
      Disconnect();
   }
   array_send=0;
   no_cache_this=false;
   no_ranges=false;
   use_propfind_now=QueryBool("use-propfind",hostname);
   special=HTTP_NONE;
   special_data.set(0);
   super::Close();
}

void Http::Send(const char *format,...)
{
   va_list va;
   va_start(va,format);
   xstring& str=xstring::vformat(format,va);
   va_end(va);
   LogSend(5,str);
   send_buf->Put(str);
}

void Http::SendMethod(const char *method,const char *efile)
{
   xstring& stripped_hostname=xstring::get_tmp(hostname);
   stripped_hostname.truncate_at('%');
   xstring& ehost=url::encode(stripped_hostname,URL_HOST_UNSAFE);
   if(portname) {
      ehost.append(':');
      ehost.append(url::encode(portname,URL_PORT_UNSAFE));
   }
   if(!use_head && !strcmp(method,"HEAD"))
      method="GET";
   last_method=method;
   if(file_url)
   {
      efile=file_url;
      if(!proxy)
	 efile+=url::path_index(efile);
      else if(!strncmp(efile,"hftp://",7))
	 efile++;
   }

   if(hftp && mode!=LONG_LIST && mode!=CHANGE_DIR && mode!=MAKE_DIR
   && mode!=REMOVE && mode!=REMOVE_DIR
   && (strlen(efile)<7 || strncmp(efile+strlen(efile)-7,";type=",6))
   && QueryBool("use-type",hostname))
   {
      char *pfile=alloca_strdup2(efile,7);
      sprintf(pfile,"%s;type=%c",efile,ascii?'a':'i');
      efile=pfile;
   }

   /*
      Handle the case when the user has not given us
      get http://foobar.org (note the absense of the trailing /

      It fixes segfault with a certain webserver which I've
      seen ... (Geoffrey Lee <glee@gnupilgrims.org>).
   */
   if(*efile=='\0')
      efile="/";

   Send("%s %s HTTP/1.1\r\n",method,efile);
   Send("Host: %s\r\n",ehost.get());
   if(user_agent && user_agent[0])
      Send("User-Agent: %s\r\n",user_agent);
   if(!hftp)
   {
      const char *content_type=0;
      if(!strcmp(method,"PUT"))
	 content_type=Query("put-content-type",hostname);
      else if(!strcmp(method,"POST"))
	 content_type=Query("post-content-type",hostname);
      if(content_type && content_type[0])
	 Send("Content-Type: %s\r\n",content_type);

      const char *accept=Query("accept",hostname);
      if(accept && accept[0])
	 Send("Accept: %s\r\n",accept);
      accept=Query("accept-language",hostname);
      if(accept && accept[0])
	 Send("Accept-Language: %s\r\n",accept);
      accept=Query("accept-charset",hostname);
      if(accept && accept[0])
	 Send("Accept-Charset: %s\r\n",accept);

      const char *referer=Query("referer",hostname);
      const char *slash="";
      if(!xstrcmp(referer,"."))
      {
	 referer=GetConnectURL(NO_USER+NO_PASSWORD);
	 if(last_char(referer)!='/' && !cwd.is_file)
	    slash="/";
      }
      if(referer && referer[0])
	 Send("Referer: %s%s\r\n",referer,slash);

      xstring cookie;
      MakeCookie(cookie,hostname,efile+(proxy?url::path_index(efile):0));
      if(cookie.length()>0)
	 Send("Cookie: %s\r\n",cookie.get());
   }
}

void Http::SendBasicAuth(const char *tag,const char *auth)
{
   if(!auth || !*auth)
      return;
   int auth_len=strlen(auth);
   char *buf64=string_alloca(base64_length(auth_len)+1);
   base64_encode(auth,buf64,auth_len);
   Send("%s: Basic %s\r\n",tag,buf64);
}
void Http::SendBasicAuth(const char *tag,const char *user,const char *pass)
{
   /* Basic scheme */
   SendBasicAuth(tag,xstring::cat(user,":",pass,NULL));
}

void Http::SendAuth()
{
   if(proxy && proxy_user && proxy_pass)
      SendBasicAuth("Proxy-Authorization",proxy_user,proxy_pass);
   if(user && pass && !(hftp && !QueryBool("use-authorization",proxy)))
      SendBasicAuth("Authorization",user,pass);
   else if(!hftp)
      SendBasicAuth("Authorization",Query("authorization",hostname));
}
void Http::SendCacheControl()
{
   const char *cc_setting=Query("cache-control",hostname);
   const char *cc_no_cache=(no_cache || no_cache_this)?"no-cache":0;
   if(!*cc_setting)
      cc_setting=0;
   if(!cc_setting && !cc_no_cache)
      return;
   int cc_no_cache_len=xstrlen(cc_no_cache);
   if(cc_no_cache && cc_setting)
   {
      const char *pos=strstr(cc_setting,cc_no_cache);
      if(pos && (pos==cc_setting || pos[-1]==' ')
	     && (pos[cc_no_cache_len]==0 || pos[cc_no_cache_len]==' '))
	 cc_no_cache=0, cc_no_cache_len=0;
   }
   xstring& cc=xstring::join(",",2,cc_no_cache,cc_setting);
   if(*cc)
      Send("Cache-Control: %s\r\n",cc.get());
}

bool Http::ModeSupported()
{
   switch((open_mode)mode)
   {
   case CLOSED:
   case QUOTE_CMD:
   case LIST:
   case CHANGE_MODE:
      return false;
   case CONNECT_VERIFY:
   case RETRIEVE:
   case STORE:
   case MAKE_DIR:
   case CHANGE_DIR:
   case ARRAY_INFO:
   case REMOVE_DIR:
   case REMOVE:
   case LONG_LIST:
   case RENAME:
      return true;
   case MP_LIST:
#if USE_EXPAT
      return QueryBool("use-propfind",hostname);
#else
      // without XML parser it is meaningless to retrieve XML file info.
      return false;
#endif
   }
   abort(); // should not happen
}

void Http::DirFile(xstring& path,const char *ecwd,const char *efile)
{
   int base=path.length();
   if(!strcmp(ecwd,"~") && !hftp)
      ecwd="";
   const char *sep=(last_char(ecwd)=='/'?"":"/");
   if(efile[0]==0)
      sep="";
   const char *pre=(ecwd[0]=='/'?"":"/");
   if(efile[0]=='/')
      path.append(efile);
   else if(efile[0]=='~')
      path.vappend("/",efile,NULL);
   else
      path.vappend(pre,ecwd,sep,efile,NULL);

   if(path[base+1]=='~' && path[base+2]==0)
      path.truncate(base+1);
   else if(path[base+1]=='~' && path[base+2]=='/')
      path.set_substr(base,2,"");
}

void Http::SendRequest(const char *connection,const char *f)
{
   xstring efile;
   xstring ecwd;
   bool add_slash=true;

   if(mode==CHANGE_DIR && new_cwd && new_cwd->url)
   {
      const char *efile_c=new_cwd->url+url::path_index(new_cwd->url);
      if(!*efile_c)
	 efile_c="/";
      efile.set(efile_c);
      add_slash=false;
   }
   else
      efile.set(url::encode(f,URL_PATH_UNSAFE));

   if(cwd.url)
      ecwd.set(cwd.url+url::path_index(cwd.url));
   else
   {
      ecwd.set(url::encode(cwd,URL_PATH_UNSAFE));
      if(hftp && ecwd[0]=='/' && ecwd[1]!='~')
      {
	 // root directory in ftp urls needs special encoding. (/%2Fpath)
	 ecwd.set_substr(1,0,"%2F");
      }
   }

   if(cwd.is_file)
   {
      if(efile[0])
	 ecwd.truncate(basename_ptr(ecwd+(!strncmp(ecwd,"/~",2)))-ecwd);
      add_slash=false;
   }
   if(mode==CHANGE_DIR && new_cwd && !new_cwd->url)
      add_slash=!new_cwd->is_file;

   const char *allprop=	// PROPFIND request
      "<?xml version=\"1.0\" ?>"
      "<propfind xmlns=\"DAV:\">"
        "<allprop/>"
      "</propfind>\r\n";

   xstring pfile;
   if(proxy && !https)
   {
      const char *proto="http";
      if(hftp)
	 proto="ftp";
      pfile.vset(proto,"://",NULL);
      if(hftp && user && pass)
      {
	 pfile.append(url::encode(user,URL_USER_UNSAFE));
	 if(!QueryBool("use-authorization",proxy))
	 {
	    pfile.append(':');
	    pfile.append(url::encode(pass,URL_PASS_UNSAFE));
	 }
	 pfile.append('@');
      }
      pfile.append(url::encode(hostname,URL_HOST_UNSAFE));
      if(portname)
      {
	 pfile.append(':');
	 pfile.append(url::encode(portname,URL_PORT_UNSAFE));
      }
   }
   else
   {
      pfile.set("");
   }

   DirFile(pfile,ecwd,efile);
   efile.set(pfile);

   if(pos==0)
      real_pos=0;
   if(mode==STORE)    // can't seek before writing
      real_pos=pos;

#ifdef DEBUG_MP_LIST
   if(mode==RETRIEVE && file[0]==0)
      mode=MP_LIST;
#endif

   switch((open_mode)mode)
   {
   case CLOSED:
   case CONNECT_VERIFY:
      abort(); // cannot happen

   case QUOTE_CMD:
      switch(special)
      {
      case HTTP_POST:
	 entity_size=special_data.length();
	 goto send_post;
      case HTTP_MOVE:
      case HTTP_COPY:
	 SendMethod(special==HTTP_MOVE?"MOVE":"COPY",efile);
	 Send("Destination: %s\r\n",special_data.get());
	 break;
      case HTTP_NONE:
	 abort(); // cannot happen
      }
      break;

   case LIST:
   case CHANGE_MODE:
      abort(); // unsupported

   case RETRIEVE:
   retrieve:
      SendMethod("GET",efile);
      if(pos>0 && !no_ranges)
      {
	 if(limit==FILE_END)
	    Send("Range: bytes=%lld-\r\n",(long long)pos);
	 else
	    Send("Range: bytes=%lld-%lld\r\n",(long long)pos,(long long)limit-1);
      }
      break;

   case STORE:
      if(hftp || strcasecmp(Query("put-method",hostname),"POST"))
	 SendMethod("PUT",efile);
      else
      {
      send_post:
	 SendMethod("POST",efile);
	 pos=0;
      }
      if(entity_size>=0)
	 Send("Content-length: %lld\r\n",(long long)(entity_size-pos));
      if(pos>0 && entity_size<0)
      {
	 if(limit==FILE_END)
	    Send("Range: bytes=%lld-\r\n",(long long)pos);
	 else
	    Send("Range: bytes=%lld-%lld\r\n",(long long)pos,(long long)limit-1);
      }
      else if(pos>0)
      {
	 Send("Range: bytes=%lld-%lld/%lld\r\n",(long long)pos,
		     (long long)((limit==FILE_END || limit>entity_size ? entity_size : limit)-1),
		     (long long)entity_size);
      }
      if(entity_date!=NO_DATE)
      {
	 char d[256];
	 static const char weekday_names[][4]={
	    "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
	 };
	 struct tm *t=gmtime(&entity_date);
	 sprintf(d,"%s, %2d %s %04d %02d:%02d:%02d GMT",
	    weekday_names[t->tm_wday],t->tm_mday,month_names[t->tm_mon],
	    t->tm_year+1900,t->tm_hour,t->tm_min,t->tm_sec);
	 Send("Last-Modified: %s\r\n",d);
      }
      break;

   case CHANGE_DIR:
   case LONG_LIST:
   case MP_LIST:
   case MAKE_DIR:
      if(last_char(efile)!='/' && add_slash)
	 efile.append('/');
      if(mode==CHANGE_DIR)
      {
	 if(use_propfind_now)
	 {
	    SendMethod("PROPFIND",efile);
	    Send("Depth: 0\r\n"); // no directory listing required
	    Send("Content-Type: text/xml\r\n");
	    Send("Content-Length: %d\r\n",(int)strlen(allprop));
	 }
	 else
	    SendMethod("HEAD",efile);
      }
      else if(mode==LONG_LIST)
	 goto retrieve;
      else if(mode==MAKE_DIR)
      {
	 if(QueryBool("use-mkcol"))
	    SendMethod("MKCOL",efile);
	 else
	 {
	    SendMethod("PUT",efile);
	    Send("Content-Length: 0\r\n");
	 }
	 pos=entity_size=0;
      }
      else if(mode==MP_LIST)
      {
	 SendMethod("PROPFIND",efile);
	 Send("Depth: 1\r\n"); // directory listing required
	 Send("Content-Type: text/xml\r\n");
	 Send("Content-Length: %d\r\n",(int)strlen(allprop));
	 pos=0;
      }
      break;

   case(REMOVE):
   case(REMOVE_DIR):
      SendMethod("DELETE",efile);
      if(mode==REMOVE)
	 Send("Depth: 0\r\n"); // deny directory removal
      break;

   case ARRAY_INFO:
      SendMethod("HEAD",efile);
      break;

   case RENAME:
      {
	 SendMethod("MOVE",efile);
	 Send("Destination: %s\r\n",GetFileURL(file1));
      }
   }
   SendAuth();
   if(no_cache || no_cache_this)
      Send("Pragma: no-cache\r\n"); // for HTTP/1.0 compatibility
   SendCacheControl();
   if(mode==ARRAY_INFO && !use_head)
      connection="close";
   else if(mode!=STORE)
      connection="keep-alive";
   if(mode!=ARRAY_INFO || connection)
      Send("Connection: %s\r\n",connection?connection:"close");
   Send("\r\n");
   if(special==HTTP_POST)
   {
      if(special_data)
	 Send("%s",special_data.get());
      entity_size=NO_SIZE;
   }
   else if(mode==MP_LIST || (mode==CHANGE_DIR && use_propfind_now))
   {
      Send("%s",allprop);
   }

   keep_alive=false;
   chunked=false;
   chunk_size=-1;
   chunk_pos=0;
   chunked_trailer=false;
   no_ranges=false;

   send_buf->SetPos(0);
}

void Http::SendArrayInfoRequest()
{
   int m=1;
   if(keep_alive && use_head)
   {
      m=keep_alive_max;
      if(m==-1)
	 m=100;
   }
   while(array_send-array_ptr<m && array_send<array_cnt)
   {
      SendRequest(array_send==array_cnt-1 ? 0 : "keep-alive",
	 array_for_info[array_send].file);
      array_send++;
   }
}

static const char *extract_quoted_header_value(const char *value)
{
   static xstring value1;
   if(*value=='"')
   {
      value++;
      value1.set(value);
      char *store=value1.get_non_const();
      while(*value && *value!='"')
      {
	 if(*value=='\\' && value[1])
	    value++;
	 *store++=*value++;
      }
      *store=0;
   }
   else
   {
      int end=strcspn(value,"()<>@,;:\\\"/[]?={} \t");
      value1.set(value);
      value1.truncate(end);
   }
   return value1;
}

void Http::HandleHeaderLine(const char *name,const char *value)
{
   if(!strcasecmp(name,"Content-length"))
   {
      long long bs=0;
      if(1!=sscanf(value,"%lld",&bs))
	 return;
      if(bs<0) // try to workaround broken servers
	 bs+=0x100000000LL;
      body_size=bs;
      if(pos==0 && mode!=STORE && mode!=MAKE_DIR)
	 entity_size=body_size;
      if(pos==0 && opt_size && H_20X(status_code))
	 *opt_size=body_size;

      if(mode==ARRAY_INFO && H_20X(status_code))
      {
	 array_for_info[array_ptr].size=body_size;
	 array_for_info[array_ptr].get_size=false;
	 TrySuccess();
      }
      return;
   }
   if(!strcasecmp(name,"Content-range"))
   {
      long long first,last,fsize;
      if(H_REQUESTED_RANGE_NOT_SATISFIABLE(status_code))
      {
	 if(sscanf(value,"%*[^/]/%lld",&fsize)!=1)
	    return;
	 if(opt_size)
	    *opt_size=fsize;
	 return;
      }
      if(sscanf(value,"%*s %lld-%lld/%lld",&first,&last,&fsize)!=3)
	 return;
      real_pos=first;
      if(last==-1)
	 last=fsize-first-1;
      if(body_size<0)
	 body_size=last-first+1;
      if(mode!=STORE && mode!=MAKE_DIR)
	 entity_size=fsize;
      if(opt_size && H_20X(status_code))
	 *opt_size=fsize;
      return;
   }
   if(!strcasecmp(name,"Last-Modified"))
   {
      time_t t=Http::atotm(value);
      if(opt_date && H_20X(status_code))
	 *opt_date=t;

      if(mode==ARRAY_INFO && H_20X(status_code))
      {
	 array_for_info[array_ptr].time=t;
	 array_for_info[array_ptr].get_time=false;
	 TrySuccess();
      }
      return;
   }
   if(!strcasecmp(name,"Location"))
   {
      location.set(value);
      return;
   }
   if(!strcasecmp(name,"Keep-Alive"))
   {
      keep_alive=true;
      const char *m=strstr(value,"max=");
      if(m) {
	 if(sscanf(m+4,"%d",&keep_alive_max)!=1)
	    keep_alive=false;
      } else
	 keep_alive_max=100;
      return;
   }
   if(!strcasecmp(name,"Connection")
   || !strcasecmp(name,"Proxy-Connection"))
   {
      if(!strcasecmp(value,"keep-alive"))
	 keep_alive=true;
      else if(!strcasecmp(value,"close"))
	 keep_alive=false;
      return;
   }
   if(!strcasecmp(name,"Transfer-Encoding"))
   {
      if(!strcasecmp(value,"identity"))
	 return;
      if(!strcasecmp(value,"chunked"));
      {
	 chunked=true;
	 chunk_size=-1;	// to indicate "before first chunk"
	 chunk_pos=0;
	 chunked_trailer=false;
      }
      // handle gzip?
      return;
   }
   if(!strcasecmp(name,"Accept-Ranges"))
   {
      if(!strcasecmp(value,"none"))
	 no_ranges=true;
      if(strstr(value,"bytes"))
	 seen_ranges_bytes=true;
      return;
   }
   if(!strcasecmp(name,"Set-Cookie"))
   {
      if(!hftp && QueryBool("set-cookies",hostname))
	 SetCookie(value);
      return;
   }
   if(!strcasecmp(name,"Content-Disposition"))
   {
      const char *filename=strstr(value,"filename=");
      if(!filename)
	 return;
      filename=extract_quoted_header_value(filename+9);
      SetSuggestedFileName(filename);
      return;
   }
   if(!strcasecmp(name,"Content-Type"))
   {
      entity_content_type.set(value);
      const char *cs=strstr(value,"charset=");
      if(cs)
      {
	 cs=extract_quoted_header_value(cs+8);
	 entity_charset.set(cs);
      }
   }
}

static
const char *find_eol(const char *p,int len,int *eol_size)
{
   *eol_size=1;
   for(int i=0; i<len; i++,p++)
   {
      if(p[0]=='\n')
	 return p;
      if(i+1<len && p[1]=='\n' && p[0]=='\r')
      {
	 *eol_size=2;
	 return p;
      }
   }
   *eol_size=0;
   return 0;
}

void Http::GetBetterConnection(int level)
{
   if(level==0)
      return;
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Http *o=(Http*)fo; // we are sure it is Http.

      if(o->sock==-1 || o->state==CONNECTING)
	 continue;

      if(o->tunnel_state==TUNNEL_WAITING)
	 continue;

      if(o->state!=CONNECTED || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || (o->priority>=priority && !o->IsSuspended()))
	    continue;
	 o->Disconnect();
	 return;
      }

      // so borrow the connection
      MoveConnectionHere(o);
      return;
   }
}

int Http::Do()
{
   int m=STALL;
   int res;
   const char *buf;
   int len;

   // check if idle time exceeded
   if(mode==CLOSED && sock!=-1 && idle_timer.Stopped())
   {
      LogNote(1,_("Closing idle connection"));
      Disconnect();
      return m;
   }

   if(home.path==0)
      home.Set(default_cwd);
   ExpandTildeInCWD();

   if(Error())
      return m;

   switch(state)
   {
   case DISCONNECTED:
      if(mode==CLOSED || !hostname)
	 return m;
      if(mode==STORE && pos>0 && entity_size>=0 && pos>=entity_size)
      {
	 state=DONE;
	 return MOVED;
      }
      if(!hftp && mode==QUOTE_CMD && !special)
      {
      handle_quote_cmd:
	 if(file && !strncasecmp(file,"Set-Cookie ",11))
	    SetCookie(file+11);
	 else if(file && !strncasecmp(file,"POST ",5))
	    special=HTTP_POST;
	 else if(file && !strncasecmp(file,"COPY ",5))
	    special=HTTP_COPY;
	 else if(file && !strncasecmp(file,"MOVE ",5))
	    special=HTTP_MOVE;
	 else
	 {
	    SetError(NOT_SUPP,0);
	    return MOVED;
	 }
	 if(special)
	 {
	    // METHOD encoded_path data
	    const char *scan=file+5;
	    while(*scan==' ')
	       scan++;
	    char *url=string_alloca(5+xstrlen(hostname)*3+1+xstrlen(portname)*3
				    +1+xstrlen(cwd)*3+1+strlen(scan)+1);
	    strcpy(url,https?"https://":"http://");
	    url::encode_string(hostname,url+strlen(url),URL_HOST_UNSAFE);
	    if(portname)
	    {
	       strcat(url,":");
	       url::encode_string(portname,url+strlen(url),URL_PORT_UNSAFE);
	    }
	    if(*scan!='/' && cwd)
	    {
	       if(cwd[0]!='/')
		  strcat(url,"/");
	       url::encode_string(cwd,url+strlen(url),URL_PATH_UNSAFE);
	    }
	    if(*scan!='/')
	       strcat(url,"/");
	    strcat(url,scan);

	    file_url.set(url);
	    file_url.truncate_at(' ');

	    scan=strchr(scan,' ');
	    while(scan && *scan==' ')
	       scan++;
	    special_data.set(scan);
	    return MOVED;
	 }
	 state=DONE;
	 return MOVED;
      }
      if(!special && !ModeSupported())
      {
	 SetError(NOT_SUPP);
	 return MOVED;
      }
      if(hftp)
      {
	 if(!proxy)
	 {
	    // problem here: hftp cannot work without proxy
	    SetError(FATAL,_("ftp over http cannot work without proxy, set hftp:proxy."));
	    return MOVED;
	 }
      }

      // walk through Http classes and try to find identical idle session
      // first try "easy" cases of session take-over.
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>CountConnections()))
	    break;
	 GetBetterConnection(i);
	 if(state!=DISCONNECTED)
	    return MOVED;
      }

      if(!resolver && mode!=CONNECT_VERIFY && !ReconnectAllowed())
	 return m;

      if(https)
	 m|=Resolve(HTTPS_DEFAULT_PORT,"https","tcp");
      else
	 m|=Resolve(HTTP_DEFAULT_PORT,"http","tcp");
      if(!peer)
	 return m;

      if(mode==CONNECT_VERIFY)
	 return m;

      if(!ReconnectAllowed())
	 return m;

      if(!NextTry())
      	 return MOVED;

      sock=SocketCreateTCP(peer[peer_curr].sa.sa_family);
      if(sock==-1)
      {
	 if(peer_curr+1<peer.count())
	 {
	    peer_curr++;
	    retries--;
	    return MOVED;
	 }
	 if(NonFatalError(errno))
	    return m;
	 char str[256];
	 sprintf(str,_("cannot create socket of address family %d"),
			peer[peer_curr].sa.sa_family);
	 SetError(SEE_ERRNO,str);
	 return MOVED;
      }

      SayConnectingTo();
      res=SocketConnect(sock,&peer[peer_curr]);
      if(res==-1 && errno!=EINPROGRESS)
      {
	 NextPeer();
	 LogError(0,"connect: %s\n",strerror(errno));
	 Disconnect();
	 if(NotSerious(errno))
	    return MOVED;
	 goto system_error;
      }
      state=CONNECTING;
      m=MOVED;
      timeout_timer.Reset();

   case CONNECTING:
      res=Poll(sock,POLLOUT);
      if(res==-1)
      {
	 NextPeer();
	 Disconnect();
	 return MOVED;
      }
      if(!(res&POLLOUT))
      {
	 if(CheckTimeout())
	 {
	    NextPeer();
	    return MOVED;
	 }
	 Block(sock,POLLOUT);
	 return m;
      }

      m=MOVED;
      state=CONNECTED;
#if USE_SSL
      if(proxy?!strncmp(proxy,"https://",8):https)
      {
	 MakeSSLBuffers();
      }
      else
#endif
      {
	 send_buf=new IOBufferFDStream(
	    new FDStream(sock,"<output-socket>"),IOBuffer::PUT);
	 recv_buf=new IOBufferFDStream(
	    new FDStream(sock,"<input-socket>"),IOBuffer::GET);
#if USE_SSL
	 if(proxy && https)
	 {
	    // have to setup a tunnel.
	    char *ehost=string_alloca(strlen(hostname)*3+1);
	    const char *port_to_use=portname?portname.get():HTTPS_DEFAULT_PORT;
	    char *eport=string_alloca(strlen(port_to_use)*3+1);
	    url::encode_string(hostname,ehost,URL_HOST_UNSAFE);
	    url::encode_string(port_to_use,eport,URL_PORT_UNSAFE);
	    Send("CONNECT %s:%s HTTP/1.1\r\n\r\n",ehost,eport);
	    tunnel_state=TUNNEL_WAITING;
	    state=RECEIVING_HEADER;
	    return MOVED;
	 }
#endif // USE_SSL
      }
      /*fallthrough*/
   case CONNECTED:
      if(mode==CONNECT_VERIFY)
	 return MOVED;

      if(mode==QUOTE_CMD && !special)
	 goto handle_quote_cmd;
      if(recv_buf->Eof())
      {
	 LogError(0,_("Peer closed connection"));
	 Disconnect();
	 return MOVED;
      }
      if(mode==CLOSED)
	 return m;
      if(!special && !ModeSupported())
      {
	 SetError(NOT_SUPP);
	 return MOVED;
      }
      if(mode==STORE && pos>0 && entity_size>=0 && pos>=entity_size)
      {
	 state=DONE;
	 return MOVED;
      }
      LogNote(9,_("Sending request..."));
      if(mode==ARRAY_INFO)
      {
	 SendArrayInfoRequest();
      }
      else
      {
	 SendRequest();
      }

      state=RECEIVING_HEADER;
      m=MOVED;
      if(mode==STORE)
	 rate_limit=new RateLimit(hostname);

   case RECEIVING_HEADER:
      if(send_buf->Error() || recv_buf->Error())
      {
	 if((mode==STORE || special) && status_code && !H_20X(status_code))
	    goto pre_RECEIVING_BODY;   // assume error.
      handle_buf_error:
	 if(send_buf->Error())
	 {
	    LogError(0,"send: %s",send_buf->ErrorText());
	    if(send_buf->ErrorFatal())
	       SetError(FATAL,send_buf->ErrorText());
	 }
	 if(recv_buf->Error())
	 {
	    LogError(0,"recv: %s",recv_buf->ErrorText());
	    if(recv_buf->ErrorFatal())
	       SetError(FATAL,recv_buf->ErrorText());
	 }
	 Disconnect();
	 return MOVED;
      }
      timeout_timer.Reset(send_buf->EventTime());
      timeout_timer.Reset(recv_buf->EventTime());
      if(CheckTimeout())
	 return MOVED;
      recv_buf->Get(&buf,&len);
      if(!buf)
      {
	 // eof
	 LogError(0,_("Hit EOF while fetching headers"));
	 // workaround some broken servers
	 if(H_REDIRECTED(status_code) && location)
	    goto pre_RECEIVING_BODY;
	 Disconnect();
	 return MOVED;
      }
      if(len>0)
      {
	 int eol_size;
	 const char *eol=find_eol(buf,len,&eol_size);
	 if(eol)
	 {
	    // empty line indicates end of headers.
	    if(eol==buf && status)
	    {
	       LogRecv(4,"");
	       recv_buf->Skip(eol_size);
	       if(tunnel_state==TUNNEL_WAITING)
	       {
		  if(H_20X(status_code))
		  {
#if USE_SSL
		     if(https)
			MakeSSLBuffers();
#endif
		     tunnel_state=TUNNEL_ESTABLISHED;
		     ResetRequestData();
		     state=CONNECTED;
		     return MOVED;
		  }
	       }
	       if(chunked_trailer)
	       {
		  chunked_trailer=false;
		  chunked=false;
		  state=DONE;
		  return MOVED;
	       }
	       if(H_CONTINUE(status_code))
	       {
		  status.set(0);
		  status_code=0;
		  return MOVED;
	       }
	       if(mode==ARRAY_INFO)
	       {
		  // we'll have to receive next header
		  status.set(0);
		  status_code=0;
		  if(array_for_info[array_ptr].get_time)
		     array_for_info[array_ptr].time=NO_DATE;
		  if(array_for_info[array_ptr].get_size)
		     array_for_info[array_ptr].size=NO_SIZE;
		  if(++array_ptr>=array_cnt)
		  {
		     state=DONE;
		     return MOVED;
		  }
		  // we can avoid reconnection if server supports it.
		  if(keep_alive && (keep_alive_max>1 || keep_alive_max==-1) && use_head)
		  {
		     SendArrayInfoRequest();
		  }
		  else
		  {
		     Disconnect();
		     try_time=0;
		  }
		  return MOVED;
	       }
	       else if(mode==STORE || mode==MAKE_DIR)
	       {
		  if((sent_eot || pos==entity_size) && H_20X(status_code))
		  {
		     state=DONE;
		     Disconnect();
		     state=DONE;
		     return MOVED;
		  }
		  if(H_20X(status_code))
		  {
		     // should never happen
		     LogError(0,"Success, but did nothing??");
		     Disconnect();
		     return MOVED;
		  }
		  // going to pre_RECEIVING_BODY to catch error
	       }
	       goto pre_RECEIVING_BODY;
	    }
	    len=eol-buf;
	    line.nset(buf,len);

	    recv_buf->Skip(len+eol_size);

	    LogRecv(4,line);
	    m=MOVED;

	    if(status==0)
	    {
	       // it's status line
	       status.set(line);
	       int ver_major,ver_minor;
	       if(3!=sscanf(status,"HTTP/%d.%d %n%d",&ver_major,&ver_minor,
		     &status_consumed,&status_code))
	       {
		  // simple 0.9 ?
		  proto_version=0x09;
		  status_code=200;
		  LogError(0,_("Could not parse HTTP status line"));
		  if(mode==STORE)
		  {
		     state=DONE;
		     Disconnect();
		     state=DONE;
		     return MOVED;
		  }
		  recv_buf->UnSkip(len+eol_size);
		  goto pre_RECEIVING_BODY;
	       }
	       proto_version=(ver_major<<4)+ver_minor;

	       // HTTP/1.1 does keep-alive by default
	       if(proto_version>=0x11)
		  keep_alive=true;

	       if(!H_20X(status_code))
	       {
		  if(H_CONTINUE(status_code))
		     return MOVED;

		  if(status_code/100==5) // server failed, try another
		     NextPeer();
		  if(status_code==504) // Gateway Timeout
		  {
		     const char *cc=Query("cache-control");
		     if(cc && strstr(cc,"only-if-cached"))
		     {
			if(mode!=ARRAY_INFO)
			{
			   SetError(NO_FILE,_("Object is not cached and http:cache-control has only-if-cached"));
			   return MOVED;
			}
			status_code=406;  // so that no retry will be attempted
		     }
		  }
		  // check for retriable codes
		  if(status_code==408  // Request Timeout
		  || status_code==502  // Bad Gateway
		  || status_code==503  // Service Unavailable
		  || status_code==504) // Gateway Timeout
		  {
		     Disconnect();
		     return MOVED;
		  }

		  if(mode==ARRAY_INFO)
		     TrySuccess();

		  return MOVED;
	       }
	    }
	    else
	    {
	       // header line.
	       char *colon=strchr(line.get_non_const(),':');
	       if(colon)
	       {
		  *colon=0;
		  colon++;
		  while(*colon && *colon==' ')
		     colon++;
		  HandleHeaderLine(line,colon);
	       }
	    }
	 }
      }

      if(mode==STORE && (!status || H_CONTINUE(status_code)) && !sent_eot)
	 Block(sock,POLLOUT);

      return m;

   pre_RECEIVING_BODY:

      // 204 No Content
      if(H_EMPTY(status_code) && body_size<0)
	 body_size=0;

      if(H_REDIRECTED(status_code))
      {
	 // check if it is redirection to the same server
	 // or to directory instead of file.
	 // FIXME.
      }

      if(H_REQUESTED_RANGE_NOT_SATISFIABLE(status_code))
      {
	 // file is smaller than requested
	 state=DONE;
	 return MOVED;
      }

      if(!H_20X(status_code))
      {
	 char *err=string_alloca(strlen(status)+strlen(file)+strlen(cwd)+xstrlen(location)+20);
	 int code=NO_FILE;

	 if(H_REDIRECTED(status_code))
	 {
	    if(location && !url::is_url(location)
	    && mode==QUOTE_CMD && !strncasecmp(file,"POST ",5))
	    {
	       const char *the_file=file;

	       const char *scan=file+5;
	       while(*scan==' ')
		  scan++;
	       char *the_post_file=alloca_strdup(scan);
	       char *space=strchr(the_post_file,' ');
	       if(space)
		  *space=0;
	       the_file=the_post_file;

	       char *new_location=alloca_strdup2(GetConnectURL(),
				    strlen(the_file)+strlen(location));
	       int p_ind=url::path_index(new_location);
	       if(location[0]=='/')
		  strcpy(new_location+p_ind,location);
	       else
	       {
		  if(the_file[0]=='/')
		     strcpy(new_location+p_ind,the_file);
		  else
		  {
		     char *slash=strrchr(new_location,'/');
		     strcpy(slash+1,the_file);
		  }
		  char *slash=strrchr(new_location,'/');
		  strcpy(slash+1,location);
	       }
	       location.set(new_location);
	    }
	    sprintf(err,"%s (%s -> %s)",status+status_consumed,file.get(),
				    location?location.get():"nowhere");
	    code=FILE_MOVED;
	 }
	 else
	 {
	    const char *closure=file;
	    if(status_code==400  // Bad request
	    || status_code==405  // Method Not Allowed
	    || status_code==501) // Not Implemented
	    {
	       if(status_code==400 || status_code==501)
	       {
		  if(!xstrcmp(last_method,"PROPFIND"))
		     ResMgr::Set("http:use-propfind",hostname,"no");
		  if(!xstrcmp(last_method,"MKCOL"))
		     ResMgr::Set("http:use-mkcol",hostname,"no");
	       }
	       if(mode==CHANGE_DIR && !xstrcmp(last_method,"PROPFIND"))
	       {
		  use_propfind_now=false;
		  try_time=0;
		  Disconnect();
		  return MOVED;
	       }
	       code=NOT_SUPP;
	       closure=last_method;
	    }
	    if(closure && closure[0])
	       sprintf(err,"%s (%s)",status+status_consumed,closure);
	    else
	       sprintf(err,"%s (%s%s)",status+status_consumed,cwd.path.get(),
				       (last_char(cwd)=='/')?"":"/");
	 }
	 state=RECEIVING_BODY;
	 LogErrorText();
	 SetError(code,err);
	 return MOVED;
      }
      if(mode==CHANGE_DIR)
      {
	 cwd.Set(new_cwd);
	 cache->SetDirectory(this, "", !cwd.is_file);
	 state=DONE;
	 return MOVED;
      }

      LogNote(9,_("Receiving body..."));
      rate_limit=new RateLimit(hostname);
      if(real_pos<0) // assume Range: did not work
      {
	 if(mode!=STORE && mode!=MAKE_DIR && body_size>=0)
	 {
	    entity_size=body_size;
	    if(opt_size && H_20X(status_code))
	       *opt_size=entity_size;
	 }
	 real_pos=0;
      }
      state=RECEIVING_BODY;
      m=MOVED;
   case RECEIVING_BODY:
      if(recv_buf->Error() || send_buf->Error())
	 goto handle_buf_error;
      if(recv_buf->Size()>=rate_limit->BytesAllowedToGet())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(recv_buf->Size()>=max_buf)
      {
	 recv_buf->Suspend();
	 m=MOVED;
      }
      else
      {
	 if(recv_buf->IsSuspended())
	 {
	    recv_buf->Resume();
	    if(recv_buf->Size()>0 || (recv_buf->Size()==0 && recv_buf->Eof()))
	       m=MOVED;
	 }
	 timeout_timer.Reset(send_buf->EventTime());
	 timeout_timer.Reset(recv_buf->EventTime());
	 if(recv_buf->Size()==0)
	 {
	    // check if ranges were emulated by squid
	    bool no_ranges_if_timeout=(bytes_received==0 && !seen_ranges_bytes);
	    if(CheckTimeout())
	    {
	       if(no_ranges_if_timeout)
	       {
		  no_ranges=true;
		  real_pos=0; // so that pget would know immediately.
	       }
	       return MOVED;
	    }
	 }
      }
      return m;

   case DONE:
      return m;
   }
   return m;

system_error:
   if(NonFatalError(errno))
      return m;
   SetError(SEE_ERRNO,0);
   Disconnect();
   return MOVED;
}

FileAccess *Http::New() { return new Http(); }
FileAccess *HFtp::New() { return new HFtp(); }

void  Http::ClassInit()
{
   // register the class
   Register("http",Http::New);
   Register("hftp",HFtp::New);
#if USE_SSL
   Register("https",Https::New);
#endif
}

void Http::SuspendInternal()
{
   if(recv_buf)
      recv_buf->SuspendSlave();
   if(send_buf)
      send_buf->SuspendSlave();
}
void Http::ResumeInternal()
{
   if(recv_buf)
      recv_buf->ResumeSlave();
   if(send_buf)
      send_buf->ResumeSlave();
}

int Http::Read(void *buf,int size)
{
   if(Error())
      return error_code;
   if(mode==CLOSED)
      return 0;
   if(state==DONE)
      return 0;	  // eof
   int res=DO_AGAIN;
   if(state==RECEIVING_BODY && real_pos>=0)
   {
      res=_Read(buf,size);
      if(res>0)
      {
	 pos+=res;
	 if(rate_limit)
	    rate_limit->BytesGot(res);
	 TrySuccess();
      }
   }
   return res;
}
int Http::_Read(void *buf,int size)
{
   const char *buf1;
   int size1;
get_again:
   if(recv_buf->Size()==0 && recv_buf->Error())
   {
      LogError(0,"recv: %s",recv_buf->ErrorText());
      if(recv_buf->ErrorFatal())
	 SetError(FATAL,recv_buf->ErrorText());
      Disconnect();
      return DO_AGAIN;
   }
   recv_buf->Get(&buf1,&size1);
   if(buf1==0) // eof
   {
      LogNote(9,_("Hit EOF"));
      if(bytes_received<body_size || chunked)
      {
	 LogError(0,_("Received not enough data, retrying"));
	 Disconnect();
	 return DO_AGAIN;
      }
      return 0;
   }
   if(!chunked)
   {
      if(body_size>=0 && bytes_received>=body_size)
      {
	 LogNote(9,_("Received all"));
	 return 0; // all received
      }
      if(entity_size>=0 && pos>=entity_size)
      {
	 LogNote(9,_("Received all (total)"));
	 return 0;
      }
   }
   if(size1==0)
      return DO_AGAIN;
   if(chunked)
   {
      if(chunked_trailer && state==RECEIVING_HEADER)
	 return DO_AGAIN;
      const char *nl;
      if(chunk_size==-1) // expecting first/next chunk
      {
	 nl=(const char*)memchr(buf1,'\n',size1);
	 if(nl==0)  // not yet
	 {
	 not_yet:
	    if(recv_buf->Eof())
	       Disconnect();	 // connection closed too early
	    return DO_AGAIN;
	 }
	 if(!is_ascii_xdigit(*buf1)
	 || sscanf(buf1,"%lx",&chunk_size)!=1)
	 {
	    Fatal(_("chunked format violated"));
	    return FATAL;
	 }
	 recv_buf->Skip(nl-buf1+1);
	 chunk_pos=0;
	 goto get_again;
      }
      if(chunk_size==0) // eof
      {
	 LogNote(9,_("Received last chunk"));
	 // headers may follow
	 chunked_trailer=true;
	 state=RECEIVING_HEADER;
	 body_size=bytes_received;
	 return DO_AGAIN;
      }
      if(chunk_pos==chunk_size)
      {
	 if(size1<2)
	    goto not_yet;
	 if(buf1[0]!='\r' || buf1[1]!='\n')
	 {
	    Fatal(_("chunked format violated"));
	    return FATAL;
	 }
	 recv_buf->Skip(2);
	 chunk_size=-1;
	 goto get_again;
      }
      // ok, now we may get portion of data
      if(size1>chunk_size-chunk_pos)
	 size1=chunk_size-chunk_pos;
   }
   else
   {
      // limit by body_size.
      if(body_size>=0 && size1+bytes_received>=body_size)
	 size1=body_size-bytes_received;
   }

   int bytes_allowed=0x10000000;
   if(rate_limit)
      bytes_allowed=rate_limit->BytesAllowedToGet();
   if(size1>bytes_allowed)
      size1=bytes_allowed;
   if(size1==0)
      return DO_AGAIN;
   if(norest_manual && real_pos==0 && pos>0)
      return DO_AGAIN;
   if(real_pos<pos)
   {
      off_t to_skip=pos-real_pos;
      if(to_skip>size1)
	 to_skip=size1;
      recv_buf->Skip(to_skip);
      real_pos+=to_skip;
      bytes_received+=to_skip;
      if(chunked)
	 chunk_pos+=to_skip;
      goto get_again;
   }
   if(size>size1)
      size=size1;
   memcpy(buf,buf1,size);
   recv_buf->Skip(size);
   if(chunked)
      chunk_pos+=size;
   real_pos+=size;
   bytes_received+=size;
   return size;
}

int Http::Done()
{
   if(mode==CLOSED)
      return OK;
   if(Error())
      return error_code;
   if(state==DONE)
      return OK;
   if(mode==CONNECT_VERIFY && (peer || sock!=-1))
      return OK;
   if((mode==REMOVE || mode==REMOVE_DIR || mode==RENAME)
   && state==RECEIVING_BODY)
      return OK;
   return IN_PROGRESS;
}

int Http::Buffered()
{
   if(mode!=STORE || !send_buf)
      return 0;
   return send_buf->Size()+SocketBuffered(sock);
}

int Http::Write(const void *buf,int size)
{
   if(mode!=STORE)
      return(0);

   Resume();
   Do();
   if(Error())
      return(error_code);

   if(state!=RECEIVING_HEADER || status!=0 || send_buf->Size()!=0)
      return DO_AGAIN;

   {
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size+send_buf->Size()>=max_buf)
      size=max_buf-send_buf->Size();
   if(entity_size!=NO_SIZE && pos+size>entity_size)
   {
      size=entity_size-pos;
      // tried to write more than originally requested. Make it retry with Open:
      if(size==0)
	 return STORE_FAILED;
   }
   if(size<=0)
      return 0;

   send_buf->Put((const char*)buf,size);

   if(retries>0 && send_buf->GetPos()-send_buf->Size()>Buffered()+0x1000)
      TrySuccess();
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   return(size);
}

int Http::SendEOT()
{
   if(sent_eot)
      return OK;
   if(Error())
      return(error_code);
   if(mode==STORE)
   {
      if(state==RECEIVING_HEADER && send_buf->Size()==0)
      {
	 if(entity_size==NO_SIZE || pos<entity_size)
	 {
	    shutdown(sock,1);
	    keep_alive=false;
	 }
	 sent_eot=true;
      	 return(OK);
      }
      return(DO_AGAIN);
   }
   return(OK);
}

int Http::StoreStatus()
{
   if(!sent_eot && state==RECEIVING_HEADER)
      SendEOT();
   return Done();
}

const char *Http::CurrentStatus()
{
   switch(state)
   {
   case DISCONNECTED:
      if(hostname)
      {
	 if(resolver)
	    return(_("Resolving host address..."));
	 if(!ReconnectAllowed())
	    return DelayingMessage();
      }
      return "";
   case CONNECTING:
      return(_("Connecting..."));
   case CONNECTED:
      return(_("Connection idle"));
   case RECEIVING_HEADER:
      if(mode==STORE && !sent_eot && !status)
	 return(_("Sending data"));
      if(tunnel_state==TUNNEL_WAITING)
	 return(_("Connecting..."));
      if(!status)
	 return(_("Waiting for response..."));
      return(_("Fetching headers..."));
   case RECEIVING_BODY:
      return(_("Receiving data"));
   case DONE:
      return "";
   }
   abort();
}

void Http::Reconfig(const char *name)
{
   const char *c=hostname;

   super::Reconfig(name);

   no_cache = !QueryBool("cache",c);
   if(!hftp && NoProxy(hostname))
      SetProxy(0);
   else
   {
      const char *p=0;
      if(hftp && vproto && !strcmp(vproto,"ftp"))
      {
	 p=ResMgr::Query("ftp:proxy",c);
	 if(p && strncmp(p,"http://",7) && strncmp(p,"https://",8))
	    p=0;
      }
      if(!p)
      {
	 if(https)
	    p=ResMgr::Query("https:proxy",c);
	 else
	    p=Query("proxy",c);
	 // if no hftp:proxy is specified, try http:proxy.
	 if(hftp && !p)
	    p=ResMgr::Query("http:proxy",c);
      }
      SetProxy(p);
   }

   if(sock!=-1)
      SetSocketBuffer(sock);
   if(proxy && proxy_port==0)
      proxy_port.set(HTTP_DEFAULT_PROXY_PORT);

   user_agent=ResMgr::Query("http:user-agent",c);
   use_propfind_now=(use_propfind_now && QueryBool("use-propfind",c));
}

bool Http::SameSiteAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   Http *o=(Http*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool Http::SameLocationAs(const FileAccess *fa) const
{
   if(!SameSiteAs(fa))
      return false;
   Http *o=(Http*)fa;
   if(cwd!=o->cwd)
      return false;
   return true;
}

void Http::ResetLocationData()
{
   super::ResetLocationData();
   Reconfig();
   state=DISCONNECTED;
   use_propfind_now=QueryBool("use-propfind",hostname);
}

DirList *Http::MakeDirList(ArgV *args)
{
   return new HttpDirList(this,args);
}
#include "FileGlob.h"
Glob *Http::MakeGlob(const char *pattern)
{
   return new GenericGlob(this,pattern);
}
ListInfo *Http::MakeListInfo(const char *path)
{
   return new HttpListInfo(this,path);
}

bool Http::CookieClosureMatch(const char *closure_c,
			      const char *hostname,const char *efile)
{
   if(!closure_c)
      return true;
   char *closure=alloca_strdup2(closure_c,1);
   char *path=0;

   char *scan=closure;
   for(;;)
   {
      char *slash=strchr(scan,';');
      if(!slash)
	 break;
      *slash++=0;
      while(*slash && *slash==' ')
	 slash++;
      if(!strncmp(slash,"path=",5))
	 path=slash+5;
      else if(!strncmp(slash,"secure",6) && (slash[6]==';' || slash[6]==0))
      {
	 if(!https)
	    return false;
      }
   }
   if(closure[0] && 0!=fnmatch(closure,hostname,FNM_PATHNAME))
      return false;
   if(!path)
      return true;
   int path_len=strlen(path);
   if(path_len>0 && path[path_len-1]=='/')
      path_len--;
   if(!strncmp(efile,path,path_len)
   && (efile[path_len]==0 || efile[path_len]=='/'))
      return true;
   return false;
}

void Http::CookieMerge(xstring &all,const char *cookie_c)
{
   char *value=alloca_strdup(cookie_c);

   for(char *entry=strtok(value,";"); entry; entry=strtok(0,";"))
   {
      if(*entry==' ')
	 entry++;
      if(*entry==0)
	 break;
      if(!strncasecmp(entry,"path=",5)
      || !strncasecmp(entry,"expires=",8)
      || !strncasecmp(entry,"domain=",7)
      || (!strncasecmp(entry,"secure",6)
	  && (entry[6]==' ' || entry[6]==0 || entry[6]==';')))
	 continue; // filter out path= expires= domain= secure

      char *c_name=entry;
      char *c_value=strchr(entry,'=');
      if(c_value)
	 *c_value++=0;
      else
	 c_value=c_name, c_name=0;
      int c_name_len=xstrlen(c_name);

      for(unsigned i=all.skip_all(0,' '); i<all.length(); i=all.skip_all(i+1,' '))
      {
	 const char *scan=all+i;
	 const char *semicolon=strchr(scan,';');
	 const char *eq=strchr(scan,'=');
	 if(semicolon && eq>semicolon)
	    eq=0;
	 if((eq==0 && c_name==0)
	 || (eq-scan==c_name_len && !strncmp(scan,c_name,c_name_len)))
	 {
	    // remove old cookie.
	    if(!semicolon)
	       all.truncate(i);
	    else
	       all.set_substr(i,all.skip_all(semicolon+1-all,' ')-i,"",0);
	    break;
	 }
	 if(!semicolon)
	    break;
	 i=semicolon+1-all;
      }

      // append cookie.
      all.rtrim(' ');
      all.rtrim(';');
      int c_len=all.length();
      if(c_len>0 && all[c_len-1]!=';')
	 all.append("; ");
      if(c_name)
	 all.vappend(c_name,"=",c_value,NULL);
      else
	 all.append(c_value);
   }
}

void Http::MakeCookie(xstring &all_cookies,const char *hostname,const char *efile)
{
   ResMgr::Resource *scan=0;
   const char *closure;
   for(;;)
   {
      const char *cookie=ResMgr::QueryNext("http:cookie",&closure,&scan);
      if(cookie==0)
	 break;
      if(!CookieClosureMatch(closure,hostname,efile))
	 continue;
      CookieMerge(all_cookies,cookie);
   }
}

void Http::SetCookie(const char *value_const)
{
   char *value=alloca_strdup(value_const);
   const char *domain=hostname;
   const char *path=0;
   bool secure=false;

   for(char *entry=strtok(value,";"); entry; entry=strtok(0,";"))
   {
      while(*entry==' ')   // skip spaces.
	 entry++;
      if(*entry==0)
	 break;

      if(!strncasecmp(entry,"expires=",8))
	 continue; // not used yet (FIXME)

      if(!strncasecmp(entry,"secure",6)
      && (entry[6]==' ' || entry[6]==0 || entry[6]==';'))
      {
	 secure=true;
	 continue;
      }

      if(!strncasecmp(entry,"path=",5))
      {
	 path=alloca_strdup(entry+5);
      	 continue;
      }

      if(!strncasecmp(entry,"domain=",7))
      {
	 char *new_domain=alloca_strdup(entry+6);
	 if(new_domain[1]=='.')
	    new_domain[0]='*';
	 else
	    new_domain++;
	 char *end=strchr(new_domain,';');
	 if(end)
	    *end=0;
	 domain=new_domain;
	 continue;
      }
   }

   char *closure=string_alloca(strlen(domain)+xstrlen(path)+32);
   strcpy(closure,domain);
   if(path && path[0] && strcmp(path,"/"))
   {
      strcat(closure,";path=");
      strcat(closure,path);
   }
   if(secure)
      strcat(closure,";secure");

   xstring c(Query("cookie",closure));
   CookieMerge(c,value_const);
   ResMgr::Set("http:cookie",closure,c);
}

#if USE_SSL
#undef super
#define super Http
Https::Https()
{
   https=true;
   res_prefix="http";
}
Https::~Https()
{
}
Https::Https(const Https *o) : super(o)
{
   https=true;
   res_prefix="http";
   Reconfig(0);
}
FileAccess *Https::New(){ return new Https();}

void Http::MakeSSLBuffers()
{
   ssl=new lftp_ssl(sock,lftp_ssl::CLIENT,hostname);
   ssl->load_keys();
   IOBufferSSL *send_buf_ssl=new IOBufferSSL(ssl,IOBuffer::PUT);
   IOBufferSSL *recv_buf_ssl=new IOBufferSSL(ssl,IOBuffer::GET);
   send_buf=send_buf_ssl;
   recv_buf=recv_buf_ssl;
}
#endif

#undef super
#define super Http
HFtp::HFtp()
{
   hftp=true;
   default_cwd="~";
   Reconfig(0);
}
HFtp::~HFtp()
{
}
HFtp::HFtp(const HFtp *o) : super(o)
{
   hftp=true;
   Reconfig(0);
}
void HFtp::Login(const char *u,const char *p)
{
   super::Login(u,p);
   if(u)
   {
      home.Set("~");
      cwd.Set(home,false,0);
   }
}
void HFtp::Reconfig(const char *name)
{
   super::Reconfig(name);
   use_head=QueryBool("use-head");
}

void Http::Cleanup()
{
   if(hostname==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}
void Http::CleanupThis()
{
   if(mode!=CLOSED)
      return;
   Disconnect();
}

void Http::LogErrorText()
{
   if(!recv_buf)
      return;
   recv_buf->Roll();
   int size=recv_buf->Size();
   if(size==0)
      return;
   char *buf=string_alloca(size+1);
   size=_Read(buf,size);
   if(size<0)
      return;
   buf[size]=0;
   remove_tags(buf);
   for(char *line=strtok(buf,"\n"); line; line=strtok(0,"\n"))
      if(*line)
	 Log::global->Format(4,"<--* %s\n",line);
}


/* The functions http_atotm and check_end are taken from wget */
#define ISSPACE(c) is_ascii_space((c))
#define ISDIGIT(c) is_ascii_digit((c))

/* Check whether the result of strptime() indicates success.
   strptime() returns the pointer to how far it got to in the string.
   The processing has been successful if the string is at `GMT' or
   `+X', or at the end of the string.

   In extended regexp parlance, the function returns 1 if P matches
   "^ *(GMT|[+-][0-9]|$)", 0 otherwise.  P being NULL (a valid result of
   strptime()) is considered a failure and 0 is returned.  */
static int
check_end (const char *p)
{
  if (!p)
    return 0;
  while (ISSPACE (*p))
    ++p;
  if (!*p
      || (p[0] == 'G' && p[1] == 'M' && p[2] == 'T')
      || ((p[0] == '+' || p[1] == '-') && ISDIGIT (p[1])))
    return 1;
  else
    return 0;
}

/* Convert TIME_STRING time to time_t.  TIME_STRING can be in any of
   the three formats RFC2068 allows the HTTP servers to emit --
   RFC1123-date, RFC850-date or asctime-date.  Timezones are ignored,
   and should be GMT.

   We use strptime() to recognize various dates, which makes it a
   little bit slacker than the RFC1123/RFC850/asctime (e.g. it always
   allows shortened dates and months, one-digit days, etc.).  It also
   allows more than one space anywhere where the specs require one SP.
   The routine should probably be even more forgiving (as recommended
   by RFC2068), but I do not have the time to write one.

   Return the computed time_t representation, or -1 if all the
   schemes fail.

   Needless to say, what we *really* need here is something like
   Marcus Hennecke's atotm(), which is forgiving, fast, to-the-point,
   and does not use strptime().  atotm() is to be found in the sources
   of `phttpd', a little-known HTTP server written by Peter Erikson.  */
time_t
Http::atotm (const char *time_string)
{
  struct tm t;

  /* Roger Beeman says: "This function dynamically allocates struct tm
     t, but does no initialization.  The only field that actually
     needs initialization is tm_isdst, since the others will be set by
     strptime.  Since strptime does not set tm_isdst, it will return
     the data structure with whatever data was in tm_isdst to begin
     with.  For those of us in timezones where DST can occur, there
     can be a one hour shift depending on the previous contents of the
     data area where the data structure is allocated."  */
  t.tm_isdst = -1;

  /* Note that under foreign locales Solaris strptime() fails to
     recognize English dates, which renders this function useless.  I
     assume that other non-GNU strptime's are plagued by the same
     disease.  We solve this by setting only LC_MESSAGES in
     i18n_initialize(), instead of LC_ALL.

     Another solution could be to temporarily set locale to C, invoke
     strptime(), and restore it back.  This is slow and dirty,
     however, and locale support other than LC_MESSAGES can mess other
     things, so I rather chose to stick with just setting LC_MESSAGES.

     Also note that none of this is necessary under GNU strptime(),
     because it recognizes both international and local dates.  */

  /* NOTE: We don't use `%n' for white space, as OSF's strptime uses
     it to eat all white space up to (and including) a newline, and
     the function fails if there is no newline (!).

     Let's hope all strptime() implementations use ` ' to skip *all*
     whitespace instead of just one (it works that way on all the
     systems I've tested it on).  */

   time_t ut=-1;

   setlocale(LC_TIME,"C"); // we need english month and week day names

   /* RFC1123: Thu, 29 Jan 1998 22:12:57 */
   if (check_end (strptime (time_string, "%a, %d %b %Y %T", &t)))
      ut=mktime_from_utc (&t);
   /* RFC850:  Thu, 29-Jan-98 22:12:57 */
   else if (check_end (strptime (time_string, "%a, %d-%b-%y %T", &t)))
      ut=mktime_from_utc (&t);
   /* asctime: Thu Jan 29 22:12:57 1998 */
   else if (check_end (strptime (time_string, "%a %b %d %T %Y", &t)))
      ut=mktime_from_utc (&t);

   setlocale(LC_TIME,"");  // restore locale

   return ut;
}


#include "modconfig.h"
#ifdef MODULE_PROTO_HTTP
void module_init()
{
   Http::ClassInit();
}
#endif
