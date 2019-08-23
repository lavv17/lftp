/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

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
#include <assert.h>
#include "Http.h"
#include "ResMgr.h"
#include "log.h"
#include "url.h"
#include "HttpAuth.h"
#include "HttpDir.h"
#include "misc.h"
#include "buffer_ssl.h"
#include "buffer_zlib.h"

#include "ascii_ctype.h"

#if !HAVE_DECL_STRPTIME
CDECL char *strptime(const char *buf, const char *format, struct tm *tm);
#endif

#define super NetAccess

#define max_buf 0x10000

#define HTTP_DEFAULT_PORT	 "80"
#define HTTP_DEFAULT_PROXY_PORT	 "3128"
#define HTTPS_DEFAULT_PORT	 "443"

enum {
   H_Continue=100,
   H_Switching_Protocols=101,
   H_Processing=102,
   H_Ok=200,
   H_Created=201,
   H_Accepted=202,
   H_Non_Authoritative_Information=203,
   H_No_Content=204,
   H_Reset_Content=205,
   H_Partial_Content=206,
   H_Multi_Status=207,
   H_Moved_Permanently=301,
   H_Found=302,
   H_See_Other=303,
   H_Not_Modified=304,
   H_Use_Proxy=305,
   H_Temporary_Redirect=307,
   H_Permanent_Redirect=308,
   H_Bad_Request=400,
   H_Unauthorized=401,
   H_Payment_Required=402,
   H_Forbidden=403,
   H_Not_Found=404,
   H_Method_Not_Allowed=405,
   H_Not_Acceptable=406,
   H_Proxy_Authentication_Required=407,
   H_Request_Timeout=408,
   H_Conflict=409,
   H_Gone=410,
   H_Length_Required=411,
   H_Precondition_Failed=412,
   H_Request_Entity_Too_Large=413,
   H_Request_URI_Too_Long=414,
   H_Unsupported_Media_Type=415,
   H_Requested_Range_Not_Satisfiable=416,
   H_Expectation_Failed=417,
   H_Unprocessable_Entity=422,
   H_Locked=423,
   H_Failed_Dependency=424,
   H_Too_Many_Requests=429,
   H_Internal_Server_Error=500,
   H_Not_Implemented=501,
   H_Bad_Gateway=502,
   H_Service_Unavailable=503,
   H_Gateway_Timeout=504,
   H_HTTP_Version_Not_Supported=505,
   H_Insufficient_Storage=507,
};

/* Some status code validation macros: */
#define H_2XX(x)        (((x) >= 200) && ((x) <= 299))
#define H_5XX(x)        (((x) >= 500) && ((x) <= 599))
#define H_PARTIAL(x)    ((x) == H_Partial_Content)
#define H_REDIRECTED(x) (((x) == H_Moved_Permanently) || ((x) == H_Found) || ((x) == H_See_Other) || ((x) == H_Temporary_Redirect) || ((x) == H_Permanent_Redirect))
#define H_EMPTY(x)	(((x) == H_No_Content) || ((x) == H_Reset_Content))
#define H_CONTINUE(x)	((x) == H_Continue || (x) == H_Processing)
#define H_REQUESTED_RANGE_NOT_SATISFIABLE(x) ((x) == H_Requested_Range_Not_Satisfiable)
#define H_TRANSIENT(x)	((x)==H_Request_Timeout || (x)==H_Bad_Gateway || (x)==H_Service_Unavailable || (x)==H_Gateway_Timeout)
#define H_UNSUPPORTED(x) ((x)==H_Bad_Request || (x)==H_Not_Implemented)
#define H_AUTH_REQ(x)	((x)==H_Unauthorized || (x)==H_Proxy_Authentication_Required)

#ifndef EINPROGRESS
#define EINPROGRESS -1
#endif

enum { CHUNK_SIZE_UNKNOWN=-1 };

Http::Connection::Connection(int s,const char *c)
   : closure(c), sock(s)
{
}
Http::Connection::~Connection()
{
   close(sock);
   /* make sure we free buffers before ssl */
   recv_buf=0;
   send_buf=0;
}
void Http::Connection::MakeBuffers()
{
   send_buf=new IOBufferFDStream(
      new FDStream(sock,"<output-socket>"),IOBuffer::PUT);
   recv_buf=new IOBufferFDStream(
      new FDStream(sock,"<input-socket>"),IOBuffer::GET);
}

void Http::Init()
{
   state=DISCONNECTED;
   tunnel_state=NO_TUNNEL;
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
   chunked_trailer=false;
   chunk_size=CHUNK_SIZE_UNKNOWN;
   chunk_pos=0;

   request_pos=0;

   no_ranges=false;
   seen_ranges_bytes=false;
   entity_date_set=false;
   sending_proppatch=false;

   no_cache_this=false;
   no_cache=false;

   auth_sent[0]=auth_sent[1]=0;
   auth_scheme[0]=auth_scheme[1]=HttpAuth::NONE;

   use_propfind_now=true;

   retry_after=0;

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
   conn=o->conn.borrow();
   conn->ResumeInternal();
   rate_limit=o->rate_limit.borrow();
   last_method=o->last_method; o->last_method=0;
   last_uri.move_here(o->last_uri);
   last_url.move_here(o->last_url);
   timeout_timer.Reset(o->timeout_timer);
   state=CONNECTED;
   tunnel_state=o->tunnel_state;
   o->Disconnect();
   ResumeInternal();
}

void Http::DisconnectLL()
{
   Enter(this);
   rate_limit=0;
   if(conn)
   {
      LogNote(7,_("Closing HTTP connection"));
      conn=0;
   }

   if(!Error() && !H_AUTH_REQ(status_code))
      auth_sent[0]=auth_sent[1]=0;

   if(state!=DONE && (real_pos>0 || special==HTTP_POST)
   && !Error() && !H_AUTH_REQ(status_code)) {
      if(last_method && !strcmp(last_method,"POST"))
	 SetError(FATAL,_("POST method failed"));
      else if(ModeIs(STORE))
	 SetError(STORE_FAILED,0);
      else if(fragile)
	 SetError(FRAGILE_FAILED,0);
   }
   if(ModeIs(STORE) && H_AUTH_REQ(status_code))
      pos=real_pos=request_pos; // resend all the data again

   last_method=0;
   last_uri.unset();
   last_url.unset();
   ResetRequestData();
   state=DISCONNECTED;
   Leave(this);
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
   array_send=fileset_for_info?fileset_for_info->curr_index():0;
   chunked=false;
   chunked_trailer=false;
   chunk_size=CHUNK_SIZE_UNKNOWN;
   chunk_pos=0;
   request_pos=0;
   propfind=0;
   inflate=0;
   seen_ranges_bytes=false;
   entity_date_set=false;
}

void Http::Close()
{
   if(mode==CLOSED)
      return;
   if(conn && conn->recv_buf)
      conn->recv_buf->Roll();	// try to read any remaining data
   if(conn && keep_alive && (keep_alive_max>0 || keep_alive_max==-1)
   && !ModeIs(STORE) && !conn->recv_buf->Eof() && (state==RECEIVING_BODY || state==DONE))
   {
      conn->recv_buf->Resume();
      conn->recv_buf->Roll();
      if(xstrcmp(last_method,"HEAD"))
      {
	 // check if all data are in buffer
	 if(!chunked)	// chunked is a bit complex, so don't handle it
	 {
	    bytes_received+=conn->recv_buf->Size();
	    conn->recv_buf->Skip(conn->recv_buf->Size());
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
      Disconnect();
      DontSleep();
   }
   array_send=0;
   no_cache_this=false;
   auth_sent[0]=auth_sent[1]=0;
   auth_scheme[0]=auth_scheme[1]=HttpAuth::NONE;
   no_ranges=!QueryBool("use-range",hostname);
   use_propfind_now=QueryBool("use-propfind",hostname);
   special=HTTP_NONE;
   special_data.set(0);
   sending_proppatch=false;
   super::Close();
}

void Http::Send(const xstring& str)
{
   if(str.length()==0)
      return;
   LogSend(5,str);
   conn->send_buf->Put(str);
}

void Http::Send(const char *format,...)
{
   va_list va;
   va_start(va,format);
   xstring& str=xstring::vformat(format,va);
   va_end(va);
   Send(str);
}

void Http::Send(const HttpHeader *hdr)
{
   Send("%s: %s\r\n",hdr->GetName(),hdr->GetValue());
}

void Http::AppendHostEncoded(xstring& buf,const char *host)
{
   if(is_ipv6_address(host))
      buf.append('[').append(host).append(']');
   else
      buf.append_url_encoded(host,URL_HOST_UNSAFE);
}

void Http::SendMethod(const char *method,const char *efile)
{
   xstring& stripped_hostname=xstring::get_tmp(hostname);
   stripped_hostname.truncate_at('%');
   xstring ehost;
   AppendHostEncoded(ehost,xidna_to_ascii(stripped_hostname));
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
      efile=xstring::format("%s;type=%c",efile,ascii?'a':'i');
   }

   /*
      Handle the case when the user has not given us
      get http://foobar.org (note the absense of the trailing /

      It fixes segfault with a certain webserver which I've
      seen ... (Geoffrey Lee <glee@gnupilgrims.org>).
   */
   if(*efile=='\0')
      efile="/";

   last_uri.set(efile+(proxy?url::path_index(efile):0));
   if(last_uri.length()==0)
      last_uri.set("/");
   if(proxy)
      last_url.set(efile);

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
      accept=Query("accept-encoding",hostname);
      if(accept && accept[0])
	 Send("Accept-Encoding: %s\r\n",accept);

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

void Http::SendAuth(HttpAuth::target_t target,const char *user,const char *uri)
{
   auth_scheme[target]=HttpAuth::NONE;
   if(!user)
      return;
   HttpAuth *auth=HttpAuth::Get(target,GetFileURL(file,NO_USER),user);
   if(auth && auth->Update(last_method,uri)) {
      auth_sent[target]++;
      Send(auth->GetHeader());
   }
}

void Http::SendProxyAuth()
{
   SendAuth(HttpAuth::PROXY,proxy_user,last_url);
}

void Http::SendAuth()
{
   if(hftp && !auth_scheme[HttpAuth::WWW] && user && pass && QueryBool("use-authorization",proxy)) {
      SendBasicAuth("Authorization",user,pass);
      return;
   }
   SendAuth(HttpAuth::WWW,user?user:auth_user,last_uri);
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
   case LINK:
   case SYMLINK:
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

void Http::DirFile(xstring& path,const xstring& ecwd,const xstring& efile) const
{
   const int base=path.length();

   if(efile[0]=='/') {
      path.append(efile);
   } else if(efile[0]=='~' || ecwd.length()==0 || (ecwd.eq("~") && !hftp)) {
      path.append('/');
      path.append(efile);
   } else {
      size_t min_len=path.length()+1;
      if(ecwd[0]!='/')
	 path.append('/');
      path.append(ecwd);
      if(ecwd.last_char()!='/' && efile.length()>0)
	 path.append('/');

      // reduce . and .. at beginning of efile:
      //  * get the minimum path length (so that we don't remove ~user)
      //  * skip .; handle .. using basename_ptr to chomp the path.
      if(path[min_len]=='~') {
	 while(path[min_len] && path[min_len]!='/')
	    ++min_len;
	 if(path[min_len]=='/')
	    ++min_len;
      }
      const char *e=efile;
      while(e[0]=='.') {
	 if(e[1]=='/' || e[1]==0)
	    ++e;
	 else if(e[1]=='.' && (e[2]=='/' || e[2]==0)) {
	    if(path.length()<=min_len)
	       break;
	    const char *bn=basename_ptr(path+min_len);
	    path.truncate(bn-path);
	    e+=2;
	 } else
	    break;
	 if(*e=='/')
	    ++e;
      }
      path.append(e);
   }

   // remove "/~" or "/~/"
   if(path[base+1]=='~' && path[base+2]==0)
      path.truncate(base+1);
   else if(path[base+1]=='~' && path[base+2]=='/')
      path.set_substr(base,2,"");
}

void Http::SendPropfind(const xstring& efile,int depth)
{
   SendMethod("PROPFIND",efile);
   Send("Depth: %d\r\n",depth);
   if(allprop.length()>0)
   {
      Send("Content-Type: text/xml\r\n");
      Send("Content-Length: %d\r\n",int(allprop.length()));
   }
}
void Http::SendPropfindBody()
{
   Send(allprop);
}

const xstring& Http::FormatLastModified(time_t lm)
{
   static const char weekday_names[][4]={
      "Sun","Mon","Tue","Wed","Thu","Fri","Sat"
   };
   const struct tm *t=gmtime(&lm);
   return xstring::format("%s, %2d %s %04d %02d:%02d:%02d GMT",
      weekday_names[t->tm_wday],t->tm_mday,month_names[t->tm_mon],
      t->tm_year+1900,t->tm_hour,t->tm_min,t->tm_sec);
}

void Http::SendProppatch(const xstring& efile)
{
   SendMethod("PROPPATCH",efile);
   xstring prop(
      "<?xml version=\"1.0\" ?>"
      "<propertyupdate xmlns=\"DAV:\">"
	 "<set>"
	    "<prop>"
	       "<getlastmodified>");
   prop.append(FormatLastModified(entity_date)).append(
	       "</getlastmodified>"
	    "</prop>"
	 "</set>"
      "</propertyupdate>");
   Send("Content-Type: text/xml\r\n");
   Send("Content-Length: %d\r\n",int(prop.length()));
   Send("\r\n");
   Send(prop);
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
      AppendHostEncoded(pfile,hostname);
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
   if(ModeIs(STORE))    // can't seek before writing
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
      case HTTP_PROPFIND:
	 SendMethod("PROPFIND",efile);
	 Send("Depth: 1\r\n"); // directory listing required
	 break;
      case HTTP_NONE:
	 abort(); // cannot happen
      }
      break;

   case LIST:
   case CHANGE_MODE:
   case LINK:
   case SYMLINK:
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
      if(sending_proppatch) {
	 SendProppatch(efile);
	 break;
      }
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
	 request_pos=pos;
	 if(limit==FILE_END)
	    Send("Range: bytes=%lld-\r\n",(long long)pos);
	 else
	    Send("Range: bytes=%lld-%lld\r\n",(long long)pos,(long long)limit-1);
      }
      else if(pos>0)
      {
	 request_pos=pos;
	 Send("Range: bytes=%lld-%lld/%lld\r\n",(long long)pos,
		     (long long)((limit==FILE_END || limit>entity_size ? entity_size : limit)-1),
		     (long long)entity_size);
      }
      if(entity_date!=NO_DATE)
      {
	 Send("Last-Modified: %s\r\n",FormatLastModified(entity_date).get());
	 Send("X-OC-MTime: %ld\r\n",(long)entity_date);	 // for OwnCloud
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
	    SendPropfind(efile,0);
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
	 SendPropfind(efile,1);
	 pos=0;
      }
      break;

   case(REMOVE):
      SendMethod("DELETE",efile);
      Send("Depth: 0\r\n"); // deny directory removal
      break;

   case(REMOVE_DIR):
      if(efile.last_char()!='/')
	 efile.append('/');
      SendMethod("DELETE",efile);
      break;

   case ARRAY_INFO:
      if(use_propfind_now)
	 SendPropfind(efile,0);
      else
	 SendMethod("HEAD",efile);
      break;

   case RENAME:
      {
	 SendMethod("MOVE",efile);
	 Send("Destination: %s\r\n",GetFileURL(file1).get());
      }
   }
   if(proxy && !https)
      SendProxyAuth();
   SendAuth();
   if(no_cache || no_cache_this)
      Send("Pragma: no-cache\r\n"); // for HTTP/1.0 compatibility
   SendCacheControl();
   if(mode==ARRAY_INFO && !use_head)
      connection="close";
   else if(!ModeIs(STORE))
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
   else if(!xstrcmp(last_method,"PROPFIND"))
      SendPropfindBody();

   keep_alive=false;
   chunked=false;
   chunked_trailer=false;
   chunk_size=CHUNK_SIZE_UNKNOWN;
   chunk_pos=0;
   request_pos=0;
   inflate=0;
   no_ranges=!QueryBool("use-range",hostname);

   conn->send_buf->SetPos(0);
}

int Http::SendArrayInfoRequest()
{
   // skip to next needed file
   for(FileInfo *fi=fileset_for_info->curr(); fi; fi=fileset_for_info->next())
      if(fi->need)
	 break;
   if(array_send<fileset_for_info->curr_index())
      array_send=fileset_for_info->curr_index();

   if(state!=CONNECTED)
      return 0;

   int m=1;
   if(keep_alive && use_head)
   {
      m=keep_alive_max;
      if(m==-1)
	 m=100;
   }
   int req_count=0;
   while(array_send-fileset_for_info->curr_index()<m
   && array_send<fileset_for_info->count())
   {
      FileInfo *fi=(*fileset_for_info)[array_send++];
      if(fi->need==0)
	 continue;
      xstring *name=&fi->name;
      if(fi->filetype==fi->DIRECTORY && name->last_char()!='/') {
	 name=&xstring::get_tmp(*name);
	 name->append('/');
      }
      if(fi->uri)
	 file_url.set(dir_file(GetConnectURL(),fi->uri));
      else
	 file_url.unset();
      SendRequest(array_send==fileset_for_info->count()-1 ? 0 : "keep-alive", *name);
      req_count++;
   }
   return req_count;
}

void Http::ProceedArrayInfo()
{
   for(;;)
   {
      // skip to next needed file
      FileInfo *fi=fileset_for_info->next();
      if(!fi || fi->need)
	 break;
   }
   if(!fileset_for_info->curr())
   {
      LogNote(10,"that was the last file info");
      // received all requested info.
      state=DONE;
      return;
   }
   // we can avoid reconnection if server supports it.
   if(keep_alive && (keep_alive_max>1 || keep_alive_max==-1)
   && (use_head || use_propfind_now))
   {
      // we'll have to receive next header, unset the status
      status.set(0);
      status_code=0;

      state=CONNECTED;
      SendArrayInfoRequest();
      state=RECEIVING_HEADER;
   }
   else
   {
      Disconnect();
      DontSleep();
   }
}

void Http::NewAuth(const char *hdr,HttpAuth::target_t target,const char *user,const char *pass)
{
   if(!user || !pass)
      return;

   // FIXME: keep a request queue, get the URI from the queue.
   const char *uri=GetFileURL(file,NO_USER);

   Ref<HttpAuth::Challenge> chal(new HttpAuth::Challenge(hdr));
   bool stale=chal->GetParam("stale").eq_nc("true");

   if(auth_sent[target]>(stale?1:0))
      return;

   HttpAuth::scheme_t new_scheme=chal->GetSchemeCode();
   if(new_scheme<=auth_scheme[target])
      return;
   if(HttpAuth::New(target,uri,chal.borrow(),user,pass))
      auth_scheme[target]=new_scheme;
}

void Http::HandleHeaderLine(const char *name,const char *value)
{
   // use a perfect hash
#define hh(L,C) ((L)+(C)*3)
#define hhc(S,C) hh(sizeof((S))-1,(C))
#define case_hh(S,C) case hhc((S),(C)): if(strcasecmp(name,(S))) break;
   switch(hh(strlen(name),c_toupper(name[0]))) {
   case_hh("Content-Length",'C') {
      long long bs=0;
      if(1!=sscanf(value,"%lld",&bs))
	 return;
      if(bs<0) // try to workaround broken servers
	 bs+=0x100000000LL;
      body_size=bs;
      if(mode==ARRAY_INFO && H_2XX(status_code)
      && xstrcmp(last_method,"PROPFIND"))
      {
	 FileInfo *fi=fileset_for_info->curr();
	 fi->SetSize(body_size);
	 TrySuccess();
      }
      return;
   }
   case_hh("Content-Range",'C') {
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
      if(!ModeIs(STORE) && !ModeIs(MAKE_DIR))
	 entity_size=fsize;
      if(opt_size && H_2XX(status_code))
	 *opt_size=fsize;
      return;
   }
   case_hh("Last-Modified",'L') {
      if(!H_2XX(status_code))
	 return;

      time_t t=Http::atotm(value);
      if(t==ATOTM_ERROR)
	 return;

      if(opt_date)
	 *opt_date=t;

      if(mode==ARRAY_INFO && !propfind)
      {
	 FileInfo *fi=fileset_for_info->curr();
	 fi->SetDate(t,0);
	 TrySuccess();
      }
      return;
   }
   case_hh("Location",'L')
      if(value[0]=='/' && value[1]=='/')
	 location.vset(GetProto(),":",value,NULL);
      else if(value[0]=='/')
	 location.vset(GetConnectURL(NO_PATH).get(),value,NULL);
      else
	 location.set(value);

      location_permanent = (status_code==H_Moved_Permanently
			 || status_code==H_Permanent_Redirect);
      location_mode=OpenMode();
      if(status_code==H_See_Other)
	 location_mode=RETRIEVE;

      if(location_mode!=QUOTE_CMD) {
	 ParsedURL url(location,true);
	 location_file.set(url.path);
      } else {
	 xstring &tmp=xstring::get_tmp(file,file.instr(' ')+1);
	 tmp.append(location+url::path_index(location));
	 if(special_data)
	    tmp.append(' ').append(special_data);
	 location_file.set(tmp);
      }
      return;

   case_hh("Retry-After",'R')
      retry_after=0;
      sscanf(value,"%ld",&retry_after);
      return;

   case_hh("Keep-Alive",'K') {
      keep_alive=true;
      const char *m=strstr(value,"max=");
      if(m) {
	 if(sscanf(m+4,"%d",&keep_alive_max)!=1)
	    keep_alive=false;
      } else
	 keep_alive_max=100;
      return;
   }
   case_hh("Proxy-Connection",'P')
      goto case_Connection;
   case_hh("Connection",'C')
   case_Connection:
      if(!strcasecmp(value,"keep-alive"))
	 keep_alive=true;
      else if(!strcasecmp(value,"close"))
	 keep_alive=false;
      return;

   case_hh("Transfer-Encoding",'T')
      if(!strcasecmp(value,"identity"))
	 return;
      if(!strcasecmp(value,"chunked"))
      {
	 chunked=true;
	 chunked_trailer=false;
	 chunk_size=CHUNK_SIZE_UNKNOWN;	  // expecting first chunk
	 chunk_pos=0;
      }
      return;

   case_hh("Content-Encoding",'C')
      content_encoding.set(value);
      return;

   case_hh("Accept-Ranges",'A')
      if(!strcasecmp(value,"none"))
	 no_ranges=true;
      if(strstr(value,"bytes"))
	 seen_ranges_bytes=true;
      return;

   case_hh("Set-Cookie",'S')
      if(!hftp && QueryBool("set-cookies",hostname))
	 SetCookie(value);
      return;

   case_hh("Content-Disposition",'C') {
      const char *filename=strstr(value,"filename=");
      if(!filename)
	 return;
      filename=HttpHeader::extract_quoted_value(filename+9);
      SetSuggestedFileName(filename);
      return;
   }
   case_hh("Content-Type",'C') {
      entity_content_type.set(value);
      const char *cs=strstr(value,"charset=");
      if(cs)
      {
	 cs=HttpHeader::extract_quoted_value(cs+8);
	 entity_charset.set(cs);
      }
      return;
   }
   case_hh("WWW-Authenticate",'W') {
      if(status_code!=H_Unauthorized)
	 return;
      if(user && pass)
	 NewAuth(value,HttpAuth::WWW,user,pass);
      else
	 NewAuth(value,HttpAuth::WWW,auth_user,auth_pass);
      return;
   }
   case_hh("Proxy-Authenticate",'P') {
      if(status_code!=H_Proxy_Authentication_Required)
	 return;
      NewAuth(value,HttpAuth::PROXY,proxy_user,proxy_pass);
      return;
   }
   case_hh("X-OC-MTime",'X') {
      if(!strcasecmp(value,"accepted"))
	 entity_date_set=true;
   }
   default:
      break;
   }
   LogNote(10,"unhandled header line `%s'",name);
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

      if(!o->conn || o->state==CONNECTING)
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
   const char *error;
   const char *buf;
   int len;

   // check if idle time exceeded
   if(mode==CLOSED && conn && idle_timer.Stopped())
   {
      LogNote(1,_("Closing idle connection"));
      Disconnect();
      return m;
   }

   if(home.path==0)
      set_home(default_cwd);

   if(Error())
      return m;

   if(propfind)
   {
      if(propfind->Error())
      {
	 propfind=0;
	 if(mode==CHANGE_DIR)
	 {
	    SetError(NO_FILE,propfind->ErrorText());
	    return MOVED;
	 }
	 if(propfind->ErrorFatal())
	    fileset_for_info->next();
	 Disconnect();
	 return MOVED;
      }
      if(propfind->Eof())
      {
	 LogNote(9,"got EOF on PROPFIND reply");
	 const char *b;
	 int len;
	 propfind->Get(&b,&len);
	 Ref<FileSet> fs(HttpListInfo::ParseProps(b,len,GetCwd()));
	 propfind=0;
	 if(fs)
	 {
	    if(mode==CHANGE_DIR)
	    {
	       fs->rewind();
	       FileInfo *fi=fs->curr();
	       if(fi && fi->Has(fi->TYPE))
	       {
		  LogNote(9,"new-cwd: %s",fi->GetLongName());
		  new_cwd->is_file=(fi->filetype!=fi->DIRECTORY);
		  if(new_cwd->url.last_char()=='/' && new_cwd->is_file)
		     new_cwd->url.rtrim('/');
		  else if(new_cwd->url.last_char()!='/' && !new_cwd->is_file)
		     new_cwd->url.append('/');
	       }
	    }
	    else if(mode==ARRAY_INFO)
	       fileset_for_info->Merge(fs);
	 }
	 m=MOVED;
	 state=DONE;
	 if(mode==CHANGE_DIR)
	 {
	    cwd.Set(new_cwd);
	    cache->SetDirectory(this, "", !cwd.is_file);
	    return m;
	 }
	 else if(mode==ARRAY_INFO)
	    ProceedArrayInfo();
      }
   }

   switch(state)
   {
   case DISCONNECTED:
      if(mode==CLOSED || !hostname)
	 return m;
      if(ModeIs(STORE) && pos>0 && entity_size>=0 && pos>=entity_size)
      {
	 state=DONE;
	 return MOVED;
      }
      if(mode==ARRAY_INFO)
      {
	 // check if we have anything to request
	 SendArrayInfoRequest();
	 if(!fileset_for_info->curr())
	 {
	    state=DONE;
	    return MOVED;
	 }
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
	 else if(file && !strncasecmp(file,"PROPFIND ",9))
	    special=HTTP_PROPFIND;
	 else
	 {
	    SetError(NOT_SUPP,0);
	    return MOVED;
	 }
	 if(special)
	 {
	    // METHOD encoded_path data
	    const char *scan=file;
	    while(*scan && *scan!=' ')
	       scan++;
	    while(*scan==' ')
	       scan++;
	    file_url.set(https?"https://":"http://");
	    AppendHostEncoded(file_url,hostname);
	    if(portname)
	    {
	       file_url.append(':');
	       file_url.append_url_encoded(portname,URL_PORT_UNSAFE);
	    }
	    if(*scan!='/' && cwd)
	    {
	       if(cwd[0]!='/')
		  file_url.append('/');
	       file_url.append_url_encoded(cwd,URL_PATH_UNSAFE);
	    }
	    if(*scan!='/' && file_url.last_char()!='/')
	       file_url.append('/');
	    file_url.append(scan);
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

      retry_after=0;

      res=SocketCreateTCP(peer[peer_curr].sa.sa_family);
      if(res==-1)
      {
	 saved_errno=errno;
	 if(peer_curr+1<peer.count())
	 {
	    peer_curr++;
	    retries--;
	    return MOVED;
	 }
	 if(NonFatalError(saved_errno))
	    return m;
	 SetError(SEE_ERRNO,xstring::format(
	    _("cannot create socket of address family %d"),
	    peer[peer_curr].sa.sa_family));
	 return MOVED;
      }
      conn=new Connection(res,hostname);

      SayConnectingTo();
      res=SocketConnect(conn->sock,&peer[peer_curr]);
      if(res==-1 && errno!=EINPROGRESS)
      {
	 saved_errno=errno;
	 NextPeer();
	 LogError(0,"connect: %s\n",strerror(saved_errno));
	 Disconnect();
	 if(NotSerious(saved_errno))
	    return MOVED;
	 goto system_error;
      }
      state=CONNECTING;
      m=MOVED;
      timeout_timer.Reset();

   case CONNECTING:
      res=Poll(conn->sock,POLLOUT,&error);
      if(res==-1)
      {
	 LogError(0,_("Socket error (%s) - reconnecting"),error);
	 Disconnect(error);
	 NextPeer();
	 return MOVED;
      }
      if(!(res&POLLOUT))
      {
	 if(CheckTimeout())
	 {
	    NextPeer();
	    return MOVED;
	 }
	 Block(conn->sock,POLLOUT);
	 return m;
      }

      m=MOVED;
      state=CONNECTED;
#if USE_SSL
      if(proxy?!strncmp(proxy,"https://",8):https)
      {
	 conn->MakeSSLBuffers();
      }
      else
#endif
      {
	 conn->MakeBuffers();
#if USE_SSL
	 if(proxy && https)
	 {
	    // have to setup a tunnel.
	    xstring ehost;
	    AppendHostEncoded(ehost,hostname);
	    const char *port_to_use=portname?portname.get():HTTPS_DEFAULT_PORT;
	    const char *eport=url::encode(port_to_use,URL_PORT_UNSAFE);
	    Send("CONNECT %s:%s HTTP/1.1\r\n",ehost.get(),eport);
	    SendProxyAuth();
	    Send("\r\n");
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
      if(conn->recv_buf->Eof())
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
      ExpandTildeInCWD();
      if(ModeIs(STORE) && pos>0 && entity_size>=0 && pos>=entity_size)
      {
	 state=DONE;
	 return MOVED;
      }
      if(mode==ARRAY_INFO)
      {
	 if(SendArrayInfoRequest()==0) {
	    // nothing to do
	    state=DONE;
	    return MOVED;
	 }
      }
      else
      {
	 LogNote(9,_("Sending request..."));
	 SendRequest();
      }

      state=RECEIVING_HEADER;
      m=MOVED;
      if(ModeIs(STORE))
	 rate_limit=new RateLimit(hostname);

   case RECEIVING_HEADER:
      if(conn->send_buf->Error() || conn->recv_buf->Error())
      {
	 if((ModeIs(STORE) || special) && status_code && !H_2XX(status_code))
	    goto pre_RECEIVING_BODY;   // assume error.
      handle_buf_error:
	 if(conn->send_buf->Error())
	 {
	    LogError(0,"send: %s",conn->send_buf->ErrorText());
	    if(conn->send_buf->ErrorFatal())
	       SetError(FATAL,conn->send_buf->ErrorText());
	 }
	 if(conn->recv_buf->Error())
	 {
	    LogError(0,"recv: %s",conn->recv_buf->ErrorText());
	    if(conn->recv_buf->ErrorFatal())
	       SetError(FATAL,conn->recv_buf->ErrorText());
	 }
	 Disconnect();
	 return MOVED;
      }
      timeout_timer.Reset(conn->send_buf->EventTime());
      timeout_timer.Reset(conn->recv_buf->EventTime());
      if(CheckTimeout())
	 return MOVED;
      conn->recv_buf->Get(&buf,&len);
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
	       conn->recv_buf->Skip(eol_size);
	       if(tunnel_state==TUNNEL_WAITING)
	       {
		  if(H_2XX(status_code))
		  {
#if USE_SSL
		     if(https)
			conn->MakeSSLBuffers();
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
		  if(propfind) {
		     // we avoid the DONE state since we have yet to handle propfind data
		     propfind->PutEOF();
		     state=CONNECTED;
		  } else
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
		  if(!xstrcmp(last_method,"PROPFIND"))
		  {
		     if(H_UNSUPPORTED(status_code))
		     {
			ResMgr::Set("http:use-propfind",hostname,"no");
			use_propfind_now=false;
			Disconnect();
			DontSleep();
			return MOVED;
		     }
		     goto pre_RECEIVING_BODY;
		  }
		  FileInfo *fi=fileset_for_info->curr();
		  if(H_REDIRECTED(status_code)) {
		     HandleRedirection();
		     if(location)
			fi->SetRedirect(location);
		  } else if(H_2XX(status_code) && !fi->Has(fi->TYPE)) {
		     fi->SetType(last_uri.last_char()=='/'?fi->DIRECTORY:fi->NORMAL);
		  }
		  ProceedArrayInfo();
		  return MOVED;
	       }
	       else if(ModeIs(STORE) || ModeIs(MAKE_DIR) || sending_proppatch)
	       {
		  if((sent_eot || pos==entity_size || sending_proppatch) && H_2XX(status_code))
		  {
		     state=DONE;
		     Disconnect();
		     state=DONE;
		     if(ModeIs(STORE) && entity_date!=NO_DATE && !entity_date_set
		     && use_propfind_now) {
			// send PROPPATCH in a separate request.
			sending_proppatch=true;
			state=DISCONNECTED;
		     }
		     return MOVED;
		  }
		  if(H_2XX(status_code))
		  {
		     // should never happen
		     LogError(0,"Unexpected success, the server did not accept full request body");
		     Disconnect();
		     return MOVED;
		  }
		  // going to pre_RECEIVING_BODY to catch error
	       }
	       goto pre_RECEIVING_BODY;
	    }
	    len=eol-buf;
	    line.nset(buf,len);

	    conn->recv_buf->Skip(len+eol_size);

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
		  status_code=H_Ok;
		  LogError(0,_("Could not parse HTTP status line"));
		  if(ModeIs(STORE))
		  {
		     state=DONE;
		     Disconnect();
		     state=DONE;
		     return MOVED;
		  }
		  conn->recv_buf->UnSkip(len+eol_size);
		  goto pre_RECEIVING_BODY;
	       }
	       proto_version=(ver_major<<4)+ver_minor;

	       // HTTP/1.1 does keep-alive by default
	       if(proto_version>=0x11)
		  keep_alive=true;

	       if(!H_2XX(status_code))
	       {
		  if(H_CONTINUE(status_code))
		     return MOVED;

		  if(H_5XX(status_code)) // server failed, try another
		     NextPeer();
		  if(status_code==H_Gateway_Timeout)
		  {
		     const char *cc=Query("cache-control");
		     if(cc && strstr(cc,"only-if-cached"))
		     {
			if(mode!=ARRAY_INFO)
			{
			   SetError(NO_FILE,_("Object is not cached and http:cache-control has only-if-cached"));
			   return MOVED;
			}
			status_code=H_Not_Acceptable; // so that no retry will be attempted
		     }
		  }
		  // check for retriable codes
		  if(H_TRANSIENT(status_code))
		  {
		     Disconnect();
		     return MOVED;
		  }
		  if(status_code==H_Too_Many_Requests)
		  {
		     Disconnect();
		     if(retry_after)
			reconnect_timer.StopDelayed(retry_after);
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
		  *colon=0; // terminate the header tag
		  const char *value=colon+1;
		  while(*value==' ')
		     value++;
		  HandleHeaderLine(line,value);
	       }
	    }
	 }
      }

      if(ModeIs(STORE) && (!status || H_CONTINUE(status_code)) && !sent_eot)
	 Block(conn->sock,POLLOUT);

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

      if((status_code==H_Unauthorized && auth_scheme[HttpAuth::WWW])
      || (status_code==H_Proxy_Authentication_Required && auth_scheme[HttpAuth::PROXY])) {
	 // retry with authentication
	 retries--;
	 state=RECEIVING_BODY;
	 LogErrorText();
	 Disconnect();
	 DontSleep();
	 return MOVED;
      }

      if(!H_2XX(status_code))
      {
	 xstring err;
	 int code=NO_FILE;

	 if(H_REDIRECTED(status_code))
	 {
	    HandleRedirection();
	    err.setf("%s (%s -> %s)",status+status_consumed,file.get(),
				    location?location.get():"nowhere");
	    code=FILE_MOVED;
	 }
	 else
	 {
	    const char *closure=file;
	    if(H_UNSUPPORTED(status_code) || status_code==H_Method_Not_Allowed)
	    {
	       if(H_UNSUPPORTED(status_code))
	       {
		  if(!xstrcmp(last_method,"PROPFIND"))
		     ResMgr::Set("http:use-propfind",hostname,"no");
		  if(!xstrcmp(last_method,"MKCOL"))
		     ResMgr::Set("http:use-mkcol",hostname,"no");
	       }
	       if(mode==CHANGE_DIR && !xstrcmp(last_method,"PROPFIND"))
	       {
		  use_propfind_now=false;
		  Disconnect();
		  DontSleep();
		  return MOVED;
	       }
	       code=NOT_SUPP;
	       closure=last_method;
	    }
	    if(closure && closure[0])
	       err.setf("%s (%s)",status+status_consumed,closure);
	    else
	       err.setf("%s (%s%s)",status+status_consumed,cwd.path.get(),
				       (last_char(cwd)=='/')?"":"/");
	 }
	 state=RECEIVING_BODY;
	 LogErrorText();
	 if(mode==ARRAY_INFO)
	 {
	    if(!H_TRANSIENT(status_code))
	       fileset_for_info->next();
	    Disconnect();
	    DontSleep();
	 }
	 else
	    SetError(code,err);
	 return MOVED;
      }
      if(!xstrcmp(last_method,"PROPFIND")
      && (mode==ARRAY_INFO || mode==CHANGE_DIR)) {
	 LogNote(9,"accepting XML for PROPFIND...");
	 propfind=new IOBufferFileAccess(this);
      }

      if(mode==CHANGE_DIR && !propfind)
      {
	 cwd.Set(new_cwd);
	 cache->SetDirectory(this, "", !cwd.is_file);
	 state=DONE;
	 return MOVED;
      }

      // Many servers send application/x-gzip with x-gzip encoding,
      // don't decode in such a case.
      if(CompressedContentEncoding() && !CompressedContentType()
      && QueryBool("decode",hostname)) {
	 // inflated size is unknown beforehand
	 entity_size=NO_SIZE;
	 if(opt_size)
	    *opt_size=NO_SIZE;
	 // start the inflation
	 inflate=new DirectedBuffer(DirectedBuffer::GET);
	 inflate->SetTranslator(new DataInflator());
      }
      // sometimes it's possible to derive entity size from body size.
      if(entity_size==NO_SIZE && body_size!=NO_SIZE
      && pos==0 && !ModeIs(STORE) && !ModeIs(MAKE_DIR) && !inflate) {
	 entity_size=body_size;
	 if(opt_size && H_2XX(status_code))
	    *opt_size=body_size;
      }

      LogNote(9,_("Receiving body..."));
      rate_limit=new RateLimit(hostname);
      if(real_pos<0) // assume Range: did not work
      {
	 if(!ModeIs(STORE) && !ModeIs(MAKE_DIR) && body_size>=0)
	 {
	    entity_size=body_size;
	    if(opt_size && H_2XX(status_code))
	       *opt_size=entity_size;
	 }
	 real_pos=0;
      }
      state=RECEIVING_BODY;
      m=MOVED;
      /*passthrough*/
   case RECEIVING_BODY:
      if(conn->recv_buf->Error() || conn->send_buf->Error())
	 goto handle_buf_error;
      if(conn->recv_buf->Size()>=rate_limit->BytesAllowedToGet())
      {
	 conn->recv_buf->Suspend();
	 TimeoutS(1);
      }
      else if(conn->recv_buf->Size()>=max_buf)
      {
	 conn->recv_buf->Suspend();
	 m=MOVED;
      }
      else
      {
	 if(conn->recv_buf->IsSuspended())
	 {
	    conn->recv_buf->Resume();
	    if(conn->recv_buf->Size()>0 || (conn->recv_buf->Size()==0 && conn->recv_buf->Eof()))
	       m=MOVED;
	 }
	 timeout_timer.Reset(conn->send_buf->EventTime());
	 timeout_timer.Reset(conn->recv_buf->EventTime());
	 if(conn->recv_buf->Size()==0)
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
   assert(saved_errno!=0);
   if(NonFatalError(saved_errno))
      return m;
   SetError(SEE_ERRNO,0);
   Disconnect();
   return MOVED;
}

void Http::HandleRedirection()
{
   bool is_url=(location && url::is_url(location));
   if(location && !is_url
   && mode==QUOTE_CMD && !strncasecmp(file,"POST ",5)
   && tunnel_state!=TUNNEL_WAITING)
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
   } else if(is_url && !hftp) {
      ParsedURL url(location);
      if(url.proto.eq(GetProto()) && !xstrcasecmp(url.host,hostname)
      && user && !url.user) {
	 // use the same user name after redirect to the same site.
	 url.user.set(user);
	 location.truncate();
	 url.CombineTo(location);
      }
   }
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
   super::SuspendInternal();
   if(conn)
      conn->SuspendInternal();
}
void Http::ResumeInternal()
{
   if(conn)
      conn->ResumeInternal();
   super::ResumeInternal();
}

int Http::Read(Buffer *buf,int size)
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
      Enter(this);
      res=_Read(buf,size);
      if(res>0)
      {
	 pos+=res;
	 if(rate_limit)
	    rate_limit->BytesGot(res);
	 TrySuccess();
      }
      Leave(this);
   }
   return res;
}
void Http::_Skip(int to_skip)
{
   if(inflate)
      inflate->Skip(to_skip);
   else
      conn->recv_buf->Skip(to_skip);
   _UpdatePos(to_skip);
}
void Http::_UpdatePos(int to_skip)
{
   if(!inflate) {
      if(chunked)
	 chunk_pos+=to_skip;
      bytes_received+=to_skip;
   }
   real_pos+=to_skip;
}
int Http::_Read(Buffer *buf,int size)
{
   const char *buf1;
   int size1;
   Buffer *src_buf=conn->recv_buf.get_non_const();
get_again:
   if(conn->recv_buf->Size()==0 && conn->recv_buf->Error())
   {
      LogError(0,"recv: %s",conn->recv_buf->ErrorText());
      if(conn->recv_buf->ErrorFatal())
	 SetError(FATAL,conn->recv_buf->ErrorText());
      Disconnect();
      return DO_AGAIN;
   }
   conn->recv_buf->Get(&buf1,&size1);
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
      if(body_size>=0 && bytes_received>=body_size
      && (!inflate || inflate->Size()==0))
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
   if(size1==0 && (!inflate || inflate->Size()==0))
      return DO_AGAIN;
   if(chunked && size1>0)
   {
      if(chunked_trailer && state==RECEIVING_HEADER)
	 return DO_AGAIN;
      const char *nl;
      if(chunk_size==CHUNK_SIZE_UNKNOWN) // expecting first/next chunk
      {
	 nl=(const char*)memchr(buf1,'\n',size1);
	 if(nl==0)  // not yet
	 {
	 not_yet:
	    if(conn->recv_buf->Eof())
	       Disconnect();	 // connection closed too early
	    return DO_AGAIN;
	 }
	 if(!is_ascii_xdigit(*buf1)
	 || sscanf(buf1,"%lx",&chunk_size)!=1)
	 {
	    Fatal(_("chunked format violated"));
	    return FATAL;
	 }
	 conn->recv_buf->Skip(nl-buf1+1);
	 chunk_pos=0;
	 LogNote(9,"next chunk size: %ld",chunk_size);
	 goto get_again;
      }
      if(chunk_size==0) // eof
      {
	 LogNote(9,_("Received last chunk"));
	 // headers may follow
	 chunked_trailer=true;
	 state=RECEIVING_HEADER;
	 body_size=bytes_received;
	 Timeout(0);
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
	 conn->recv_buf->Skip(2);
	 chunk_size=CHUNK_SIZE_UNKNOWN;
	 goto get_again;
      }
      // ok, now we may get portion of data
      if(size1>chunk_size-chunk_pos)
	 size1=chunk_size-chunk_pos;
   }

   if(!chunked)
   {
      // limit by body_size.
      if(body_size>=0 && size1+bytes_received>=body_size)
	 size1=body_size-bytes_received;
   }

   int bytes_allowed=0x10000000;
   if(rate_limit)
      bytes_allowed=rate_limit->BytesAllowedToGet();

   if(inflate) {
      // do the inflation, if there are not enough inflated data
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(inflate->Size()<size && size1>0) {
	 inflate->PutTranslated(buf1,size1);
	 conn->recv_buf->Skip(size1);
	 if(chunked)
	    chunk_pos+=size1;
	 bytes_received+=size1;
	 if(inflate->Error())
	    SetError(FATAL,inflate->ErrorText());
      }
      inflate->Get(&buf1,&size1);
      src_buf=inflate.get_non_const();
   } else {
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
   }
   if(size1==0)
      return DO_AGAIN;
   if(norest_manual && real_pos==0 && pos>0)
      return DO_AGAIN;
   if(real_pos<pos)
   {
      off_t to_skip=pos-real_pos;
      if(to_skip>size1)
	 to_skip=size1;
      _Skip(to_skip);
      goto get_again;
   }
   if(size>size1)
      size=size1;
   size=buf->MoveDataHere(src_buf,size);
   _UpdatePos(size);
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
   if(mode==CONNECT_VERIFY && (peer || conn))
      return OK;
   if((mode==REMOVE || mode==REMOVE_DIR || mode==RENAME)
   && state==RECEIVING_BODY)
      return OK;
   return IN_PROGRESS;
}

int Http::Buffered()
{
   if(!ModeIs(STORE) || !conn || !conn->send_buf)
      return 0;
   return conn->send_buf->Size()+SocketBuffered(conn->sock);
}

int Http::Write(const void *buf,int size)
{
   if(!ModeIs(STORE))
      return(0);

   Resume();
   Do();
   if(Error())
      return(error_code);

   if(state!=RECEIVING_HEADER || status!=0 || conn->send_buf->Size()!=0)
      return DO_AGAIN;

   {
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size+conn->send_buf->Size()>=max_buf)
      size=max_buf-conn->send_buf->Size();
   if(entity_size!=NO_SIZE && pos+size>entity_size)
   {
      size=entity_size-pos;
      // tried to write more than originally requested. Make it retry with Open:
      if(size==0)
	 return STORE_FAILED;
   }
   if(size<=0)
      return 0;

   conn->send_buf->Put((const char*)buf,size);

   if(retries>0 && conn->send_buf->GetPos()-conn->send_buf->Size()>Buffered()+0x1000)
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
   if(ModeIs(STORE))
   {
      if(state==RECEIVING_HEADER && conn->send_buf->Size()==0)
      {
	 if(entity_size==NO_SIZE || pos<entity_size)
	 {
	    shutdown(conn->sock,1);
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
      if(ModeIs(STORE) && !sent_eot && !status)
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

   if(conn)
      SetSocketBuffer(conn->sock);
   if(proxy && proxy_port==0)
      proxy_port.set(HTTP_DEFAULT_PROXY_PORT);

   user_agent=ResMgr::Query("http:user-agent",c);
   use_propfind_now=(use_propfind_now && QueryBool("use-propfind",c));
   no_ranges=(no_ranges || !QueryBool("use-range",hostname));

   if(QueryBool("use-allprop",c)) {
      allprop.set(   // PROPFIND request
	 "<?xml version=\"1.0\" ?>"
	 "<propfind xmlns=\"DAV:\">"
	   "<allprop/>"
	 "</propfind>\r\n");
   } else {
      allprop.unset();
   }

   if(!user || !pass) {
      // get auth info from http:authorization setting
      const char *auth_c=Query("authorization",hostname);
      if(auth_c && *auth_c) {
	 char *auth=alloca_strdup(auth_c);
	 char *colon=strchr(auth,':');
	 if(colon) {
	    *colon=0;
	    auth_user.set(auth);
	    auth_pass.set(colon+1);
	 }
      }
   }
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
   no_ranges=!QueryBool("use-range",hostname);
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
   Resource *scan=0;
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

   xstring closure(domain);
   if(path && path[0] && strcmp(path,"/"))
      closure.append(";path=").append(path);
   if(secure)
      closure.append(";secure");

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

void Http::Connection::MakeSSLBuffers()
{
   ssl=new lftp_ssl(sock,lftp_ssl::CLIENT,closure);
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

void Http::LogErrorText()
{
   if(!conn || !conn->recv_buf)
      return;
   conn->recv_buf->Roll();
   int size=conn->recv_buf->Size();
   if(size==0)
      return;
   Buffer tmpbuf;
   size=_Read(&tmpbuf,size);
   if(size<=0)
      return;
   tmpbuf.SpaceAdd(size);
   const char *buf0=tmpbuf.Get();
   char *buf=alloca_strdup(buf0);
   remove_tags(buf);
   for(char *line=strtok(buf,"\n"); line; line=strtok(0,"\n")) {
      rtrim(line);
      if(*line)
	 Log::global->Format(4,"<--* %s\n",line);
   }
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
      || (p[0] == 'U' && p[1] == 'T' && p[2] == 'C')
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

   Return the computed time_t representation, or ATOTM_ERROR if all the
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

   time_t ut=ATOTM_ERROR;

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

bool Http::IsCompressed(const char *s)
{
   static const char *const values[] = {
      "x-gzip", "gzip", "deflate", "compress", "x-compress", NULL
   };
   for(const char *const *v=values; *v; v++)
      if(!strcmp(s,*v))
	 return true;
   return false;
}

bool Http::CompressedContentEncoding() const
{
   return content_encoding && IsCompressed(content_encoding);
}
bool Http::CompressedContentType() const
{
   if(file.ends_with(".gz") || file.ends_with(".Z") || file.ends_with(".tgz"))
      return true;
   static const char app[]="application/";
   return entity_content_type && entity_content_type.begins_with(app)
      && IsCompressed(entity_content_type+sizeof(app)-1);
}

#include "modconfig.h"
#ifdef MODULE_PROTO_HTTP
void module_init()
{
   Http::ClassInit();
}
#endif
