/*
 * lftp and utils
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <ctype.h>
#include <time.h>
#include "Http.h"
#include "ResMgr.h"
#include "log.h"
#include "url.h"

#define super FileAccess

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
   resolver=0;
   peer=0;
   peer_num=peer_curr=0;
   sock=-1;
   send_buf=0;
   recv_buf=0;
   body_size=-1;
   line=0;
   status=0;
   status_code=0;
   status_consumed=0;
   proto_version=0x10;
   location=0;

   idle=0;
   idle_start=now;
   retries=0;

   socket_buffer=0;
   socket_maxseg=0;
   max_retries=0;

   proxy=0;
   proxy_port=0;
   proxy_user=proxy_pass=0;

   default_cwd="/";
}

Http::Http() : super()
{
   Init();
   xfree(home);
   home=xstrdup("/");
   xfree(cwd);
   cwd=xstrdup(default_cwd);
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
   if(resolver)
      delete resolver;
   xfree(peer);
   if(send_buf)
      delete send_buf;
   if(recv_buf)
      delete recv_buf;
   if(sock!=-1)
      close(sock);
   xfree(line);
   xfree(status);
   xfree(location);

   xfree(proxy); proxy=0;
   xfree(proxy_port); proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;
}

bool Http::CheckTimeout()
{
   if(now-event_time>=timeout)
   {
      DebugPrint("**** ",_("Timeout - reconnecting"));
      Disconnect();
      event_time=now;
      return(true);
   }
   block+=TimeOut((timeout-(now-event_time))*1000);
   return(false);
}

void Http::SetError(int ec,const char *e)
{
   xfree(last_error_resp);
   last_error_resp=xstrdup(e);
   state=ERROR;
   error_code=ec;
}

void Http::Fatal(const char *e)
{
   SetError(FATAL,e);
}

void Http::Disconnect()
{
   if(send_buf)
   {
      delete send_buf;
      send_buf=0;
   }
   if(recv_buf)
   {
      delete recv_buf;
      recv_buf=0;
   }
   if(sock!=-1)
   {
      close(sock);
      sock=-1;
   }
   state=DISCONNECTED;
   body_size=-1;
   bytes_received=0;
   real_pos=-1;
   xfree(status);
   status=0;
   status_consumed=0;
   xfree(location);
   location=0;
}

void Http::Close()
{
   retries=0;
   Disconnect();
   super::Close();
}

void Http::Send(const char *format,...)
{
   char *str=(char*)alloca(max_send);
   va_list va;
   va_start(va,format);

   vsprintf(str,format,va);

   va_end(va);

   DebugPrint("---> ",str,3);
   send_buf->Put(str);
}

void Http::SendMethod(const char *method,const char *efile)
{
   Send("%s %s HTTP/1.1\r\n",method,efile);
   Send("Host: %s\r\n",url::encode_string(hostname));
}


void Http::SendAuth()
{
   if(!user || !pass)
      return;
   /* Basic scheme */
   char *buf=(char*)alloca(strlen(user)+1+strlen(pass)+1);
   sprintf(buf,"%s:%s",user,pass);
   char *buf64=(char*)alloca(base64_length(strlen(buf))+1);
   base64_encode(buf,buf64,strlen(buf));
   Send("Authorization: Basic %s\r\n",buf64);
}

bool Http::ModeSupported()
{
   switch((open_mode)mode)
   {
   case CLOSED:
   case CONNECT_VERIFY:
   case QUOTE_CMD:
   case RENAME:
   case LIST:
      return false;
   default:
      return true;
   }
}

void Http::SendRequest(const char *connection)
{
   char *efile=alloca_strdup(url::encode_string(file));
   char *ecwd=alloca_strdup(url::encode_string(cwd));
   int efile_len;

   char *pfile=(char*)alloca(4+3+strlen(hostname)*3+1
			      +strlen(cwd)*3+1+strlen(efile)+1+1);

   if(proxy)
   {
      const char *proto="http";
      sprintf(pfile,"%s://%s",proto,url::encode_string(hostname));
   }
   else
   {
      pfile[0]=0;
   }

   if(ecwd[0]=='~' && ecwd[1]=='/')
      ecwd+=1;

   if(efile[0]=='/')
      strcat(pfile,efile);
   else if(efile[0]=='~')
      sprintf(pfile+strlen(pfile),"/%s",efile);
   else if(cwd[0]==0 || ((cwd[0]=='/' || cwd[0]=='~') && cwd[1]==0))
      sprintf(pfile+strlen(pfile),"/%s",efile);
   else if(cwd[0]=='~')
      sprintf(pfile+strlen(pfile),"/%s/%s",ecwd,efile);
   else
      sprintf(pfile+strlen(pfile),"%s/%s",ecwd,efile);

   efile=pfile;
   efile_len=strlen(efile);

   max_send=efile_len+40;

   if(pos==0)
      real_pos=0;

   switch((open_mode)mode)
   {
   case CLOSED:
   case CONNECT_VERIFY:
   case QUOTE_CMD:
   case RENAME:
   case LIST:
      abort(); // unsupported

   case RETRIEVE:
      SendMethod("GET",efile);
      if(pos>0)
	 Send("Range: bytes=%ld-\r\n",pos);
      break;

   case STORE:
      SendMethod("PUT",efile);
      if(entity_size>=0)
	 Send("Content-length: %ld\r\n",entity_size-pos);
      if(pos>0 && entity_size<0)
	 Send("Range: bytes=%ld-\r\n",pos);
      else if(pos>0)
	 Send("Range: bytes=%ld-%ld/%ld\r\n",pos,entity_size-1,entity_size);
      break;

   case CHANGE_DIR:
   case LONG_LIST:
   case MAKE_DIR:
      if(efile[0]==0 || efile[efile_len-1]!='/')
	 strcat(efile,"/");
      if(mode==CHANGE_DIR)
	 SendMethod("HEAD",efile);
      else if(mode==LONG_LIST)
	 SendMethod("GET",efile);
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
   if(mode!=ARRAY_INFO || connection)
      Send("Connection: %s\r\n",connection?connection:"close");
   Send("\r\n");
}

void Http::HandleHeaderLine(const char *name,const char *value)
{
   if(!strcasecmp(name,"Content-length"))
   {
      sscanf(value,"%ld",&body_size);
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

int Http::Do()
{
   int m=STALL;
   int res;
   const char *buf;
   int len;
   Buffer *data_buf;

   // check if idle time exceeded
   if(mode==CLOSED && sock!=-1 && idle>0)
   {
      if(now-idle_start>=idle)
      {
	 DebugPrint("---- ",_("Closing idle connection"),2);
	 Disconnect();
	 return m;
      }
      Timeout((idle_start+idle-now)*1000);
   }

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
      if(peer==0 || relookup_always)
      {
	 if(resolver==0)
	 {
	    resolver=new Resolver(proxy?proxy:hostname,
			proxy?proxy_port:(portname?portname:HTTP_DEFAULT_PORT));
	    ClearPeer();
	 }
	 if(!resolver->Done())
	    return m;

	 if(resolver->Error())
	 {
	    SetError(LOOKUP_ERROR,resolver->ErrorMsg());
	    return(MOVED);
	 }

	 xfree(peer);
	 peer=(sockaddr_u*)xmalloc(resolver->GetResultSize());
	 peer_num=resolver->GetResultNum();
	 resolver->GetResult(peer);
	 peer_curr=0;

	 delete resolver;
	 resolver=0;
      }
      if(peer_curr>=peer_curr)
	 peer_curr=0;

      if(mode==CONNECT_VERIFY)
	 return m;

      if(try_time!=0 && now-try_time<sleep_time)
      {
	 block+=TimeOut(1000*(sleep_time-(now-try_time)));
	 return m;
      }
      try_time=now;

      if(max_retries>0 && retries>=max_retries)
      {
	 Fatal(_("max-retries exceeded"));
      	 return MOVED;
      }
      retries++;

      assert(peer!=0);
      assert(peer_curr<peer_num);

      sock=socket(peer[peer_curr].sa.sa_family,SOCK_STREAM,IPPROTO_TCP);
      if(sock==-1)
	 goto system_error;
      KeepAlive(sock);
      SetSocketBuffer(sock,socket_buffer);
      SetSocketMaxseg(sock,socket_maxseg);
      NonBlock(sock);
      CloseOnExec(sock);

#if 0
      a=(unsigned char*)&peer_sa.in.sin_addr;
      sprintf(str,_("Connecting to %s%s (%s) port %u"),proxy?"proxy ":"",
	 host_to_connect,numeric_address(&peer_sa),get_port(&peer_sa));
      DebugPrint("---- ",str,0);
#endif

      DebugPrint("---- ","Connecting...",9);
      res=connect(sock,&peer[peer_curr].sa,sizeof(*peer));
      UpdateNow(); // if non-blocking don't work

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
      send_buf=new FileOutputBuffer(new FDStream(sock,"<output-socket>"));
      recv_buf=new FileInputBuffer(new FDStream(sock,"<input-socket>"));

      DebugPrint("---- ","Sending request...",9);
      if(mode==ARRAY_INFO)
      {
	 xfree(file);
	 for(int i=array_ptr; i<array_cnt; i++)
	 {
	    file=array_for_info[i].file;
	    SendRequest(i==array_cnt-1 ? "close" : 0);
	 }
	 file=0;
      }
      else
      {
	 SendRequest();
      }

      state=RECEIVING_HEADER;
      m=MOVED;

   case RECEIVING_HEADER:
      if(send_buf->Error() || recv_buf->Error())
      {
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
	       DebugPrint("<--- ","",2);
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
		     Disconnect();
		     state=DONE;
		     return MOVED;
		  }
		  return MOVED;
	       }
	       goto pre_RECEIVING_BODY;
	    }
	    len=eol-buf;
	    xfree(line);
	    line=(char*)xmalloc(len+1);
	    memcpy(line,buf,len);
	    line[len]=0;

	    recv_buf->Skip(len+2);

	    DebugPrint("<--- ",line,2);
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
		  DebugPrint("**** ","Could not parse HTTP status line",1);
		  // simple 0.9 ?
		  proto_version=0x09;
		  goto pre_RECEIVING_BODY;
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
	 else
	    return m;
      }

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
	 char *err=(char*)alloca(strlen(status)+strlen(file)+xstrlen(location)+20);

	 if(H_REDIRECTED(status_code))
	    sprintf(err,"%s (%s -> %s)",status+status_consumed,file,
				    location?location:"nowhere");
	 else
	    sprintf(err,"%s (%s)",status+status_consumed,file);
	 Disconnect();
	 SetError(NO_FILE,err);
	 return MOVED;
      }
      if(mode==CHANGE_DIR)
      {
	 xfree(cwd);
	 cwd=xstrdup(file);
	 Disconnect();
	 state=DONE;
	 return MOVED;
      }

      DebugPrint("---- ","Receiving body...",9);
      BytesReset();
      if(real_pos<0) // assume Range: did not work
	 real_pos=0;
      state=RECEIVING_BODY;
      m=MOVED;
   case RECEIVING_BODY:
      data_buf=recv_buf;
      if(recv_buf->Error() || send_buf->Error())
      {
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Size()>=BytesAllowed())
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
	    if(CheckTimeout())
	       return MOVED;
	 }
      }
      return m;

   case DONE:
   case ERROR:
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
   saved_errno=errno;
   Disconnect();
   SetError(SEE_ERRNO,strerror(saved_errno));
   return MOVED;
}

void  Http::ClassInit()
{
   // register the class
   Register("http",Http::New);
}

int Http::Read(void *buf,int size)
{
   if(state==ERROR)
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
	 DebugPrint("---- ","Hit EOF",9);
	 if(bytes_received<body_size)
	 {
	    DebugPrint("**** ","Received not enough data, retrying",1);
	    Disconnect();
	    return DO_AGAIN;
	 }
	 return 0;
      }
      if(body_size>=0 && bytes_received>=body_size)
      {
	 Disconnect();
	 return 0; // all received
      }
      int bytes_allowed=BytesAllowed();
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(size1==0)
	 return DO_AGAIN;
      if(real_pos<pos)
      {
	 int to_skip=pos-real_pos;
	 if(to_skip>size1)
	    to_skip=size1;
	 recv_buf->Skip(to_skip);
	 real_pos+=to_skip;
	 goto get_again;
      }
      if(size>size1)
	 size=size1;
      memcpy(buf,buf1,size);
      recv_buf->Skip(size);
      pos+=size;
      real_pos+=size;
      bytes_received+=size;
      BytesUsed(size);
      retries=0;
      return size;
   }
   return DO_AGAIN;
}

int Http::Done()
{
   if(mode==CLOSED)
      return OK;
   if(state==ERROR)
      return error_code;
   if(state==DONE)
      return OK;
   return IN_PROGRESS;
}

int Http::Write(const void *buf,int size)
{
   //FIXME
}

int Http::StoreStatus()
{
   //FIXME
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
	 if(now-try_time<sleep_time)
	    return(_("Delaying before reconnect"));
      }
      return "";
   case CONNECTING:
      return(_("Connecting..."));
   case RECEIVING_HEADER:
      if(!status)
	 return(_("Waiting for response..."));
      return(_("Fetching headers..."));
   case RECEIVING_BODY:
      return(_("Receiving data"));
   case ERROR:
   case DONE:
      return "";
   }
   abort();
}

void Http::Reconfig()
{
   xfree(closure);
   closure=xstrdup(hostname);
   const char *c=closure;

   super::Reconfig();

   timeout = Query("timeout",c);
   sleep_time = Query("reconnect-interval",c);
   idle = Query("idle",c);
   max_retries = Query("max-retries",c);
   relookup_always = Query("relookup-always",c);
   socket_buffer = Query("socket-buffer",c);
   socket_maxseg = Query("socket-maxseg",c);

   SetProxy(Query("proxy",c));

   if(sock!=-1)
      SetSocketBuffer(sock,socket_buffer);
}

void Http::ClearPeer()
{
   xfree(peer);
   peer=0;
   peer_curr=peer_num=0;
}

void Http::NextPeer()
{
   peer_curr++;
   if(peer_curr>peer_num)
      peer_curr=0;
}

void Http::SetProxy(const char *px)
{
   bool was_proxied=(proxy!=0);

   xfree(proxy); proxy=0;
   xfree(proxy_port); proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;

   if(!px)
   {
   no_proxy:
      if(was_proxied)
	 ClearPeer();
      return;
   }

   ParsedURL url(px);
   if(!url.host || url.host[0]==0)
      goto no_proxy;

   proxy=xstrdup(url.host);
   proxy_port=xstrdup(url.port);
   proxy_user=xstrdup(url.user);
   proxy_pass=xstrdup(url.pass);
   if(proxy_port==0)
      proxy_port=xstrdup(HTTP_DEFAULT_PROXY_PORT);
   ClearPeer();
}

void Http::BumpEventTime(time_t t)
{
   if(event_time<t)
      event_time=t;
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
#define ISSPACE(c) isspace((unsigned char)(c))
#define ISDIGIT(c) isdigit((unsigned char)(c))

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
