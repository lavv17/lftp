/*
 * lftp and utils
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <time.h>
#include "Http.h"
#include "ResMgr.h"
#include "log.h"
#include "url.h"
#include "HttpDir.h"
#include "misc.h"

#include "ascii_ctype.h"

#ifndef HAVE_STRPTIME_DECL
CDECL char *strptime(const char *buf, const char *format, struct tm *tm);
#endif

#define super NetAccess

#define max_buf 0x10000

#define HTTP_DEFAULT_PORT	 "80"
#define HTTP_DEFAULT_PROXY_PORT	 "3128"

static time_t http_atotm (const char *time_string);
static int  base64_length (int len);
static void base64_encode (const char *s, char *store, int length);

/* Some status code validation macros: */
#define H_20X(x)        (((x) >= 200) && ((x) < 300))
#define H_PARTIAL(x)    ((x) == 206)
#define H_REDIRECTED(x) (((x) == 301) || ((x) == 302))


void Http::Init()
{
   state=DISCONNECTED;
   sock=-1;
   send_buf=0;
   recv_buf=0;
   body_size=-1;
   bytes_received=0;
   line=0;
   status=0;
   status_code=0;
   status_consumed=0;
   proto_version=0x10;
   location=0;
   sent_eot=false;
   last_method=0;

   default_cwd="/";

   keep_alive=false;
   keep_alive_max=-1;

   array_send=0;

   chunked=false;
   chunk_size=-1;
   chunk_pos=0;

   no_ranges=false;
   seen_ranges_bytes=false;

   no_cache_this=false;
   no_cache=false;

   hftp=false;
   use_head=true;

   user_agent=0;
}

Http::Http() : super()
{
   Init();
   Reconfig();
}
Http::Http(const Http *f) : super(f)
{
   Init();
   if(f->peer)
   {
      peer=(sockaddr_u*)xmemdup(f->peer,f->peer_num*sizeof(*peer));
      peer_num=f->peer_num;
      peer_curr=f->peer_curr;
      if(peer_curr>=peer_num)
	 peer_curr=0;
   }
   Reconfig();
}

Http::~Http()
{
   Delete(send_buf);
   Delete(recv_buf);
   if(sock!=-1)
      close(sock);
   xfree(line);
   xfree(status);
   xfree(location);
}

void Http::MoveConnectionHere(Http *o)
{
   send_buf=o->send_buf; o->send_buf=0;
   recv_buf=o->recv_buf; o->recv_buf=0;
   sock=o->sock; o->sock=-1;
   state=CONNECTED;
   o->Disconnect();
}

void Http::Disconnect()
{
   Delete(send_buf);
   send_buf=0;
   Delete(recv_buf);
   recv_buf=0;
   if(rate_limit)
   {
      delete rate_limit;
      rate_limit=0;
   }
   if(sock!=-1)
   {
      DebugPrint("---- ",_("Closing HTTP connection"),7);
      close(sock);
      sock=-1;
   }
   last_method=0;
   ResetRequestData();
   if(mode==STORE && state!=DONE && !Error())
      SetError(STORE_FAILED,0);
   state=DISCONNECTED;
}

void Http::ResetRequestData()
{
   body_size=-1;
   bytes_received=0;
   real_pos=-1;
   xfree(status);
   status=0;
   status_consumed=0;
   xfree(location);
   location=0;
   sent_eot=false;
   keep_alive=false;
   keep_alive_max=-1;
   array_send=array_ptr;
   chunked=false;
   chunk_size=-1;
   chunk_pos=0;
   seen_ranges_bytes=false;
}

void Http::Close()
{
   if(mode==CLOSED)
      return;
   if(sock!=-1 && keep_alive && (keep_alive_max>1 || keep_alive_max==-1)
   && mode!=STORE && (!xstrcmp(last_method,"HEAD")
		      || (body_size>=0 && bytes_received==body_size)))
   {
      // can reuse the connection.
      state=CONNECTED;
      ResetRequestData();
      idle_start=now;
      TimeoutS(idle);
   }
   else
   {
      Disconnect();
   }
   array_send=0;
   no_cache_this=false;
   no_ranges=false;
   super::Close();
}

void Http::Send(const char *format,...)
{
   static int max_send=256;
   va_list va;
   va_start(va,format);
   for(;;)
   {
      char *str=(char*)alloca(max_send);
      int res=vsnprintf(str,max_send,format,va);
      if(res>=0 && res<max_send)
      {
	 if(res<max_send/16)
	    max_send/=2;
	 DebugPrint("---> ",str,5);
	 send_buf->Put(str);
	 break;
      }
      max_send*=2;
   }
   va_end(va);
}

void Http::SendMethod(const char *method,const char *efile)
{
   char *ehost=string_alloca(strlen(hostname)*3+1);
   url::encode_string(hostname,ehost);
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
   Send("%s %s HTTP/1.1\r\n",method,efile);
   Send("Host: %s\r\n",ehost);
   if(user_agent && user_agent[0])
      Send("User-Agent: %s\r\n",user_agent);
   Send("Accept: */*\r\n");
}


void Http::SendBasicAuth(const char *tag,const char *user,const char *pass)
{
   /* Basic scheme */
   char *buf=(char*)alloca(strlen(user)+1+strlen(pass)+1);
   sprintf(buf,"%s:%s",user,pass);
   char *buf64=(char*)alloca(base64_length(strlen(buf))+1);
   base64_encode(buf,buf64,strlen(buf));
   Send("%s: Basic %s\r\n",tag,buf64);
}

void Http::SendAuth()
{
   if(proxy && proxy_user && proxy_pass)
      SendBasicAuth("Proxy-Authorization",proxy_user,proxy_pass);
   if(user && pass)
      SendBasicAuth("Authorization",user,pass);
}

bool Http::ModeSupported()
{
   switch((open_mode)mode)
   {
   case CLOSED:
   case QUOTE_CMD:
   case RENAME:
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
      return true;
   }
   abort(); // should not happen
}

void Http::SendRequest(const char *connection,const char *f)
{
   char *efile=string_alloca(strlen(f)*3+1);
   url::encode_string(f,efile,URL_PATH_UNSAFE);
   char *ecwd=string_alloca(strlen(cwd)*3+1);
   url::encode_string(cwd,ecwd,URL_PATH_UNSAFE);
   int efile_len;

   char *pfile=(char*)alloca(4+3+xstrlen(user)*6+3+xstrlen(pass)*3+1+
			      strlen(hostname)*3+1+strlen(cwd)*3+1+
			      strlen(efile)+1+6+1);

   if(proxy)
   {
      const char *proto="http";
      if(hftp)
      {
	 if(user && pass)
	 {
	    strcpy(pfile,"ftp://");
	    url::encode_string(user,pfile+strlen(pfile),URL_USER_UNSAFE);
	    strcat(pfile,"@");
	    url::encode_string(hostname,pfile+strlen(pfile),URL_HOST_UNSAFE);
	    goto add_path;
	 }
	 proto="ftp";
      }
      sprintf(pfile,"%s://",proto);
      url::encode_string(hostname,pfile+strlen(pfile),URL_HOST_UNSAFE);
      if(portname)
      {
	 strcat(pfile,":");
	 url::encode_string(portname,pfile+strlen(pfile),URL_PORT_UNSAFE);
      }
   }
   else
   {
      pfile[0]=0;
   }

add_path:

   char *path_base=pfile+strlen(pfile);

   if(efile[0]=='/')
      strcpy(path_base,efile);
   else if(efile[0]=='~')
      sprintf(path_base,"/%s",efile);
   else if(cwd[0]==0 || ((cwd[0]=='/' || (!hftp && cwd[0]=='~')) && cwd[1]==0))
      sprintf(path_base,"/%s",efile);
   else if(cwd[0]=='~')
      sprintf(path_base,"/%s/%s",ecwd,efile);
   else
      sprintf(path_base,"%s/%s",ecwd,efile);

   if(path_base[1]=='~' && path_base[2]=='/')
      memmove(path_base,path_base+2,strlen(path_base+2)+1);
   else if(hftp && path_base[1]!='~')
   {
      // root directory in ftp urls needs special encoding. (/%2Fpath)
      memmove(path_base+4,path_base+1,strlen(path_base+1)+1);
      memcpy(path_base+1,"%2F",3);
   }

   if(hftp && mode!=LONG_LIST && mode!=CHANGE_DIR && mode!=MAKE_DIR)
   {
      if(ascii)
	 strcat(pfile,";type=a");
      else
	 strcat(pfile,";type=i");
   }

   efile=pfile;
   efile_len=strlen(efile);

   if(pos==0)
      real_pos=0;
   if(mode==STORE)    // can't seek before writing
      real_pos=pos;

   switch((open_mode)mode)
   {
   case CLOSED:
   case CONNECT_VERIFY:
   case QUOTE_CMD:
   case RENAME:
   case LIST:
   case CHANGE_MODE:
      abort(); // unsupported

   case RETRIEVE:
   retrieve:
      SendMethod("GET",efile);
      if(pos>0 && !no_ranges)
	 Send("Range: bytes=%ld-\r\n",pos);
      break;

   case STORE:
      if(hftp || strcasecmp(Query("put-method",hostname),"POST"))
	 SendMethod("PUT",efile);
      else
	 SendMethod("POST",efile);
      if(entity_size>=0)
	 Send("Content-length: %ld\r\n",entity_size-pos);
      if(pos>0 && entity_size<0)
	 Send("Range: bytes=%ld-\r\n",pos);
      else if(pos>0)
	 Send("Range: bytes=%ld-%ld/%ld\r\n",pos,entity_size-1,entity_size);
      if(entity_date!=(time_t)-1)
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
   case MAKE_DIR:
      if(efile_len<1 || efile[efile_len-1]!='/')
	 strcat(efile,"/");
      if(mode==CHANGE_DIR)
	 SendMethod("HEAD",efile);
      else if(mode==LONG_LIST)
	 goto retrieve;
      else if(mode==MAKE_DIR)
	 SendMethod("PUT",efile);   // hope it would work
      break;

   case(REMOVE):
   case(REMOVE_DIR):
      SendMethod("DELETE",efile);
      break;

   case ARRAY_INFO:
      SendMethod("HEAD",efile);
      break;
   }
   SendAuth();
   if(no_cache || no_cache_this)
   {
      Send("Pragma: no-cache\r\n"); // for HTTP/1.0 compatibility
      Send("Cache-Control: no-cache\r\n");
   }
   if(!hftp)
   {
      const char *cookie=Query("cookie",hostname);
      if(cookie && cookie[0])
	 Send("Cookie: %s\r\n",cookie);
      if(mode==STORE)
      {
	 const char *content_type=Query("put-content-type",hostname);
	 if(content_type && content_type[0])
	    Send("Content-Type: %s\r\n",content_type);
      }
   }
   if(mode==ARRAY_INFO && !use_head)
      connection="close";
   else if(mode!=STORE)
      connection="keep-alive";
   if(mode!=ARRAY_INFO || connection)
      Send("Connection: %s\r\n",connection?connection:"close");
   Send("\r\n");

   keep_alive=false;
   chunked=false;
   chunk_size=-1;
   chunk_pos=0;
   no_ranges=false;
}

void Http::SendArrayInfoRequest()
{
   int m=1;
   if(keep_alive)
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
   keep_alive=false;
}

void Http::HandleHeaderLine(const char *name,const char *value)
{
   if(!strcasecmp(name,"Content-length"))
   {
      sscanf(value,"%ld",&body_size);
      if(pos==0 && mode!=STORE)
	 entity_size=body_size;
      if(pos==0 && opt_size)
	 *opt_size=body_size;

      if(mode==ARRAY_INFO && H_20X(status_code))
      {
	 array_for_info[array_ptr].size=body_size;
	 array_for_info[array_ptr].get_size=false;
	 retries=0;
      }
      return;
   }
   if(!strcasecmp(name,"Content-range"))
   {
      long first,last,fsize;
      if(sscanf(value,"%*s %ld-%ld/%ld",&first,&last,&fsize)!=3)
	 return;
      real_pos=first;
      body_size=last-first+1;
      if(mode!=STORE)
	 entity_size=fsize;
      if(opt_size)
	 *opt_size=fsize;
      return;
   }
   if(!strcasecmp(name,"Last-Modified"))
   {
      time_t t=http_atotm(value);
      if(opt_date)
	 *opt_date=t;

      if(mode==ARRAY_INFO && H_20X(status_code))
      {
	 array_for_info[array_ptr].time=t;
	 array_for_info[array_ptr].get_time=false;
	 retries=0;
      }
      return;
   }
   if(!strcasecmp(name,"Location"))
   {
      xfree(location);
      location=xstrdup(value);
      return;
   }
   if(!strcasecmp(name,"Keep-Alive"))
   {
      keep_alive=true;
      const char *m=strstr(value,"max=");
      if(m)
	 sscanf(m+4,"%d",&keep_alive_max);
      else
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
      if(!strcasecmp(value,"chunked"))
      {
	 chunked=true;
	 chunk_size=-1;	// to indicate "before first chunk"
	 chunk_pos=0;
      }
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
}

static const char *find_eol(const char *str,int len)
{
   const char *p=str;
   for(int i=0; i<len-1; i++,p++)
   {
      if(p[1]=='\n' && p[0]=='\r')
	 return p;
      if(p[1]!='\r')
	 p++,i++; // fast skip
   }
   return 0;
}

void Http::GetBetterConnection(int level,int count)
{
   if(level==0)
      return;
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Http *o=(Http*)fo; // we are sure it is Http.

      if(o->sock==-1 || o->state==CONNECTING)
	 continue;

      if(o->state!=CONNECTED || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || o->priority>=priority)
	    continue;
	 o->Disconnect();
	 return;
      }
      else
      {
	 takeover_time=now;
      }

      // connected session (o) must have resolved address
      if(!peer)
      {
	 // copy resolved address so that it would be possible to create
	 // data connection.
	 xfree(peer);
	 peer=(sockaddr_u*)xmemdup(o->peer,o->peer_num*sizeof(*o->peer));
	 peer_num=o->peer_num;
	 peer_curr=o->peer_curr;
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
   Buffer *data_buf;
   int count;

   // check if idle time exceeded
   if(mode==CLOSED && sock!=-1 && idle>0)
   {
      if(now-idle_start>=idle)
      {
	 DebugPrint("---- ",_("Closing idle connection"),1);
	 Disconnect();
	 return m;
      }
      TimeoutS(idle_start+idle-now);
   }

   if(home==0)
      home=xstrdup(default_cwd);
   ExpandTildeInCWD();

   if(Error())
      return MOVED;

   switch(state)
   {
   case DISCONNECTED:
      if(mode==CLOSED || !hostname)
	 return m;
      if(!ModeSupported())
      {
	 SetError(NOT_SUPP);
	 return MOVED;
      }
      if(hftp)
      {
	 if(!proxy)
	 {
	    // problem here: hftp cannot work without proxy
	    SetError(FATAL,"ftp over http cannot work without proxy, set hftp:proxy.");
	    return MOVED;
	 }
      }

      // walk through Http classes and try to find identical idle session
      // first try "easy" cases of session take-over.
      count=CountConnections();
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>count))
	    break;
	 GetBetterConnection(i,count);
	 if(state!=DISCONNECTED)
	    return MOVED;
      }

      if(!ReconnectAllowed())
	 return m;

      if(Resolve(HTTP_DEFAULT_PORT,"http","tcp")==MOVED)
	 m=MOVED;
      if(!peer)
	 return m;

      if(mode==CONNECT_VERIFY)
	 return m;

      if(!NextTry())
      	 return MOVED;

      sock=socket(peer[peer_curr].sa.sa_family,SOCK_STREAM,IPPROTO_TCP);
      if(sock==-1)
      {
	 if(peer_curr+1<peer_num)
	 {
	    peer_curr++;
	    retries--;
	    return MOVED;
	 }
	 if(errno==ENFILE || errno==EMFILE)
	 {
	    // file table overflow - it could free sometime
	    TimeoutS(1);
	    return m;
	 }
	 char str[256];
	 sprintf(str,"cannot create socket of address family %d",
			peer[peer_curr].sa.sa_family);
	 SetError(SEE_ERRNO,str);
	 return MOVED;
      }
      KeepAlive(sock);
      SetSocketBuffer(sock,socket_buffer);
      SetSocketMaxseg(sock,socket_maxseg);
      NonBlock(sock);
      CloseOnExec(sock);

      SayConnectingTo();
      res=SocketConnect(sock,&peer[peer_curr]);
      if(res==-1
#ifdef EINPROGRESS
      && errno!=EINPROGRESS
#endif
      )
      {
	 NextPeer();
	 Log::global->Format(0,"connect: %s",strerror(errno));
	 Disconnect();
	 if(NotSerious(errno))
	    return MOVED;
	 goto system_error;
      }
      state=CONNECTING;
      m=MOVED;
      event_time=now;

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
      send_buf=new FileOutputBuffer(new FDStream(sock,"<output-socket>"));
      recv_buf=new FileInputBuffer(new FDStream(sock,"<input-socket>"));
      /*fallthrough*/
   case CONNECTED:
      if(recv_buf->Eof())
      {
	 DebugPrint("**** ",_("Peer closed connection"),0);
	 Disconnect();
	 return MOVED;
      }
      if(mode==CLOSED)
	 return m;
      DebugPrint("---- ","Sending request...");
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
      {
	 assert(rate_limit==0);
	 rate_limit=new RateLimit();
      }

   case RECEIVING_HEADER:
      if(send_buf->Error() || recv_buf->Error())
      {
	 if(mode==STORE && status_code && !H_20X(status_code))
	    goto pre_RECEIVING_BODY;   // assume error.
	 if(send_buf->Error())
	    DebugPrint("**** ",send_buf->ErrorText(),0);
	 if(recv_buf->Error())
	    DebugPrint("**** ",recv_buf->ErrorText(),0);
	 Disconnect();
	 return MOVED;
      }
      BumpEventTime(send_buf->EventTime());
      BumpEventTime(recv_buf->EventTime());
      if(CheckTimeout())
	 return MOVED;
      recv_buf->Get(&buf,&len);
      if(!buf)
      {
	 // eof
	 DebugPrint("**** ","Hit EOF while fetching headers",0);
	 Disconnect();
	 return MOVED;
      }
      if(len>0)
      {
	 const char *eol=find_eol(buf,len);
	 if(eol)
	 {
	    if(eol==buf)
	    {
	       DebugPrint("<--- ","",4);
	       recv_buf->Skip(2);
	       if(mode==ARRAY_INFO)
	       {
		  // we'll have to receive next header
		  xfree(status);
		  status=0;
		  status_code=0;
		  if(array_for_info[array_ptr].get_time)
		     array_for_info[array_ptr].time=(time_t)-1;
		  if(array_for_info[array_ptr].get_size)
		     array_for_info[array_ptr].size=-1;
		  if(++array_ptr>=array_cnt)
		  {
		     state=DONE;
		     return MOVED;
		  }
		  // if protocol is HTTP/1.1, we can avoid reconnection
		  if(proto_version>=0x11 && keep_alive)
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
	       else if(mode==STORE)
	       {
		  if(sent_eot && H_20X(status_code))
		  {
		     state=DONE;
		     Disconnect();
		     state=DONE;
		     return MOVED;
		  }
		  if(!sent_eot && H_20X(status_code))
		  {
		     // should never happen
		     DebugPrint("**** ","Success, but did nothing??",0);
		     Disconnect();
		     return MOVED;
		  }
		  // going to pre_RECEIVING_BODY to catch error
	       }
	       goto pre_RECEIVING_BODY;
	    }
	    len=eol-buf;
	    xfree(line);
	    line=(char*)xmalloc(len+1);
	    memcpy(line,buf,len);
	    line[len]=0;

	    recv_buf->Skip(len+2);

	    DebugPrint("<--- ",line,4);
	    m=MOVED;

	    if(status==0)
	    {
	       // it's status line
	       status=line;
	       line=0;
	       int ver_major,ver_minor;
	       if(3!=sscanf(status,"HTTP/%d.%d %n%d",&ver_major,&ver_minor,
		     &status_consumed,&status_code))
	       {
		  // simple 0.9 ?
		  ver_major=0;
		  ver_minor=9;
		  status_code=200;
		  if(1!=sscanf(status,"HTTP %n%d",&status_consumed,&status_code))
		  {
		     DebugPrint("**** ","Could not parse HTTP status line",0);
		     //FIXME: STORE
		     goto pre_RECEIVING_BODY;
		  }
	       }
	       proto_version=(ver_major<<4)+ver_minor;
	       if(!H_20X(status_code))
	       {
		  if(status_code/100==5) // server failed, try another
		     NextPeer();
		  // check for retriable codes
		  if(status_code==408 // Request Timeout
		  || status_code==502 // Bad Gateway
		  || status_code==503 // Service Unavailable
		  || status_code==504)// Gateway Timeout
		  {
		     Disconnect();
		     return MOVED;
		  }

		  if(mode==ARRAY_INFO)
		  {
		     retries=0;
		     return MOVED;
		  }

		  return MOVED;
	       }
	    }
	    else
	    {
	       // header line.
	       char *colon=strchr(line,':');
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

      if(mode==STORE && !status && !sent_eot)
	 Block(sock,POLLOUT);

      return m;

   pre_RECEIVING_BODY:

      if(H_REDIRECTED(status_code))
      {
	 // check if it is redirection to the same server
	 // or to directory instead of file.
	 // FIXME.
      }

      if(!H_20X(status_code))
      {
	 char *err=(char*)alloca(strlen(status)+strlen(file)+strlen(cwd)+xstrlen(location)+20);

	 if(H_REDIRECTED(status_code))
	    sprintf(err,"%s (%s -> %s)",status+status_consumed,file,
				    location?location:"nowhere");
	 else
	 {
	    if(file && file[0])
	       sprintf(err,"%s (%s)",status+status_consumed,file);
	    else
	       sprintf(err,"%s (%s%s)",status+status_consumed,cwd,
		  (cwd[0] && cwd[strlen(cwd)-1]=='/')?"":"/");
	 }
	 Disconnect();
	 SetError(NO_FILE,err);
	 return MOVED;
      }
      if(mode==CHANGE_DIR)
      {
	 xfree(cwd);
	 cwd=xstrdup(file);
	 state=DONE;
	 return MOVED;
      }

      DebugPrint("---- ","Receiving body...");
      assert(rate_limit==0);
      rate_limit=new RateLimit();
      if(real_pos<0) // assume Range: did not work
      {
	 if(mode!=STORE && body_size>=0)
	 {
	    entity_size=body_size;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
	 real_pos=0;
      }
      state=RECEIVING_BODY;
      m=MOVED;
   case RECEIVING_BODY:
      data_buf=recv_buf;
      if(recv_buf->Error() || send_buf->Error())
      {
	 if(send_buf->Error())
	    DebugPrint("**** ",send_buf->ErrorText(),0);
	 if(recv_buf->Error())
	    DebugPrint("**** ",recv_buf->ErrorText(),0);
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Size()>=rate_limit->BytesAllowed())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(data_buf->Size()>=max_buf)
      {
	 recv_buf->Suspend();
	 m=MOVED;
      }
      else
      {
	 recv_buf->Resume();
	 BumpEventTime(send_buf->EventTime());
	 BumpEventTime(recv_buf->EventTime());
	 if(data_buf->Size()>0 || (data_buf->Size()==0 && recv_buf->Eof()))
	    m=MOVED;
	 else
	 {
	    // check if ranges were emulated by squid
	    bool no_ranges_if_timeout=(bytes_received==0 && !seen_ranges_bytes);
	    if(CheckTimeout())
	    {
	       no_ranges|=no_ranges_if_timeout;
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
   if(errno==ENFILE || errno==EMFILE)
   {
      // file table overflow - it could free sometime
      Timeout(1000);
      return m;
   }
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
}

int Http::Read(void *buf,int size)
{
   if(Error())
      return error_code;
   if(mode==CLOSED)
      return 0;
   if(state==DONE)
      return 0;	  // eof
   if(state==RECEIVING_BODY && real_pos>=0)
   {
      const char *buf1;
      int size1;
   get_again:
      if(recv_buf->Size()==0 && recv_buf->Error())
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      recv_buf->Get(&buf1,&size1);
      if(buf1==0) // eof
      {
	 DebugPrint("---- ","Hit EOF");
	 if(bytes_received<body_size || chunked)
	 {
	    DebugPrint("**** ","Received not enough data, retrying",0);
	    Disconnect();
	    return DO_AGAIN;
	 }
	 return 0;
      }
      if(body_size>=0 && bytes_received>=body_size)
      {
	 DebugPrint("---- ","Received all");
	 return 0; // all received
      }
      if(entity_size>=0 && pos>=entity_size)
      {
	 DebugPrint("---- ","Received all (total)");
	 return 0;
      }
      if(size1==0)
	 return DO_AGAIN;
      if(chunked)
      {
	 if(chunk_size==-1) // expecting first/next chunk
	 {
	    const char *nl=(const char*)memchr(buf1,'\n',size1);
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
	    // FIXME: headers may follow
	    // to avoid messing with headers, we close connection.
	    Disconnect();
	    state=DONE;
	    return 0;
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

      int bytes_allowed=rate_limit->BytesAllowed();
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(size1==0)
	 return DO_AGAIN;
      if(norest_manual && real_pos==0 && pos>0)
	 return DO_AGAIN;
      if(real_pos<pos)
      {
	 long to_skip=pos-real_pos;
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
      pos+=size;
      real_pos+=size;
      bytes_received+=size;
      if(chunked)
	 chunk_pos+=size;
      rate_limit->BytesUsed(size);
      retries=0;
      return size;
   }
   return DO_AGAIN;
}

int Http::Done()
{
   if(mode==CLOSED)
      return OK;
   if(Error())
      return error_code;
   if(state==DONE)
      return OK;
   if(mode==CONNECT_VERIFY && peer)
      return OK;
   return IN_PROGRESS;
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
      int allowed=rate_limit->BytesAllowed();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size==0)
      return 0;
   int res=write(sock,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return DO_AGAIN;
      if(NotSerious(errno) || errno==EPIPE)
      {
	 DebugPrint("**** ",strerror(errno),0);
	 Disconnect();
	 return STORE_FAILED;
      }
      SetError(SEE_ERRNO,0);
      Disconnect();
      return error_code;
   }
   retries=0;
   rate_limit->BytesUsed(res);
   pos+=res;
   real_pos+=res;
   return(res);
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
	 shutdown(sock,1);
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

   no_cache = !(bool)Query("cache",c);
   if(!hftp && NoProxy())
      SetProxy(0);
   else
      SetProxy(Query("proxy",c));

   if(sock!=-1)
      SetSocketBuffer(sock,socket_buffer);
   if(proxy && proxy_port==0)
      proxy_port=xstrdup(HTTP_DEFAULT_PROXY_PORT);

   user_agent=ResMgr::Query("http:user-agent",c);
}

bool Http::SameSiteAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   Http *o=(Http*)fa;
   return(!xstrcmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool Http::SameLocationAs(FileAccess *fa)
{
   if(!SameSiteAs(fa))
      return false;
   Http *o=(Http*)fa;
   if(xstrcmp(cwd,o->cwd))
      return false;
   return true;
}

void Http::Connect(const char *new_host,const char *new_port)
{
   super::Connect(new_host,new_port);
   Reconfig();
   state=DISCONNECTED;
}

DirList *Http::MakeDirList(ArgV *args)
{
   return new HttpDirList(args,this);
}
Glob *Http::MakeGlob(const char *pattern)
{
   return new HttpGlob(this,pattern);
}
ListInfo *Http::MakeListInfo()
{
   return new HttpListInfo(this);
}


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
      xfree(home);
      home=xstrdup("~");
      xfree(cwd);
      cwd=xstrdup(home);
   }
}
void HFtp::Reconfig(const char *name)
{
   super::Reconfig(name);
   use_head=Query("use-head");
}

/* Converts struct tm to time_t, assuming the data in tm is UTC rather
   than local timezone (mktime assumes the latter).

   Contributed by Roger Beeman <beeman@cisco.com>, with the help of
   Mark Baushke <mdb@cisco.com> and the rest of the Gurus at CISCO.  */
static time_t
mktime_from_utc (struct tm *t)
{
  time_t tl, tb;

  tl = mktime (t);
  if (tl == -1)
    return -1;
  tb = mktime (gmtime (&tl));
  return (tl <= tb ? (tl + (tl - tb)) : (tl - (tb - tl)));
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
static time_t
http_atotm (const char *time_string)
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

  /* RFC1123: Thu, 29 Jan 1998 22:12:57 */
  if (check_end (strptime (time_string, "%a, %d %b %Y %T", &t)))
    return mktime_from_utc (&t);
  /* RFC850:  Thu, 29-Jan-98 22:12:57 */
  if (check_end (strptime (time_string, "%a, %d-%b-%y %T", &t)))
    return mktime_from_utc (&t);
  /* asctime: Thu Jan 29 22:12:57 1998 */
  if (check_end (strptime (time_string, "%a %b %d %T %Y", &t)))
    return mktime_from_utc (&t);
  /* Failure.  */
  return -1;
}

/* How many bytes it will take to store LEN bytes in base64.  */
static int
base64_length(int len)
{
  return (4 * (((len) + 2) / 3));
}

/* Encode the string S of length LENGTH to base64 format and place it
   to STORE.  STORE will be 0-terminated, and must point to a writable
   buffer of at least 1+BASE64_LENGTH(length) bytes.  */
static void
base64_encode (const char *s, char *store, int length)
{
  /* Conversion table.  */
  static char tbl[64] = {
    'A','B','C','D','E','F','G','H',
    'I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X',
    'Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n',
    'o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3',
    '4','5','6','7','8','9','+','/'
  };
  int i;
  unsigned char *p = (unsigned char *)store;

  /* Transform the 3x8 bits to 4x6 bits, as required by base64.  */
  for (i = 0; i < length; i += 3)
    {
      *p++ = tbl[s[0] >> 2];
      *p++ = tbl[((s[0] & 3) << 4) + (s[1] >> 4)];
      *p++ = tbl[((s[1] & 0xf) << 2) + (s[2] >> 6)];
      *p++ = tbl[s[2] & 0x3f];
      s += 3;
    }
  /* Pad the result if necessary...  */
  if (i == length + 1)
    *(p - 1) = '=';
  else if (i == length + 2)
    *(p - 1) = *(p - 2) = '=';
  /* ...and zero-terminate it.  */
  *p = '\0';
}



#ifdef MODULE
CDECL void module_init()
{
   Http::ClassInit();
}
#endif
