/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "ftpclass.h"
#include "ProtoList.h"
#include "xstring.h"
#include "xmalloc.h"
#include "xalloca.h"
#include "url.h"
#include "FtpListInfo.h"

enum {TYPE_A,TYPE_I};

#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <assert.h>
#include <pwd.h>

#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h>
#endif

#ifdef HAVE_SYS_POLL_H
# include <sys/poll.h>
#else
# include <poll.h>
#endif

#include "xalloca.h"

#define FTPUSER "anonymous"
#define FTPPORT 21
#define FTP_DATA_PORT 20

#define super FileAccess

static const char *ProxyValidate(char **p)
{
   ParsedURL url(*p);
   if(url.host==0)
   {
      if((*p)[0]!=0)
	 (*p)[0]=0;
      return 0;
   }
   if(url.proto)
   {
      if(strcmp(url.proto,"ftp") /* TODO: && strcmp(url.proto,"http")*/)
	 return _("Proxy protocol unsupported");
   }
   return 0;
}

static ResDecl
   res_sync_mode	("ftp:sync-mode",      "on", ResMgr::BoolValidate,0),
   res_timeout		("ftp:timeout",	       "600",ResMgr::UNumberValidate,0),
   res_redial_interval	("ftp:redial-interval","30", ResMgr::UNumberValidate,0),
   res_passive_mode	("ftp:passive-mode",   "off",ResMgr::BoolValidate,0),
   res_relookup_always	("ftp:relookup-always","off",ResMgr::BoolValidate,0),
   res_proxy		("ftp:proxy",          "",   ProxyValidate,0),
   res_nop_interval	("ftp:nop-interval",   "120",ResMgr::UNumberValidate,0),
   res_idle		("ftp:idle",	       "180",ResMgr::UNumberValidate,0),
   res_max_retries	("ftp:max-retries",    "0",  ResMgr::UNumberValidate,0),
   res_allow_skey	("ftp:skey-allow",     "yes",ResMgr::BoolValidate,0),
   res_force_skey	("ftp:skey-force",     "no", ResMgr::BoolValidate,0),
   res_anon_user	("ftp:anon-user",      "anonymous",0,0),
   res_anon_pass	("ftp:anon-pass",      0,    0,0),
   res_socket_buffer	("ftp:socket-buffer",  "0",  ResMgr::UNumberValidate,0),
   res_address_verify	("ftp:verify-address", "yes",ResMgr::BoolValidate,0),
   res_port_verify      ("ftp:verify-port",    "no", ResMgr::BoolValidate,0);

void  Ftp::ClassInit()
{
   // register the class
   (void)new Protocol("ftp",Ftp::New);
   if(res_anon_pass.Query(0)==(const char *)0)
      ResMgr::Set(res_anon_pass.name,0,DefaultAnonPass());
}

Ftp *Ftp::ftp_chain=0;

static bool NotSerious(int e)
{
   switch(e)
   {
   case(ETIMEDOUT):
#ifdef ECONNRESET
   case(ECONNRESET):
#endif
   case(ECONNREFUSED):
#ifdef EHOSTUNREACH
   case(EHOSTUNREACH):
#endif
#ifdef EHOSTDOWN
   case(EHOSTDOWN):
#endif
#ifdef ENETRESET
   case(ENETRESET):
#endif
#ifdef ENETUNREACH
   case(ENETUNREACH):
#endif
#ifdef ENETDOWN
   case(ENETDOWN):
#endif
#ifdef ECONNABORTED
   case(ECONNABORTED):
#endif
      return true;
   }
   return false;
}

bool Ftp::data_address_ok()
{
   struct sockaddr d;
   struct sockaddr c;
   ADDRLEN_TYPE len;
   len=sizeof(d);
   getpeername(data_sock,&d,&len);
   len=sizeof(c);
   getpeername(control_sock,&c,&len);
   if(d.sa_family!=c.sa_family)
      return false;
   if(d.sa_family==AF_INET)
   {
      struct sockaddr_in *dp=(struct sockaddr_in*)&d;
      struct sockaddr_in *cp=(struct sockaddr_in*)&c;
      if(memcmp(&dp->sin_addr,&cp->sin_addr,sizeof(dp->sin_addr)))
	 goto address_mismatch;
      if(dp->sin_port!=htons(FTP_DATA_PORT))
	 goto wrong_port;
      return true;
   }
   return true;

wrong_port:
   if(!verify_data_port)
      return true;
   DebugPrint("**** ",_("Data connection peer has wrong port number"),0);
   return false;

address_mismatch:
   if(!verify_data_address)
      return true;
   DebugPrint("**** ",_("Data connection peer has mismatching address"),0);
   return false;
}

/* Procedures for checking for a special answers */

int Ftp::ReadyCheck(int,int)
{
   // M$ can't get it right... I'm really tired of setting sync-mode manually.
   if(!(flags&SYNC_MODE) && strstr(line,"Microsoft FTP Service (Version 3.0)"))
   {
      DebugPrint("---- ","Turning on sync-mode",3);
      flags|=SYNC_MODE;
      ResMgr::Set("ftp:sync-mode",hostname,"on");
      try_time=0; // retry immediately
      return INITIAL_STATE;
   }
   return -1;
}

int   Ftp::RestCheck(int act,int exp)
{
   (void)exp;
   if(act/100==5)
   {
      DebugPrint("---- ","Switching to NOREST mode");
      flags|=NOREST_MODE;
      real_pos=0;
      if(mode==STORE)
	 pos=0;
      return(state);
   }
   real_pos=pos;  // REST successful
   return state;
}

int   Ftp::NoFileCheck(int act,int exp)
{
   (void)exp;
   if(act==RESP_NOT_IMPLEMENTED || act==RESP_NOT_UNDERSTOOD)
      return(FATAL_STATE);
   if(act/100==5)
   {
      if(strstr(line,"Broken pipe"))
	 return INITIAL_STATE;
      if(real_pos>0 && !(flags&IO_FLAG))
      {
	 DebugPrint("---- ","Switching to NOREST mode");
	 flags|=NOREST_MODE;
	 real_pos=0;
	 if(mode==STORE)
	    pos=0;
	 state=EOF_STATE;
	 return(state);
      }
      return(NO_FILE_STATE);
   }
   return(-1);
}

int Ftp::CWD_Check(int act,int exp)
{
   if(act/100==2)
   {
      xfree(cwd);
      cwd=xstrdup(target_cwd);
      xfree(real_cwd);
      real_cwd=target_cwd;
      target_cwd=0;
      return state;
   }
   else if(act/100==5)
      return NO_FILE_STATE;
   return(-1);
}

// check response for 'CWD $cwd' command.
int   Ftp::CwdCwd_Check(int act,int exp)
{
   if(act!=exp)
      return NO_FILE_STATE;

   set_real_cwd(cwd);

   return state;
}

int   Ftp::TransferCheck(int act,int exp)
{
   (void)exp;
   if(mode==CLOSED || RespQueueSize()>1)
      return state;
   if(act==RESP_NO_FILE && mode==LIST)
      return(NO_FILE_STATE);
   if(act==RESP_BROKEN_PIPE)
   {
      if(data_sock==-1 && strstr(line,"Broken pipe"))
   	 return(state);
   }
   return(NoFileCheck(act,exp));
}

int   Ftp::LoginCheck(int act,int exp)
{
   if(ignore_pass)
      return state;
   (void)exp;
   if(act==RESP_LOGIN_FAILED)
   {
      if(strstr(line,"Login incorrect")) // Don't translate!!!
      {
	 DebugPrint("---- ",_("Saw `Login incorrect', assume failed login"),9);
	 return(LOGIN_FAILED_STATE);
      }
      return(-1);
   }
   if(act/100==5)
      return(LOGIN_FAILED_STATE);

   return(-1);
}

int   Ftp::NoPassReqCheck(int act,int exp) // for USER command
{
   (void)exp;
   if(act==RESP_LOGGED_IN) // in some cases, ftpd does not ask for pass.
   {
      ignore_pass=true;
      return(state);
   }
   if(act==530)	  // no such user or overloaded server
   {
      if(strstr(line,"unknown")) // Don't translate!!!
      {
	 DebugPrint("---- ",_("Saw `unknown', assume failed login"),9);
	 return(LOGIN_FAILED_STATE);
      }
      return -1;
   }
   if(act/100==5)
   {
      // proxy interprets USER as user@host, so we check for host name validity
      if(proxy && (strstr(line,"host") || strstr(line,"resolve")))
      {
	 DebugPrint("---- ",_("assuming failed host name lookup"),9);
	 xfree(last_error_resp);
	 last_error_resp=xstrdup(line);
	 Disconnect();
	 return(LOOKUP_ERROR_STATE);
      }
      return(LOGIN_FAILED_STATE);
   }
   if(act==331 && allow_skey && user && pass && result)
   {
      skey_pass=xstrdup(make_skey_reply());
      free(result); result=0; result_size=0;
      if(force_skey && skey_pass==0)
      {
	 // FIXME
	 return(LOGIN_FAILED_STATE);
      }
   }
   if(act/100==3)
      return state;
   return(-1);
}

int   Ftp::proxy_LoginCheck(int act,int exp)
{
   (void)exp;
   if(act==RESP_LOGIN_FAILED)
   {
      if(strstr(line,"Login incorrect")) // Don't translate!!!
      {
	 DebugPrint("---- ",_("Saw `Login incorrect', assume failed login"),9);
	 return(LOGIN_FAILED_STATE);
      }
   }
   if(act/100==5)
      return(LOGIN_FAILED_STATE);
   return(-1);
}

int   Ftp::proxy_NoPassReqCheck(int act,int exp)
{
   (void)exp;
   if(act==530)
   {
      if(strstr(line,"unknown"))
      {
	 DebugPrint("---- ",_("Saw `unknown', assume failed login"),9);
	 return(LOGIN_FAILED_STATE);
      }
      return -1;
   }
   if(act/100==5)
      return(LOGIN_FAILED_STATE);
   return(-1);
}

int   Ftp::IgnoreCheck(int,int)
{
   return(state);
}

int   Ftp::RNFR_Check(int act,int exp)
{
   if(act==exp)
   {
      char *str=(char*)alloca(10+strlen(file1));
      sprintf(str,"RNTO %s\n",file1);
      SendCmd(str);
      AddResp(250,INITIAL_STATE,&NoFileCheck);
      return state;
   }
   return NoFileCheck(act,exp);
}

char *Ftp::ExtractPWD()
{
   static char pwd[1024];

   if(sscanf(line,"%*d \"%[^\"]\"",pwd)!=1)
      return "";

   if(isalpha(pwd[0]) && pwd[1]==':')
      flags|=DOSISH_PATH;
   int slash=0;
   for(char *s=pwd; *s; s++)
   {
      if(*s=='/')
      {
	 slash=1;
	 break;
      }
   }
   if(slash==0 || (flags&DOSISH_PATH))
   {
      // for safety -- against dosish ftpd
      for(char *s=pwd; *s; s++)
	 if(*s=='\\')
	    *s='/';
   }
   return pwd;
}

int   Ftp::PASV_Catch(int act,int)
{
   if(act==RESP_NOT_IMPLEMENTED || act==RESP_NOT_UNDERSTOOD || act==504)
   {
   sw_off:
      DebugPrint("---- ",_("Switching passive mode off"));
      SetFlag(PASSIVE_MODE,0);
      return(INITIAL_STATE);
   }
   if(act/100!=2)
      return(-1);
   char *b=strchr(line,'(');
   if(b==0)
      goto sw_off;
   unsigned a0,a1,a2,a3,p0,p1;
   if(sscanf(b,"(%u,%u,%u,%u,%u,%u)",&a0,&a1,&a2,&a3,&p0,&p1)!=6)
      goto sw_off;
   data_sa.sin_family=AF_INET;
   unsigned char *a,*p;
   a=(unsigned char*)&data_sa.sin_addr;
   p=(unsigned char*)&data_sa.sin_port;
   if(a0==0 && a1==0 && a2==0 && a3==0)
   {
      // broken server, try to fix up
      struct sockaddr_in *c=&peer_sa;
      memcpy(a,&c->sin_addr,sizeof(c->sin_addr));
   }
   else
   {
      a[0]=a0; a[1]=a1; a[2]=a2; a[3]=a3;
   }
   p[0]=p0; p[1]=p1;
   addr_received=1;
   return state;
}

int   Ftp::CatchHomePWD(int act,int exp)
{
   if(act==exp && !home)
   {
      home=xstrdup(ExtractPWD());
   }
   return state;
}

int   Ftp::CatchDATE(int act,int)
{
   if(!array_for_info)
      return state;

   if(act/100==2)
   {
      if(strlen(line)>4 && isdigit(line[4]))
	 array_for_info[array_ptr].time=ConvertFtpDate(line+4);
      else
	 array_for_info[array_ptr].time=(time_t)-1;
   }
   else if(act/100!=5)
      return -1;
   else
      array_for_info[array_ptr].time=(time_t)-1;

   array_for_info[array_ptr].get_time=false;
   if(!array_for_info[array_ptr].get_size)
      array_ptr++;

   retries=0;

   return state;
}
int   Ftp::CatchDATE_opt(int act,int)
{
   if(!opt_date)
      return state;

   if(act/100==2 && strlen(line)>4 && isdigit(line[4]))
   {
      *opt_date=ConvertFtpDate(line+4);
      opt_date=0;
   }
   else
      *opt_date=(time_t)-1;
   return state;
}

int   Ftp::CatchSIZE(int act,int)
{
   if(!array_for_info)
      return state;

   if(act/100==2)
   {
      if(strlen(line)>4 && isdigit(line[4]))
	 array_for_info[array_ptr].size=atol(line+4);
      else
	 array_for_info[array_ptr].size=-1;
   }
   else if(act/100!=5)
      return -1;
   else
      array_for_info[array_ptr].size=-1;

   array_for_info[array_ptr].get_size=false;
   if(!array_for_info[array_ptr].get_time)
      array_ptr++;

   retries=0;

   return state;
}
int   Ftp::CatchSIZE_opt(int act,int)
{
   if(!opt_size)
      return state;

   if(act/100==2 && strlen(line)>4 && isdigit(line[4]))
   {
      *opt_size=atol(line+4);
      opt_size=0;
   }
   else
      *opt_size=-1;
   return state;
}

void Ftp::InitFtp()
{
   control_sock=-1;
   data_sock=-1;

   try_time=0;
   nop_time=0;
   nop_count=0;
   nop_offset=0;
   resp=0;
   resp_size=0;
   resp_alloc=0;
   line=0;
   type=TYPE_A;
   send_cmd_buffer=0;
   send_cmd_alloc=0;
   send_cmd_count=0;
   send_cmd_ptr=0;
   result=NULL;
   result_size=0;
   state=NO_HOST_STATE;
   flags=SYNC_MODE;
   resolver=0;
   lookup_done=false;
   wait_flush=false;
   ignore_pass=false;
   proxy=0;
   proxy_port=0;
   proxy_user=proxy_pass=0;
   idle=0;
   idle_start=time(0);
   max_retries=0;
   retries=0;
   target_cwd=0;
   skey_pass=0;
   allow_skey=true;
   force_skey=false;
   verify_data_address=true;
   socket_buffer=0;

   RespQueue=0;
   RQ_alloc=0;
   EmptyRespQueue();

   anon_pass=0;
   anon_user=0;	  // will be set by Reconfig()

   ftp_next=ftp_chain;
   ftp_chain=this;

   Reconfig();
}
Ftp::Ftp() : super()
{
   InitFtp();
}
Ftp::Ftp(const Ftp *f) : super(f)
{
   InitFtp();

   if(f->anon_pass)
   {
      xfree(anon_pass);
      anon_pass=xstrdup(f->anon_pass);
   }
   if(f->anon_user)
   {
      xfree(anon_user);
      anon_user=xstrdup(f->anon_user);
   }

   if(f->state!=NO_HOST_STATE)
      state=INITIAL_STATE;
   if(!relookup_always)
   {
      memcpy(&peer_sa,&f->peer_sa,sizeof(peer_sa));
      lookup_done=f->lookup_done;
   }
   flags=f->flags&MODES_MASK;
}

Ftp::~Ftp()
{
   Close();
   Disconnect();

   xfree(anon_user); anon_user=0;
   xfree(anon_pass); anon_pass=0;
   xfree(line); line=0;
   xfree(resp); resp=0;
   xfree(target_cwd); target_cwd=0;

   xfree(RespQueue); RespQueue=0;
   xfree(send_cmd_buffer); send_cmd_buffer=0;

   xfree(proxy); proxy=0;

   xfree(skey_pass); skey_pass=0;

   for(Ftp **scan=&ftp_chain; *scan; scan=&((*scan)->ftp_next))
   {
      if(*scan==this)
      {
	 *scan=ftp_next;
	 break;
      }
   }
}

int   Ftp::CheckTimeout()
{
   if(time(NULL)-event_time>=timeout)
   {
      /* try to autodetect faulty ftp server */
      /* Some Windows based ftp servers seem to clear input queue
	    after USER command */
      if(!RespQueueIsEmpty() && RespQueue[RQ_head].expect==RESP_LOGGED_IN
      && !(flags&SYNC_MODE))
      {
	 flags|=SYNC_MODE;
	 DebugPrint("**** ",_("Timeout - trying sync mode (is it windoze?)"));
      }
      else
	 DebugPrint("**** ",_("Timeout - reconnecting"));
      Disconnect();
      time(&event_time);
      return(1);
   }
   block+=TimeOut(timeout*1000);
   return(0);
}

int   Ftp::AbsolutePath(const char *s)
{
   if(!s)
      return 0;
   return(s[0]=='/'
      || ((flags&DOSISH_PATH) && isalpha(s[0]) && s[1]==':' && s[2]=='/'));
}

void  Ftp::GetBetterConnection(int level)
{
   if(level==0 && cwd==0)
      return;

   for(Ftp *o=ftp_chain; o!=0; o=o->ftp_next)
   {
      if(o==this)
	 continue;
      if(o->control_sock==-1 || o->data_sock!=-1 || o->state!=EOF_STATE
      || !o->RespQueueIsEmpty() || o->mode!=CLOSED)
	 continue;
      if(SameConnection(o))
      {
	 if(!relookup_always)
	 {
	    if(lookup_done && !o->lookup_done)
	    {
	       o->lookup_done=true;
	       o->peer_sa=peer_sa;
	    }
	    else if(o->lookup_done && !lookup_done)
	    {
	       lookup_done=true;
	       peer_sa=o->peer_sa;
	    }
	 }

	 if(home && !o->home)
	    o->home=xstrdup(home);
	 else if(!home && o->home)
	    home=xstrdup(o->home);

	 o->ExpandTildeInCWD();
	 ExpandTildeInCWD();

	 if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	    continue;

	 // so borrow the connection
	 o->state=INITIAL_STATE;
	 control_sock=o->control_sock;
	 o->control_sock=-1;
	 state=EOF_STATE;
	 type=o->type;
	 event_time=o->event_time;

	 set_real_cwd(o->real_cwd);
	 o->set_real_cwd(0);
      	 o->Disconnect();
      }
   }
}

void  Ftp::SetSocketBuffer(int sock)
{
   if(socket_buffer==0)
      return;
   setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&socket_buffer,sizeof(socket_buffer));
   setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&socket_buffer,sizeof(socket_buffer));
}

void  Ftp::SetKeepAlive(int sock)
{
   static int one=1;
   setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&one,sizeof(one));
}

int   Ftp::Do()
{
   char	 *str =(char*)alloca(xstrlen(cwd)+xstrlen(hostname)+xstrlen(proxy)+256);
   char	 *str1=(char*)alloca(xstrlen(file)+20);
   char	 *str2=(char*)alloca(xstrlen(file)+20);
   int   old_type,res;
   ADDRLEN_TYPE addr_len;
   unsigned char *a;
   unsigned char *p;
   time_t   t;
   automate_state oldstate;
   int	 m=STALL;

   // check if idle time exceeded
   if(mode==CLOSED && control_sock!=-1 && idle>0)
   {
      time_t now=time(0);
      if(now-idle_start>=idle)
      {
	 DebugPrint("---- ",_("Closing idle connection"),2);
	 Disconnect();
	 return m;
      }
      block+=TimeOut((idle_start+idle-now)*1000);
   }

   switch(state)
   {
   case(INITIAL_STATE):
   {
      if(mode==CLOSED)
	 return m;

      if(hostname==0)
	 return m;

      // walk through ftp classes and try to find identical idle ftp session
      GetBetterConnection(0);
      GetBetterConnection(1);

      if(state!=INITIAL_STATE)
	 return MOVED;

      char *host_to_connect=(proxy?proxy:hostname);
      int port_to_connect=(proxy?proxy_port:port);
      if(port_to_connect==0)
	 port_to_connect=FTPPORT;

      if(!lookup_done)
      {
	 if(!resolver)
	 {
	    DebugPrint("---- ",_("Resolving host address..."),4);
	    resolver=new Resolver(host_to_connect,port_to_connect);
	    m=MOVED;
	 }
	 if(!resolver->Done())
	    return m;

	 if(resolver->Error())
	 {
	    xfree(last_error_resp);
	    last_error_resp=xstrdup(resolver->ErrorMsg());
	    SwitchToState(LOOKUP_ERROR_STATE);
	    return(MOVED);
	 }

	 resolver->GetResult(&peer_sa);

	 delete resolver;
	 resolver=0;
	 lookup_done=true;
	 m=MOVED;
      }

      if(mode==CONNECT_VERIFY)
	 return m;

      time(&t);
      if(t-try_time<sleep_time)
      {
	 block+=TimeOut(1000*(sleep_time-(t-try_time)));
	 return m;
      }
      time(&try_time);

      if(max_retries>0 && retries>=max_retries)
      {
	 xfree(last_error_resp);
	 last_error_resp=xstrdup(_("max-retries exceeded"));
	 SwitchToState(FATAL_STATE);
      	 return MOVED;
      }
      retries++;

      control_sock=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
      if(control_sock==-1)
	 goto system_error;
      SetKeepAlive(control_sock);
      SetSocketBuffer(control_sock);
      fcntl(control_sock,F_SETFL,O_NONBLOCK);
      fcntl(control_sock,F_SETFD,FD_CLOEXEC);

      a=(unsigned char*)&peer_sa.sin_addr;
      sprintf(str,_("Connecting to %s%s (%d.%d.%d.%d) port %u"),proxy?"proxy ":"",
	    host_to_connect,a[0],a[1],a[2],a[3],ntohs(peer_sa.sin_port));
      DebugPrint("---- ",str,0);

      res=connect(control_sock,(struct sockaddr*)&peer_sa,sizeof(peer_sa));

      if(relookup_always && !proxy)
	 lookup_done=false;

      if(res==-1
#ifdef EINPROGRESS
      && errno!=EINPROGRESS
#endif
      )
      {
	 sprintf(str,"connect: %s",strerror(errno));
         DebugPrint("**** ",str,0);
         Disconnect();
	 if(NotSerious(errno))
	    return MOVED;
	 goto system_error;
      }
      state=CONNECTING_STATE;
      m=MOVED;
      time(&event_time);
   }

   case(CONNECTING_STATE):
   {
      res=Poll(control_sock,POLLOUT);
      if(state!=CONNECTING_STATE)
	 return MOVED;
      if(!(res&POLLOUT))
	 goto usual_return;

      if(flags&SYNC_MODE)
	 flags|=SYNC_WAIT; // we need to wait for RESP_READY

      AddResp(RESP_READY,INITIAL_STATE,&ReadyCheck);

      char *user_to_use=(user?user:anon_user);
      if(proxy)
      {
	 char *combined=(char*)alloca(strlen(user_to_use)+1+strlen(hostname)+20+1);
	 sprintf(combined,"%s@%s",user_to_use,hostname);
	 if(port)
	    sprintf(combined+strlen(combined),":%d",port);
	 user_to_use=combined;

      	 if(proxy_user && proxy_pass)
	 {
	    sprintf(str,"USER %s\nPASS %s\n",proxy_user,proxy_pass);
	    AddResp(RESP_PASS_REQ,INITIAL_STATE,&proxy_NoPassReqCheck);
	    AddResp(RESP_LOGGED_IN,INITIAL_STATE,&proxy_LoginCheck);
	    SendCmd(str);
	 }
      }

      xfree(skey_pass);
      skey_pass=0;

      ignore_pass=false;
      sprintf(str,"USER %s\n",user_to_use);
      AddResp(RESP_PASS_REQ,INITIAL_STATE,&NoPassReqCheck,allow_skey);
      SendCmd(str);

      state=USER_RESP_WAITING_STATE;
      m=MOVED;
   }

   case(USER_RESP_WAITING_STATE):
      if(((flags&SYNC_MODE) || (user && pass && allow_skey))
      && !RespQueueIsEmpty())
      {
	 FlushSendQueue();
	 ReceiveResp();
	 if(state!=USER_RESP_WAITING_STATE)
	    return MOVED;
	 if(!RespQueueIsEmpty())
	    goto usual_return;
      }

      if(!ignore_pass)
      {
	 const char *pass_to_use=pass?pass:anon_pass;
	 if(allow_skey && skey_pass)
	    pass_to_use=skey_pass;
	 sprintf(str,"PASS %s\n",pass_to_use);
	 AddResp(RESP_LOGGED_IN,INITIAL_STATE,&LoginCheck);
	 SendCmd(str);
      }

      // FIXME: site group/site gpass

      if(!home)
      {
	 // if we don't yet know the home location, try to get it
	 SendCmd("PWD");
	 AddResp(RESP_PWD_MKD_OK,INITIAL_STATE,&CatchHomePWD);
      }

      set_real_cwd("~");   // starting point
      type=TYPE_A;	   // just after login we are in TEXT mode

      state=EOF_STATE;
      m=MOVED;

   case(EOF_STATE):
      FlushSendQueue();
      ReceiveResp();
      if(state!=EOF_STATE)
	 return MOVED;

      if(mode==CLOSED || mode==CONNECT_VERIFY)
	 goto notimeout_return;

      if(home==0 && !RespQueueIsEmpty())
	 goto usual_return;

      ExpandTildeInCWD();

      if(mode!=CHANGE_DIR && xstrcmp(cwd,real_cwd))
      {
	 char *s=(char*)alloca(strlen(cwd)+5);
	 sprintf(s,"CWD %s",cwd);
	 SendCmd(s);
	 AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,&CwdCwd_Check);
      }
      state=CWD_CWD_WAITING_STATE;
      m=MOVED;


   case CWD_CWD_WAITING_STATE:
      FlushSendQueue();
      ReceiveResp();
      if(state!=CWD_CWD_WAITING_STATE)
	 return MOVED;

      if(mode!=CHANGE_DIR && xstrcmp(cwd,real_cwd))
	 goto usual_return;

      if(mode==STORE && (flags&NOREST_MODE) && pos>0)
	 pos=0;

      if(mode==RETRIEVE || mode==STORE || mode==LIST || mode==LONG_LIST)
      {
	 data_sock=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
	 if(data_sock==-1)
	    goto system_error;
	 fcntl(data_sock,F_SETFL,O_NONBLOCK);
	 fcntl(data_sock,F_SETFD,FD_CLOEXEC);
	 SetKeepAlive(data_sock);
	 SetSocketBuffer(data_sock);
      }

      old_type=type;
      if((flags&NOREST_MODE) || pos==0)
	 real_pos=0;
      else
	 real_pos=-1;	// we don't yet know if REST will succeed

      flags&=~IO_FLAG;

      switch((enum open_mode)mode)
      {
      case(RETRIEVE):
         type=TYPE_I;
         sprintf(str1,"RETR %s\n",file);
         break;
      case(STORE):
         type=TYPE_I;
         sprintf(str1,"STOR %s\n",file);
         break;
      case(LONG_LIST):
         type=TYPE_A;
         if(file && file[0])
            sprintf(str1,"LIST %s\n",file);
         else
            sprintf(str1,"LIST\n");
         break;
      case(LIST):
         type=TYPE_A;
         real_pos=0; // REST doesn't work for NLST
	 if(file && file[0])
            sprintf(str1,"NLST %s\n",file);
         else
            sprintf(str1,"NLST\n");
         break;
      case(CHANGE_DIR):
	 if(!xstrcmp(real_cwd,file))
	 {
	    xfree(cwd);
	    cwd=xstrdup(real_cwd);
	 }
	 else
	 {
	    xfree(target_cwd);
	    target_cwd=xstrdup(file);

	    int len=xstrlen(real_cwd);
	    if(!AbsolutePath(file) && real_cwd
	    && !strncmp(file,real_cwd,len) && file[len]=='/')
	       sprintf(str1,"CWD .%s\n",file+len);
	    else
	       sprintf(str1,"CWD %s\n",file);

	    SendCmd(str1);
	    AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,&CWD_Check);
	 }
	 state=WAITING_STATE;
	 m=MOVED;
	 goto waiting_state_label;
      case(MAKE_DIR):
	 sprintf(str1,"MKD %s\n",file);
	 break;
      case(REMOVE_DIR):
	 sprintf(str1,"RMD %s\n",file);
	 break;
      case(REMOVE):
	 sprintf(str1,"DELE %s\n",file);
	 break;
      case(QUOTE_CMD):
	 sprintf(str1,"%s\n",file);
	 break;
      case(RENAME):
	 sprintf(str1,"RNFR %s\n",file);
	 break;
      case(ARRAY_INFO):
	 type=TYPE_I;
	 break;
      case(CONNECT_VERIFY):
      case(CLOSED):
	 abort(); // can't happen
      }
      if(old_type!=type)
      {
         strcpy(str,type==TYPE_I?"TYPE I\n":"TYPE A\n");
	 SendCmd(str);
	 AddResp(RESP_TYPE_OK,INITIAL_STATE);
      }

      if(opt_size)
      {
	 sprintf(str2,"SIZE %s\n",file);
	 SendCmd(str2);
	 AddResp(RESP_RESULT_HERE,INITIAL_STATE,&CatchSIZE_opt);
      }
      if(opt_date)
      {
	 sprintf(str2,"MDTM %s\n",file);
	 SendCmd(str2);
	 AddResp(RESP_RESULT_HERE,INITIAL_STATE,&CatchDATE_opt);
      }

      if(mode==ARRAY_INFO)
      {
	 for(int i=array_ptr; i<array_cnt; i++)
	 {
	    char *s=(char*)alloca(strlen(array_for_info[i].file)+20);
	    if(array_for_info[i].get_time)
	    {
	       sprintf(s,"MDTM %s\n",array_for_info[i].file);
	       SendCmd(s);
	       AddResp(RESP_RESULT_HERE,INITIAL_STATE,&CatchDATE);
	    }
	    if(array_for_info[i].get_size)
	    {
	       sprintf(s,"SIZE %s\n",array_for_info[i].file);
	       SendCmd(s);
	       AddResp(RESP_RESULT_HERE,INITIAL_STATE,&CatchSIZE);
	    }
	 }
      	 state=WAITING_STATE;
	 m=MOVED;
	 goto waiting_state_label;
      }

      if(mode==QUOTE_CMD
      || mode==REMOVE || mode==REMOVE_DIR || mode==MAKE_DIR || mode==RENAME)
      {
	 if(mode==MAKE_DIR && mkdir_p)
	 {
	    char *sl;
	    sl=strchr(file,'/');
	    while(sl)
	    {
	       if(sl>file)
	       {
		  *sl=0;
		  sprintf(str2,"MKD %s\n",file);
		  SendCmd(str2);
		  AddResp(0,INITIAL_STATE,&IgnoreCheck);
		  *sl='/';
	       }
	       sl=strchr(sl+1,'/');
	    }
	 }
	 SendCmd(str1);
	 if(mode==REMOVE_DIR || mode==REMOVE)
	    AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,&NoFileCheck);
	 else if(mode==MAKE_DIR)
	    AddResp(RESP_PWD_MKD_OK,INITIAL_STATE,&NoFileCheck);
	 else if(mode==QUOTE_CMD)
	    AddResp(0,INITIAL_STATE,&IgnoreCheck,true);
	 else if(mode==RENAME)
	    AddResp(350,INITIAL_STATE,&RNFR_Check);

	 if(result)
	 {
	    free(result);
	    result=0;
	    result_size=0;
	 }
	 state=WAITING_STATE;
	 m=MOVED;
	 goto waiting_state_label;
      }

      if(flags&PASSIVE_MODE)
      {
	 SendCmd("PASV");
	 AddResp(227,INITIAL_STATE,&PASV_Catch);
	 addr_received=0;
      }
      else
      {
	 addr_len=sizeof(struct sockaddr);
	 getsockname(control_sock,(struct sockaddr*)&data_sa,&addr_len);
	 data_sa.sin_port=0;
	 bind(data_sock,(struct sockaddr*)&data_sa,addr_len);
	 listen(data_sock,1);
	 getsockname(data_sock,(struct sockaddr*)&data_sa,&addr_len);
	 a=(unsigned char*)&data_sa.sin_addr;
	 p=(unsigned char*)&data_sa.sin_port;
	 sprintf(str,"PORT %d,%d,%d,%d,%d,%d\n",a[0],a[1],a[2],a[3],p[0],p[1]);
	 SendCmd(str);
	 AddResp(RESP_PORT_OK,INITIAL_STATE);
      }
      if(real_pos==-1)
      {
         sprintf(str,"REST %ld\n",pos);
	 SendCmd(str);
	 AddResp(RESP_REST_OK,INITIAL_STATE,&RestCheck);
      }
      SendCmd(str1);
      AddResp(RESP_TRANSFER_OK,mode==STORE?STORE_FAILED_STATE:INITIAL_STATE,&TransferCheck);

      m=MOVED;
      if(flags&PASSIVE_MODE)
      {
	 state=DATASOCKET_CONNECTING_STATE;
      	 goto datasocket_connecting_state;
      }
      state=ACCEPTING_STATE;

   case(ACCEPTING_STATE):
      FlushSendQueue();
      ReceiveResp();

      res=Poll(data_sock,POLLIN);

      if(state!=ACCEPTING_STATE)
         return MOVED;

      if(!(res&POLLIN))
	 goto usual_return;

      addr_len=sizeof(struct sockaddr);
      res=accept(data_sock,(struct sockaddr *)&data_sa,&addr_len);
      if(res==-1)
      {
	 if(errno==EWOULDBLOCK)
	    goto usual_return;
	 if(NotSerious(errno))
	 {
	    Disconnect();
	    return MOVED;
	 }
	 goto system_error;
      }

      close(data_sock);
      data_sock=res;
      fcntl(data_sock,F_SETFL,O_NONBLOCK);
      fcntl(data_sock,F_SETFD,FD_CLOEXEC);
      SetKeepAlive(data_sock);
      SetSocketBuffer(data_sock);

      state=DATA_OPEN_STATE;
      m=MOVED;

      if(!data_address_ok())
      {
	 Disconnect();
	 return MOVED;
      }

      goto data_open_state;

   case(DATASOCKET_CONNECTING_STATE):
   datasocket_connecting_state:
      FlushSendQueue();
      ReceiveResp();

      if(state!=DATASOCKET_CONNECTING_STATE)
         return MOVED;

      if(addr_received==0)
	 goto usual_return;

      if(addr_received==1)
      {
	 addr_received=2;
	 a=(unsigned char*)&data_sa.sin_addr;
	 sprintf(str,_("Connecting data socket to (%d.%d.%d.%d) port %u"),
			      a[0],a[1],a[2],a[3],ntohs(data_sa.sin_port));
	 DebugPrint("---- ",str,3);
	 if(connect(data_sock,(struct sockaddr*)&data_sa,sizeof(data_sa))==-1
#ifdef EINPROGRESS
	 && errno!=EINPROGRESS
#endif
	 )
	 {
	    sprintf(str,"connect: %s",strerror(errno));
	    DebugPrint("**** ",str,0);
	    Disconnect();
	    if(NotSerious(errno))
	       return MOVED;
	    goto system_error;
	 }
	 m=MOVED;
      }
      res=Poll(data_sock,POLLOUT);

      if(state!=DATASOCKET_CONNECTING_STATE)
         return MOVED;

      if(!(res&POLLOUT))
	 goto usual_return;

      state=DATA_OPEN_STATE;
      m=MOVED;

   case(DATA_OPEN_STATE):
   data_open_state:
   {
      if(RespQueueIsEmpty())
      {
	 // When ftp server has sent "Transfer complete" it is idle,
	 // but the data can be still unsent in server side kernel buffer.
	 // So the ftp server can decide the connection is idle for too long
	 // time and disconnect. This hack is to prevent the above.
	 time_t t=time(0);
	 if(t-nop_time>=nop_interval)
	 {
	    // prevent infinite NOOP's
	    if(nop_offset==pos && nop_count*nop_interval>=timeout)
	    {
	       DebugPrint("**** ",_("Timeout - reconnecting"));
	       Disconnect();
	       return MOVED;
	    }
	    if(nop_time!=0)
	    {
	       nop_count++;
	       SendCmd("NOOP");
	       AddResp(0,0,&IgnoreCheck);
	    }
	    nop_time=t;
	    if(nop_offset!=pos)
	       nop_count=0;
	    nop_offset=pos;
	 }
	 block+=TimeOut((nop_interval-(t-nop_time))*1000);
      }

      int ev=(mode==STORE?POLLOUT:POLLIN);
      oldstate=state;

      FlushSendQueue();
      ReceiveResp();
      if(data_sock!=-1)
	 Poll(data_sock,ev);
      CheckTimeout();

      if(state!=oldstate)
         return MOVED;

      if(data_sock==-1)
      {
	 if(RespQueueIsEmpty())
	 {
	    block+=NoWait();
	    return m;
	 }
      }

      goto usual_return;
   }
   case(WAITING_STATE):
   {
   waiting_state_label:
      oldstate=state;

      FlushSendQueue();
      bool was_empty=RespQueueIsEmpty();
      ReceiveResp();

      if(state!=oldstate)
         return MOVED;

      if(!was_empty && RespQueueIsEmpty())
      {
	 // we've got the last response
	 m=MOVED;
      }

      if(RespQueueSize()==0)
	 block+=NoWait();     // ???

      // store mode is special - the data can be buffered
      if(mode==STORE)
	 goto notimeout_return;

      goto usual_return;
   }
   default:
      // error state - need intervension of other task
      if(mode!=CLOSED)
	 block+=NoWait();
      return m;
   }
   return m;

usual_return:
   if(m==MOVED)
      return MOVED;
   if(CheckTimeout())
      return MOVED;
notimeout_return:
   if(m==MOVED)
      return MOVED;
   if(data_sock!=-1)
   {
      if(state==ACCEPTING_STATE)
	 block+=PollVec(data_sock,POLLIN);
      else if(state==DATASOCKET_CONNECTING_STATE && addr_received==2)
      	 block+=PollVec(data_sock,POLLOUT);
      else if(state==DATA_OPEN_STATE)
      {
	 // guard against unimplemented REST: if we have sent REST command
	 // (real_pos==-1) and did not yet receive the response
	 // (RespQueueSize()>1), don't allow to read/write the data
	 // since REST could fail.
	 if(!(RespQueueSize()>1 && real_pos==-1))
	    block+=PollVec(data_sock,(mode==STORE?POLLOUT:POLLIN));
      }
   }
   if(control_sock!=-1)
   {
      if(state==CONNECTING_STATE)
	 block+=PollVec(control_sock,POLLOUT);
      else
      {
	 block+=PollVec(control_sock,POLLIN);
	 if(send_cmd_count>0 && !(flags&SYNC_WAIT))
	    block+=PollVec(control_sock,POLLOUT);
      }
   }
   return m;

system_error:
   if(errno==ENFILE || errno==EMFILE)
   {
      // file table overflow - it could free sometime
      block+=TimeOut(1000);
      return m;
   }
   SwitchToState(SYSTEM_ERROR_STATE);
   return MOVED;
}

void  Ftp::LogResp(char *l)
{
   if(result==0)
   {
      result=xstrdup(l);
      result_size=strlen(result);
      return;
   }
   result=(char*)xrealloc(result,result_size+strlen(l)+1);
   strcpy(result+result_size,l);
   result_size+=strlen(l);
}

void  Ftp::ReceiveResp()
{
   char  *store;
   char  *nl;
   int   code;

   if(control_sock==-1)
      return;

   for(;;)
   {
      for(;;)
      {
	 if(resp_alloc-resp_size<256)
	    resp=(char*)xrealloc(resp,resp_alloc+=1024);

	 store=resp+resp_size;
	 *store=0;
	 nl=strchr(resp,'\n');
	 if(nl!=NULL)
	 {
	    *nl=0;
	    xfree(line);
	    line=xstrdup(resp);
	    memmove(resp,nl+1,strlen(nl+1)+1);
	    resp_size-=nl-resp+1;
	    if(nl-resp>0 && line[nl-resp-1]=='\r')
	       line[nl-resp-1]=0;

	    code=0;

	    if(strlen(line)>=3 && isdigit(line[0]))
	       sscanf(line,"%3d",&code);

	    int pri=1;
	    if(code==220 || code==230
	    || (code==0 && (multiline_code==220 || multiline_code==230)))
	       pri=0;

	    DebugPrint("<--- ",line,pri);
	    if(!RespQueueIsEmpty() && RespQueue[RQ_head].log_resp)
	    {
	       LogResp(line);
	       LogResp("\n");
	    }

	    if(code==0)
	       continue;

	    if(line[3]=='-')
	    {
	       if(multiline_code==0)
		  multiline_code=code;
	       continue;
	    }
	    if(multiline_code)
	    {
	       if(multiline_code!=code)
		  continue;   // Multiline response can terminate only with
			      // the same code as it started with.
	       multiline_code=0;
	    }
	    flags&=~SYNC_WAIT; // clear the flag to send next command
	    break;
	 }
	 else
	 {
	    struct pollfd pfd;
	    pfd.fd=control_sock;
	    pfd.events=POLLIN;
	    int res=poll(&pfd,1,0);
	    if(res==-1 && errno!=EAGAIN && errno!=EINTR)
	    {
	       SwitchToState(SYSTEM_ERROR_STATE);
	       return;
	    }
	    if(res<=0)
	       return;
	    if(CheckHangup(&pfd,1))
	       return;

	    res=read(control_sock,resp+resp_size,resp_alloc-resp_size-1);
	    if(res==-1)
	    {
	       if(errno==EAGAIN || errno==EINTR)
		  return;
	       if(NotSerious(errno))
	       {
		  Disconnect();
		  return;
	       }
	       SwitchToState(SYSTEM_ERROR_STATE);
	       return;
	    }
	    if(res==0)
	    {
	       DebugPrint("**** ",_("Peer closed connection"));
	       Disconnect();
	       return;
	    }

	    // workaround for \0 characters in response
	    for(int i=0; i<res; i++)
	       if(resp[resp_size+i]==0)
		  resp[resp_size+i]='!';

	    time(&event_time);
	    resp_size+=res;
	 }
      }
      if(code==RESP_RESULT_HERE)
      {
	 if(result)
	    free(result);
	 result=(char*)xmalloc(result_size=strlen(line+4)+1);
	 strcpy(result,line+4);	 // store the response for reference
      }

      int newstate=CheckResp(code);
      if(newstate!=-1 && newstate!=state)
      {
	 if(newstate==FATAL_STATE || newstate==NO_FILE_STATE)
	 {
	    xfree(last_error_resp);
	    last_error_resp=xstrdup(line);
	 }
	 SwitchToState((automate_state)newstate);
	 if(resp_size==0)
	    return;
      }
   }
}

void  Ftp::Disconnect()
{
   DataClose();
   if(control_sock>=0)
   {
      DebugPrint("---- ",_("Closing control socket"),2);
      close(control_sock);
      control_sock=-1;
      if(relookup_always && !proxy)
	 lookup_done=false;
   }
   resp_size=0;
   state=(mode==STORE && (flags&IO_FLAG) ? STORE_FAILED_STATE : INITIAL_STATE);
   send_cmd_count=0;
   flags&=~SYNC_WAIT;
   EmptyRespQueue();
   xfree(send_cmd_buffer);
   send_cmd_buffer=send_cmd_ptr=0;
   send_cmd_alloc=send_cmd_count=0;
}

void  Ftp::DataClose()
{
   if(data_sock>=0)
   {
      DebugPrint("---- ",_("Closing data socket"),2);
      close(data_sock);
      data_sock=-1;
   }
   nop_time=0;
   nop_offset=0;
   nop_count=0;
   xfree(result);
   result=NULL;
}

void  Ftp::FlushSendQueue()
{
   int res;
   struct pollfd pfd;

   pfd.events=POLLOUT;
   pfd.fd=control_sock;
   res=poll(&pfd,1,0);
   if(res==-1 && errno!=EAGAIN && errno!=EINTR)
   {
      SwitchToState(SYSTEM_ERROR_STATE);
      return;
   }
   if(res<=0)
      return;
   if(CheckHangup(&pfd,1))
      return;
   if(!(pfd.revents&POLLOUT))
      return;

   char *cmd_begin=send_cmd_ptr;

   while(send_cmd_count>0 && !(flags&SYNC_WAIT))
   {
      int to_write=send_cmd_count;
      if(flags&SYNC_MODE)
      {
	 char *line_end=(char*)memchr(send_cmd_ptr,'\n',send_cmd_count);
	 if(line_end==NULL)
	    return;
	 to_write=line_end+1-send_cmd_ptr;
      }
      res=write(control_sock,send_cmd_ptr,to_write);
      if(res==0)
	 return;
      if(res==-1)
      {
	 if(errno==EAGAIN || errno==EINTR)
	    return;
	 if(NotSerious(errno) || errno==EPIPE)
	 {
	    Disconnect();
	    return;
	 }
	 SwitchToState(SYSTEM_ERROR_STATE);
	 return;
      }
      send_cmd_count-=res;
      send_cmd_ptr+=res;
      time(&event_time);

      if(flags&SYNC_MODE)
	 flags|=SYNC_WAIT;
   }
   if(send_cmd_ptr>cmd_begin)
   {
      send_cmd_ptr[-1]=0;
      char *p=strstr(cmd_begin,"PASS ");

      bool may_show = (skey_pass!=0) || (user==0);
      if(proxy && proxy_user) // can't distinguish here
	 may_show=false;
      if(p && !may_show)
      {
	 // try to hide password
	 if(p>cmd_begin)
	 {
	    p[-1]=0;
	    DebugPrint("---> ",cmd_begin,3);
	 }
	 DebugPrint("---> ","PASS XXXX",3);
	 char *eol=strchr(p,'\n');
	 if(eol)
	 {
	    *eol=0;
	    DebugPrint("---> ",eol+1,3);
	 }
      }
      else
      {
	 DebugPrint("---> ",cmd_begin,3);
      }
   }
}

void  Ftp::SendCmd(const char *cmd)
{
   char ch,prev_ch;
   if(send_cmd_count==0)
      send_cmd_ptr=send_cmd_buffer;
   prev_ch=0;
   while((ch=*cmd++)!=0)
   {
      if(send_cmd_ptr-send_cmd_buffer+send_cmd_count+1>=send_cmd_alloc)
      {
	 if(send_cmd_ptr-send_cmd_buffer<2)
	 {
	    int shift=send_cmd_ptr-send_cmd_buffer;
	    if(send_cmd_alloc==0)
	       send_cmd_alloc=0x80;
	    send_cmd_buffer=(char*)xrealloc(send_cmd_buffer,send_cmd_alloc*=2);
	    send_cmd_ptr=send_cmd_buffer+shift;
	 }
	 memmove(send_cmd_buffer,send_cmd_ptr,send_cmd_count);
	 send_cmd_ptr=send_cmd_buffer;
      }
      if(ch=='\n' && prev_ch!='\r')
      {
	 ch='\r';
	 cmd--;
      }
      else if(ch=='\377') // double chr(255) as in telnet protocol
      	 send_cmd_ptr[send_cmd_count++]='\377';
      send_cmd_ptr[send_cmd_count++]=prev_ch=ch;
      if(*cmd==0 && ch!='\n')
	 cmd="\n";
   }
}

int   Ftp::SendEOT()
{
   if(mode==STORE)
   {
      if(state==DATA_OPEN_STATE)
      {
	 DataClose();
	 state=WAITING_STATE;
      	 return(OK);
      }
      return(DO_AGAIN);
   }
   return(OK);
}

void  Ftp::Close()
{
   if(mode!=CLOSED)
      set_idle_start();

   retries=0;

   flags&=~NOREST_MODE;	// can depend on a particular file

   if(resolver)
   {
      delete resolver;
      resolver=0;
   }

   Resume();
   ExpandTildeInCWD();
   DataClose();
   if(control_sock!=-1)
   {
      switch(state)
      {
      case(ACCEPTING_STATE):
      case(CWD_CWD_WAITING_STATE):
      case(CONNECTING_STATE):
      case(DATASOCKET_CONNECTING_STATE):
      case(USER_RESP_WAITING_STATE):
	 Disconnect();
	 break;
      case(WAITING_STATE):
	 if((mode==CHANGE_DIR || mode==QUOTE_CMD) && !RespQueueIsEmpty())
	 {
	    Disconnect();
	    break;
	 }
      case(DATA_OPEN_STATE):
      case(NO_FILE_STATE):
      case(STORE_FAILED_STATE):
      case(FATAL_STATE):
      case(SYSTEM_ERROR_STATE):
	 state=EOF_STATE;
	 break;
      case(NO_HOST_STATE):
      case(INITIAL_STATE):
      case(EOF_STATE):
      case(LOGIN_FAILED_STATE):
      case(LOOKUP_ERROR_STATE):
	 break;
      }
   }
   else
   {
      if(hostname)
	 state=INITIAL_STATE;
      else
	 state=NO_HOST_STATE;
   }
   super::Close();
}

int   Ftp::Read(void *buf,int size)
{
   int res,shift;

   Resume();
   Do();
   res=StateToError();
   if(res!=OK)
      return(res);

   if(mode==CLOSED)
      return(0);

   if(mode==CHANGE_DIR || mode==MAKE_DIR || mode==REMOVE_DIR || mode==REMOVE)
      abort();

   if(state==WAITING_STATE && RespQueueIsEmpty())
   {
      if(result_size==0)
      {
	 SwitchToState(EOF_STATE);
	 return(0);
      }
      if(result_size<size)
	 size=result_size;
      memcpy(buf,result,size);
      memmove(result,result+size,result_size-=size);
      return(size);
   }

read_again:
   if(state==DATA_OPEN_STATE)
   {
      if(data_sock==-1)
	 goto we_have_eof;

      if(RespQueueSize()>1 && real_pos==-1)
	 return(DO_AGAIN);

      struct pollfd pfd;
      pfd.fd=data_sock;
      pfd.events=POLLIN;
      res=poll(&pfd,1,0);
      if(res<=0)
	 return(DO_AGAIN);
      if(CheckHangup(&pfd,1))
	 return(DO_AGAIN);
      res=read(data_sock,buf,size);
      if(res==-1)
      {
	 if(errno==EAGAIN || errno==EINTR)
	    return DO_AGAIN;
	 if(NotSerious(errno))
	 {
	    Disconnect();
	    return DO_AGAIN;
	 }
	 SwitchToState(SYSTEM_ERROR_STATE);
	 return(StateToError());
      }
      if(res==0)
      {
      we_have_eof:
	 if(RespQueueIsEmpty())
	 {
	    SwitchToState(EOF_STATE);
	    return(0);
	 }
	 else
	 {
	    DataClose();
	    return DO_AGAIN;
	 }
      }
      retries=0;
      real_pos+=res;
      if(real_pos<=pos)
	 goto read_again;
      flags|=IO_FLAG;
      if((shift=pos+res-real_pos)>0)
      {
	 memmove(buf,(char*)buf+shift,size-shift);
	 res-=shift;
      }
      pos+=res;
      return(res);
   }
   return(DO_AGAIN);
}

/*
   Write - send data to ftp server

   * Uploading is not reliable in this realization *
   Well, not less reliable than in any usual ftp client.

   The reason for this is uncheckable receiving of data on the remote end.
   Since that, we have to leave re-putting up to user program
*/
int   Ftp::Write(const void *buf,int size)
{
   if(mode!=STORE)
      return(0);

   Resume();
   Do();
   int res=StateToError();
   if(res!=OK)
      return(res);

   if(state!=DATA_OPEN_STATE || (RespQueueSize()>1 && real_pos==-1))
      return DO_AGAIN;

   struct pollfd pfd;
   pfd.fd=data_sock;
   pfd.events=POLLOUT;
   res=poll(&pfd,1,0);
   if(res<=0)
      return(DO_AGAIN);
   if(CheckHangup(&pfd,1))
      return(DO_AGAIN);
   res=write(data_sock,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return DO_AGAIN;
      if(NotSerious(errno) || errno==EPIPE)
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      SwitchToState(SYSTEM_ERROR_STATE);
      return(StateToError());
   }
   retries=0;
   pos+=res;
   real_pos+=res;
   flags|=IO_FLAG;
   return(res);
}

int   Ftp::StoreStatus()
{
   int res=StateToError();
   if(res!=OK)
      return(res);

   if(mode!=STORE)
      return(OK);

   if(state==WAITING_STATE && RespQueueIsEmpty())
   {
      SwitchToState(EOF_STATE);
      return(OK);
   }
   if(state==DATA_OPEN_STATE)
   {
      // have not send EOT by SendEOT, do it now
      SendEOT();
   }
   return(IN_PROGRESS);
}

void  Ftp::SwitchToState(automate_state ns)
{
   if(ns==state)
      return;
   switch(ns)
   {
   case(INITIAL_STATE):
      Disconnect();
      break;
   case(FATAL_STATE):
   case(STORE_FAILED_STATE):
   case(LOGIN_FAILED_STATE):
      Disconnect();
      break;
   case(EOF_STATE):
      DataClose();
      xfree(file); file=0;
      set_idle_start();
      mode=CLOSED;
      break;
   case(NO_FILE_STATE):
      break;
   case(SYSTEM_ERROR_STATE):
      saved_errno=errno;
      Disconnect();
      break;
   case(LOOKUP_ERROR_STATE):
      break;
   default:
      fprintf(stderr,_("SwitchToState called with invalid state\n"));
      abort();
   }
   if(ns==STORE_FAILED_STATE && mode!=STORE)
      state=INITIAL_STATE;
   else
      state=ns;
}

int   Ftp::DataReady()
{
   int	 res;

   res=StateToError();
   if(res!=OK)
      return(1);  // say data ready and let'em get an error

   if(mode==CLOSED)
      return(1);

   if(state==WAITING_STATE)
      return(RespQueueIsEmpty());

   if(state==DATA_OPEN_STATE)
   {
      if(data_sock==-1)
	 return(1);  // eof
      if(real_pos==-1 && RespQueueSize()>1)
	 return(0);  // disallow reading/writing
      int ev=(mode==RETRIEVE?POLLIN:POLLOUT);
      if(Poll(data_sock,ev)&ev)
	 return(1);
   }
   return(0);
}

// Wait until requested data is available or operation is completed
int   Ftp::Block()
{
   int res;

   for(;;)
   {
      SMTask::Schedule();

      res=StateToError();
      if(res!=OK)
	 return res;

      if(DataReady())
	 return(OK);

      SMTask::Block();
   }
}

int   Ftp::ChdirStatus()
{
   int res=StateToError();
   if(res!=OK)
      return(res);

   if(mode!=CHANGE_DIR)
      return(OK);

   res=Read(NULL,0);
   if(res==DO_AGAIN)
      return(IN_PROGRESS);

   if(res==0)
      res=OK;

   Close();
   return(res);
}

void  Ftp::AddResp(int exp,int fail, int (Ftp::*ck)(int,int),bool log)
{
   int newtail=RQ_tail+1;
   if(newtail>RQ_alloc)
   {
      if(RQ_head-0<newtail-RQ_alloc)
	 RespQueue=(struct expected_response*)
	    xrealloc(RespQueue,(RQ_alloc=newtail+16)*sizeof(*RespQueue));
      memmove(RespQueue,RespQueue+RQ_head,(RQ_tail-RQ_head)*sizeof(*RespQueue));
      RQ_tail=0+(RQ_tail-RQ_head);
      RQ_head=0;
      newtail=RQ_tail+1;
   }
   RespQueue[RQ_tail].expect=exp;
   RespQueue[RQ_tail].fail_state=fail;
   RespQueue[RQ_tail].check_resp=ck;
   RespQueue[RQ_tail].log_resp=log;
   RQ_tail=newtail;
}

void  Ftp::EmptyRespQueue()
{
   RQ_head=RQ_tail=0;
   multiline_code=0;
   xfree(RespQueue);
   RespQueue=0;
   RQ_alloc=0;
}

int   Ftp::CheckResp(int act)
{
   int ns=-1;

   if(act==150 && mode==RETRIEVE && opt_size && *opt_size==-1)
   {
      // try to catch size
      char *s=strrchr(line,'(');
      if(s && isdigit(s[1]))
      {
	 *opt_size=atol(s+1);
      	 DebugPrint("---- ",_("saw file size in response"));
      }
   }

   if(act>=100 && act<200)	// intermediate responses are ignored
      return -1;

   if(act==421)  // timeout or something else
   {
      if(strstr(line,"Timeout"))
	 DebugPrint("**** ",_("remote timeout"));
      return(INITIAL_STATE);
   }

   if(RespQueueIsEmpty())
   {
      DebugPrint("**** ",_("extra server response"));
      return(INITIAL_STATE);
   }

   if(RespQueue[RQ_head].check_resp)
      ns=(this->*RespQueue[RQ_head].check_resp)(act,RespQueue[RQ_head].expect);
   if(ns==-1 && act!=RespQueue[RQ_head].expect)
      ns=RespQueue[RQ_head].fail_state;
   PopResp();
   return(ns);
}
void  Ftp::PopResp()
{
   if(RQ_head!=RQ_tail)
      RQ_head=RQ_head+1;
}

const char *Ftp::CurrentStatus()
{
   switch(state)
   {
   case(EOF_STATE):
   case(NO_FILE_STATE):
      if(control_sock!=-1)
      {
	 if(send_cmd_count>0)
	    return(_("Sending commands..."));
	 if(!RespQueueIsEmpty())
	    return(_("Waiting for response..."));
	 return(_("Connection idle"));
      }
      return(_("Not connected"));
   case(INITIAL_STATE):
      if(hostname)
      {
	 if(resolver)
	    return(_("Resolving host address..."));
	 time_t	t=time(NULL);
	 if(t-try_time<sleep_time)
	    return(_("Delaying before reconnect"));
      }
   case(NO_HOST_STATE):
      return(_("Not connected"));
   case(CONNECTING_STATE):
      return(_("Connecting..."));
   case(USER_RESP_WAITING_STATE):
      return(_("Logging in..."));
   case(DATASOCKET_CONNECTING_STATE):
      if(addr_received==0)
	 return(_("Waiting for response..."));
      return(_("Making data connection..."));
   case(CWD_CWD_WAITING_STATE):
      return(_("Changing remote directory..."));
   case(WAITING_STATE):
      if(mode==STORE)
	 return(_("Waiting for transfer to complete"));
      return(_("Waiting for response..."));
   case(ACCEPTING_STATE):
      return(_("Waiting for data connection..."));
   case(DATA_OPEN_STATE):
      if(data_sock!=-1)
         return(_("Data connection open"));
      return(_("Waiting for transfer to complete"));
   case(FATAL_STATE):
      return(_("Fatal protocol error occured"));
   case(STORE_FAILED_STATE):
      // this error is supposed to be handled by program
      return("Store failed - reput is needed");
   case(LOGIN_FAILED_STATE):
      return(_("Login failed"));
   case(SYSTEM_ERROR_STATE):
      return(strerror(saved_errno));
   case(LOOKUP_ERROR_STATE):
      return(StrError(LOOKUP_ERROR));
   }
   return("");
}

int   Ftp::StateToError()
{
   switch(state)
   {
   case(NO_FILE_STATE):
      return(NO_FILE);
   case(NO_HOST_STATE):
      return(NO_HOST);
   case(FATAL_STATE):
      return(FATAL);
   case(STORE_FAILED_STATE):
      return(STORE_FAILED);
   case(LOGIN_FAILED_STATE):
      return(LOGIN_FAILED);
   case(SYSTEM_ERROR_STATE):
      errno=saved_errno;
      return(SEE_ERRNO);
   case(LOOKUP_ERROR_STATE):
      return(LOOKUP_ERROR);
   default:
      return(OK);
   }
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

time_t	 Ftp::ConvertFtpDate(const char *s)
{
   struct tm tm;
   memset(&tm,0,sizeof(tm));

   int n=sscanf(s,"%4d%2d%2d%2d%2d%2d",&tm.tm_year,&tm.tm_mon,&tm.tm_mday,
				       &tm.tm_hour,&tm.tm_min,&tm.tm_sec);

   if(n!=6)
      return((time_t)-1);

   tm.tm_year-=1900;
   tm.tm_mon--;

   return mktime_from_utc(&tm);
}

void  Ftp::SetFlag(int flag,bool val)
{
   flag&=MODES_MASK;  // only certain flags can be changed
   if(val)
      flags|=flag;
   else
      flags&=~flag;

   if(!(flags&SYNC_MODE))
      flags&=~SYNC_WAIT;   // if SYNC_MODE is off, we don't need to wait
}

bool  Ftp::SameConnection(const Ftp *o)
{
   if(!strcmp(hostname,o->hostname) && port==o->port
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && !xstrcmp(group,o->group) && !xstrcmp(gpass,o->gpass)
   && (user || !xstrcmp(anon_user,o->anon_user))
   && (pass || !xstrcmp(anon_pass,o->anon_pass)))
      return true;
   return false;
}

bool  Ftp::SameLocationAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   Ftp *o=(Ftp*)fa;
   if(!hostname || !o->hostname)
      return false;
   if(SameConnection(o))
   {
      if(home && !o->home)
	 o->home=xstrdup(home);
      else if(!home && o->home)
	 home=xstrdup(o->home);

      if(home && xstrcmp(home,o->home))
	 return false;

      if(!cwd || !o->cwd)
	 return false;

      ExpandTildeInCWD();
      o->ExpandTildeInCWD();
      return !xstrcmp(cwd,o->cwd);
   }
   return false;
}

int   Ftp::Done()
{
   Resume();

   int res=StateToError();
   if(res!=OK)
      return(res);

   if(mode==CLOSED)
      return OK;

   if(mode==CHANGE_DIR || mode==RENAME || mode==ARRAY_INFO
   || mode==MAKE_DIR || mode==REMOVE_DIR || mode==REMOVE)
   {
      if(state==WAITING_STATE && RespQueueIsEmpty())
	 return(OK);
      return(IN_PROGRESS);
   }
   if(mode==CONNECT_VERIFY)
   {
      if(state!=INITIAL_STATE)
	 return OK;
      return(lookup_done?OK:IN_PROGRESS);
   }
   abort();
}

void Ftp::Connect(const char *new_host,int new_port)
{
   Close();
   xfree(hostname);
   hostname=xstrdup(new_host);
   port=new_port;
   xfree(cwd);
   cwd=xstrdup("~");
   xfree(home); home=0;
   flags=0;
   Reconfig();
   DontSleep();
   state=INITIAL_STATE;
   lookup_done=false;
   try_time=0;
   if(hostname[0]==0)
      state=NO_HOST_STATE; // no need to lookup
}

void Ftp::ConnectVerify()
{
   if(lookup_done)
      return;
   mode=CONNECT_VERIFY;
}

void Ftp::SetProxy(const char *px)
{
   xfree(proxy); proxy=0;
   proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;

   if(!px)
      return;

   ParsedURL url(px);
   if(!url.host || url.host[0]==0)
      return;

   // FIXME: check url.proto

   proxy=xstrdup(url.host);
   if(url.port)
      proxy_port=atoi(url.port);
   proxy_user=xstrdup(url.user);
   proxy_pass=xstrdup(url.pass);
   if(proxy_port==0)
      proxy_port=FTPPORT;
   lookup_done=false;
}

void Ftp::Reconfig()
{
   const char *c=hostname;

   SetFlag(SYNC_MODE,	res_sync_mode.Query(c));
   SetFlag(PASSIVE_MODE,res_passive_mode.Query(c));

   timeout = res_timeout.Query(c);
   sleep_time = res_redial_interval.Query(c);
   nop_interval = res_nop_interval.Query(c);
   idle = res_idle.Query(c);
   max_retries = res_max_retries.Query(c);
   relookup_always = res_relookup_always.Query(c);
   allow_skey = res_allow_skey.Query(c);
   force_skey = res_force_skey.Query(c);
   verify_data_address = res_address_verify.Query(c);
   verify_data_port = res_port_verify.Query(c);
   socket_buffer = res_socket_buffer.Query(c);

   xfree(anon_user);
   anon_user=xstrdup(res_anon_user.Query(c));
   xfree(anon_pass);
   anon_pass=xstrdup(res_anon_pass.Query(c));
   if(anon_user==0)
      anon_user=xstrdup(FTPUSER);
   if(anon_pass==0)
      anon_pass=xstrdup(DefaultAnonPass());

   SetProxy(res_proxy.Query(c));

   if(nop_interval<30)
      nop_interval=30;

   if(control_sock!=-1)
      SetSocketBuffer(control_sock);
   if(data_sock!=-1)
      SetSocketBuffer(data_sock);
}

void Ftp::Cleanup(bool all)
{
   if(!all && hostname==0)
      return;

   for(Ftp *o=ftp_chain; o!=0; o=o->ftp_next)
   {
      if(o->control_sock==-1 || o->mode!=CLOSED)
	 continue;
      if(all || !xstrcmp(hostname,o->hostname))
	 o->Disconnect();
   }
}

ListInfo *Ftp::MakeListInfo()
{
   return new FtpListInfo(this);
}


extern "C"
   const char *calculate_skey_response (int, const char *, const char *);

const char *Ftp::make_skey_reply()
{
   static const char * const skey_head[] = {
      "S/Key MD5 ",
      "s/key ",
      "opiekey ",
      "otp-md5 ",
      0
   };

   const char *cp;
   for(int i=0; ; i++)
   {
      if(skey_head[i]==0)
	 return 0;
      cp=strstr(result,skey_head[i]);
      if(cp)
      {
	 cp+=strlen(skey_head[i]);
	 break;
      }
   }

   DebugPrint("---- ","found s/key substring",9);

   int skey_sequence=0;
   char *buf=(char*)alloca(strlen(cp));

   if(sscanf(cp,"%d %s",&skey_sequence,buf)!=2 || skey_sequence<1)
      return 0;

   return calculate_skey_response(skey_sequence,buf,pass);
}

void Ftp::Login(const char *u,const char *p)
{
   if(u)
   {
      if(!strcasecmp(u,"ftp")
      || !strcasecmp(u,"anonymous"))
      {
	 xfree(anon_user);
	 anon_user=xstrdup(u);
	 u=0;
	 if(p)
	 {
	    xfree(anon_pass);
      	    anon_pass=xstrdup(p);
	    p=0;
	 }
      }
   }
   super::Login(u,p);
}

const char *Ftp::DefaultAnonPass()
{
   static char *pass=0;

   if(pass)
      return pass;

   struct passwd *pw=getpwuid(getuid());
   char *u=pw?pw->pw_name:"unknown";
   pass=(char*)xmalloc(strlen(u)+3);
   sprintf(pass,"-%s@",u);

   return pass;
}
