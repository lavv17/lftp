/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <assert.h>

#include "ftpclass.h"
#include "xstring.h"
#include "xmalloc.h"
#include "xalloca.h"
#include "url.h"
#include "FtpListInfo.h"
#include "FtpGlob.h"
#include "FtpDirList.h"
#include "log.h"
#include "FileCopyFtp.h"

#include "ascii_ctype.h"

enum {FTP_TYPE_A,FTP_TYPE_I};

#define TELNET_IAC	255		/* interpret as command: */
#define	TELNET_IP	244		/* interrupt process--permanently */
#define	TELNET_SYNCH	242		/* for telfunc calls */

#include <errno.h>
#include <time.h>

#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif

#include "xalloca.h"

#define FTPPORT "ftp"
#define FTP_DATA_PORT 20

#define super NetAccess
#define peer_sa (peer[peer_curr])

#ifdef TIOCOUTQ
static bool TIOCOUTQ_returns_free_space;
static bool TIOCOUTQ_works;
static void test_TIOCOUTQ()
{
   int sock=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
   if(sock==-1)
      return;
   int avail=-1;
   socklen_t len=sizeof(avail);
   if(getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&avail,&len)==-1)
      avail=-1;
   int buf=-1;
   if(ioctl(sock,TIOCOUTQ,&buf)==-1)
      buf=-1;
   if(buf>=0 && avail>0 && (buf==0 || buf==avail))
   {
      TIOCOUTQ_works=true;
      TIOCOUTQ_returns_free_space=(buf==avail);
   }
   close(sock);
}
#else
# define test_TIOCOUTQ()
#endif

FileAccess *Ftp::New() { return new Ftp(); }

void  Ftp::ClassInit()
{
   // register the class
   Register("ftp",Ftp::New);
   FileCopy::fxp_create=FileCopyFtp::New;

   test_TIOCOUTQ();
}

Ftp *Ftp::ftp_chain=0;


#if INET6

struct eprt_proto_match
{
   int proto;
   int eprt_proto;
};
static const eprt_proto_match eprt_proto[]=
{
   { AF_INET,  1 },
   { AF_INET6, 2 },
   { -1, -1 }
};

const char *Ftp::encode_eprt(sockaddr_u *a)
{
   char host[NI_MAXHOST];
   char serv[NI_MAXSERV];
   static char *eprt=0;

   int proto=-1;
   for(const eprt_proto_match *p=eprt_proto; p->proto!=-1; p++)
   {
      if(a->sa.sa_family==p->proto)
      {
	 proto=p->eprt_proto;
	 break;
      }
   }
   if(proto==-1)
      return 0;

   if(getnameinfo(&a->sa, sizeof(*a), host, sizeof(host), serv, sizeof(serv),
      NI_NUMERICHOST | NI_NUMERICSERV) < 0)
   {
      return 0;
   }
   eprt=(char*)xrealloc(eprt,20+strlen(host)+strlen(serv));
   sprintf(eprt, "|%d|%s|%s|", proto, host, serv);
   return eprt;
}
#endif

bool Ftp::data_address_ok(sockaddr_u *dp,bool verify_this_data_port)
{
   sockaddr_u d;
   sockaddr_u c;
   socklen_t len;
   len=sizeof(d);
   if(dp)
      d=*dp;
   else if(getpeername(data_sock,&d.sa,&len)==-1)
   {
      perror("getpeername(data_sock)");
      return false;
   }
   len=sizeof(c);
   if(getpeername(control_sock,&c.sa,&len)==-1)
   {
      perror("getpeername(control_sock)");
      return false;
   }

#if INET6
   if(d.sa.sa_family==AF_INET && c.sa.sa_family==AF_INET6
      && IN6_IS_ADDR_V4MAPPED(&c.in6.sin6_addr))
   {
      if(memcmp(&d.in.sin_addr,&c.in6.sin6_addr.s6_addr[12],4))
	 goto address_mismatch;
      if(d.in.sin_port!=htons(FTP_DATA_PORT))
	 goto wrong_port;
   }
#endif

   if(d.sa.sa_family!=c.sa.sa_family)
      return false;
   if(d.sa.sa_family==AF_INET)
   {
      if(memcmp(&d.in.sin_addr,&c.in.sin_addr,sizeof(d.in.sin_addr)))
	 goto address_mismatch;
      if(d.in.sin_port!=htons(FTP_DATA_PORT))
	 goto wrong_port;
      return true;
   }
#if INET6
# ifndef  IN6_ARE_ADDR_EQUAL
#  define IN6_ARE_ADDR_EQUAL(a,b) (!memcmp((a),(b),16))
# endif
   if(d.sa.sa_family==AF_INET6)
   {
      if(!IN6_ARE_ADDR_EQUAL(&d.in6.sin6_addr,&c.in6.sin6_addr))
	 goto address_mismatch;
      if(d.in6.sin6_port!=htons(FTP_DATA_PORT))
         goto wrong_port;
      return true;
   }
#endif
   return true;

wrong_port:
   if(!verify_this_data_port || !verify_data_port)
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

int   Ftp::RestCheck(int act,int exp)
{
   (void)exp;
   if(act/100==5 && pos>0)
   {
      DebugPrint("---- ","Switching to NOREST mode",2);
      flags|=NOREST_MODE;
      real_pos=0;
      if(mode==STORE)
	 pos=0;
      if(copy_mode!=COPY_NONE)
	 return COPY_FAILED;
      return(state);
   }
   if(act/100==4)
   {
      // strange, but possible
      return -1;
   }
   real_pos=pos;  // REST successful
   last_rest=pos;
   return state;
}

int   Ftp::NoFileCheck(int act,int exp)
{
   (void)exp;
   if(act==RESP_NOT_IMPLEMENTED || act==RESP_NOT_UNDERSTOOD)
      return(FATAL_STATE);
   if(act/100==5)
   {
      // retry on these errors (ftp server ought to send 4xx code instead)
      if((strstr(line,"Broken pipe") && (!file || !strstr(file,"Broken pipe")))
      || (strstr(line,"Too many")    && (!file || !strstr(file,"Too many")))
      || (strstr(line,"timed out")   && (!file || !strstr(file,"timed out")))
      // if there were some data received, assume it is temporary error.
      || (flags&IO_FLAG))
      {
	 if(copy_mode!=COPY_NONE)
	    return COPY_FAILED;
	 return INITIAL_STATE;
      }
      if(real_pos>0 && !(flags&IO_FLAG) && copy_mode==COPY_NONE)
      {
	 DataClose();
	 DebugPrint("---- ","Switching to NOREST mode",2);
	 flags|=NOREST_MODE;
	 real_pos=0;
	 if(mode==STORE)
	    pos=0;
	 state=EOF_STATE;
	 return(state);
      }
      return(NO_FILE_STATE);
   }
   if(act/100!=exp/100 && copy_mode!=COPY_NONE)
      return COPY_FAILED;
   return(-1);
}

int   Ftp::TransferCheck(int act,int exp)
{
   (void)exp;
   if(act==225 || act==226) // data connection is still open or ABOR worked.
   {
      copy_done=true;
      AbortedClose();
   }
   if(act==211)
   {
      // permature STAT?
      stat_time=now+3;
      return state;
   }
   if(act==213)	  // this must be STAT reply.
   {
      stat_time=now;
      // find the number.
      long p;
      for(char *b=line+4; ; b++)
      {
	 if(*b==0)
	    return state;
	 if(!is_ascii_digit(*b))
	    continue;
	 if(sscanf(b,"%ld",&p)==1)
	    break;
      }
      if(copy_mode==COPY_DEST)
	 real_pos=pos=p;
      return state;
   }
   if(copy_mode!=COPY_NONE && act==425 && strstr(line,"port theft"))
   {
      copy_passive=!copy_passive;
      return COPY_FAILED;
   }
   if(act==RESP_NO_FILE && mode==LIST)
   {
      DataClose();
      return(DATA_OPEN_STATE); // simulate eof
   }
   if(act==RESP_BROKEN_PIPE && copy_mode==COPY_NONE)
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
   if(act==530) // login incorrect or overloaded server
   {
      if(strstr(line,"Login incorrect")) // Don't translate!!!
      {
	 if(!user)   // unusual message for anonymous user
	    goto def_ret;
	 DebugPrint("---- ",_("Saw `Login incorrect', assume failed login"));
	 return(LOGIN_FAILED_STATE);
      }
      return(-1);
   }
   if(act/100==5)
      return(LOGIN_FAILED_STATE);

def_ret:
   if(act/100!=exp/100)
      try_time=now;	// count the reconnect-interval from this moment
   return(-1);
}

void Ftp::FreeResult()
{
   xfree(result);
   result=0;
   result_size=0;
}

int   Ftp::NoPassReqCheck(int act,int exp) // for USER command
{
   (void)exp;
   if(act/100==RESP_LOGGED_IN/100) // in some cases, ftpd does not ask for pass.
   {
      ignore_pass=true;
      return(state);
   }
   if(act==530)	  // no such user or overloaded server
   {
      // Unfortunately, at this point we cannot tell if it is
      // really incorrect login or overloaded server, because
      // many overloaded servers return hard error 530...
      // So try to check the message for `user unknown'.
      // NOTE: modern ftp servers don't say `user unknown', they wait for
      // PASS and then say `Login incorrect'.
      if(strstr(line,"unknown")) // Don't translate!!!
      {
	 DebugPrint("---- ",_("Saw `unknown', assume failed login"));
	 return(LOGIN_FAILED_STATE);
      }
      try_time=now;	// count the reconnect-interval from this moment
      return -1;
   }
   if(act/100==5)
   {
      // proxy interprets USER as user@host, so we check for host name validity
      if(proxy && (strstr(line,"host") || strstr(line,"resolve")))
      {
	 DebugPrint("---- ",_("assuming failed host name lookup"));
	 SetError(LOOKUP_ERROR,line);
	 Disconnect();
	 return(LOOKUP_ERROR_STATE);
      }
      return(LOGIN_FAILED_STATE);
   }
   if(act==331 && allow_skey && user && pass && result)
   {
      skey_pass=xstrdup(make_skey_reply());
      FreeResult();
      if(force_skey && skey_pass==0)
      {
	 // FIXME - make proper err msg
	 return(LOGIN_FAILED_STATE);
      }
   }
   if(act/100==3)
      return state;
   try_time=now;	// count the reconnect-interval from this moment
   return(-1);
}

int   Ftp::proxy_LoginCheck(int act,int exp)
{
   (void)exp;
   if(act==RESP_LOGIN_FAILED)
   {
      if(strstr(line,"Login incorrect")) // Don't translate!!!
      {
	 DebugPrint("---- ",_("Saw `Login incorrect', assume failed login"));
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
	 DebugPrint("---- ",_("Saw `unknown', assume failed login"));
	 return(LOGIN_FAILED_STATE);
      }
      return -1;
   }
   if(act/100==5)
      return(LOGIN_FAILED_STATE);
   return(-1);
}

static void normalize_path_vms(char *path)
{
   for(char *s=path; *s; s++)
      *s=to_ascii_lower(*s);
   char *colon=strchr(path,':');
   if(colon)
   {
      memmove(path+1,path,strlen(path)+1);
      path[0]='/';
      path=colon+1;
      if(path[1]=='[')
	 memmove(path,path+1,strlen(path));
   }
   else
   {
      path=strchr(path,'[');
      if(!*path)
	 return;
   }
   *path++='/';
   while(*path && *path!=']')
   {
      if(*path=='.')
	 *path='/';
      path++;
   }
   if(!*path)
      return;
   if(path[1])
      *path='/';
   else
      *path=0;
}

char *Ftp::ExtractPWD()
{
   char *pwd=string_alloca(strlen(line)+1);

   if(sscanf(line,"%*d \"%[^\"]\"",pwd)!=1)
      return 0;

   if(pwd[0]==0)
      return 0;	  // empty home not allowed.

   int dev_len=device_prefix_len(pwd);
   if(pwd[dev_len]=='[')
   {
      vms_path=true;
      normalize_path_vms(pwd);
   }
   else if(dev_len==2 || dev_len==3)
   {
      dos_path=true;
   }

   if(!strchr(pwd,'/') || dos_path)
   {
      // for safety -- against dosish ftpd
      for(char *s=pwd; *s; s++)
	 if(*s=='\\')
	    *s='/';
   }
   return xstrdup(pwd);
}

int   Ftp::Handle_PASV()
{
   unsigned a0,a1,a2,a3,p0,p1;
   /*
    * Extract address. RFC1123 says:
    * "...must scan the reply for the first digit..."
    */
   for(char *b=line+4; ; b++)
   {
      if(*b==0)
	 return INITIAL_STATE;
      if(!is_ascii_digit(*b))
	 continue;
      if(sscanf(b,"%u,%u,%u,%u,%u,%u",&a0,&a1,&a2,&a3,&p0,&p1)==6)
         break;
   }
   unsigned char *a,*p;
   data_sa.sa.sa_family=peer_sa.sa.sa_family;
   if(data_sa.sa.sa_family==AF_INET)
   {
      a=(unsigned char*)&data_sa.in.sin_addr;
      p=(unsigned char*)&data_sa.in.sin_port;
   }
#if INET6
   else if(data_sa.sa.sa_family==AF_INET6)
   {
      a=((unsigned char*)&data_sa.in6.sin6_addr)+12;
      a[-1]=a[-2]=0xff; // V4MAPPED
      p=(unsigned char*)&data_sa.in6.sin6_port;
   }
#endif
   else
      return INITIAL_STATE;

   if(a0==0 && a1==0 && a2==0 && a3==0)
   {
      // broken server, try to fix up
      if(data_sa.sa.sa_family==AF_INET)
	 memcpy(a,&peer_sa.in.sin_addr,sizeof(peer_sa.in.sin_addr));
#if INET6
      else if(data_sa.in.sin_family==AF_INET6)	// peer_sa should be V4MAPPED
	 memcpy(a,&peer_sa.in6.sin6_addr.s6_addr[12],4);
#endif
   }
   else
   {
      a[0]=a0; a[1]=a1; a[2]=a2; a[3]=a3;
   }
   p[0]=p0; p[1]=p1;
   return state;
}

int   Ftp::Handle_EPSV()
{
   char delim;
   char *format=alloca_strdup("|||%u|");
   unsigned port;
   char *c;

   c=strchr(line,'(');
   c=c?c+1:line+4;
   delim=*c;

   for(char *p=format; *p; p++)
      if(*p=='|')
	 *p=delim;

   if(sscanf(c,format,&port)!=1)
   {
      DebugPrint("**** ","cannot parse EPSV response",0);
      return INITIAL_STATE;
   }

   socklen_t len=sizeof(data_sa);
   getpeername(control_sock,&data_sa.sa,&len);
   if(data_sa.sa.sa_family==AF_INET)
      data_sa.in.sin_port=htons(port);
#if INET6
   else if(data_sa.sa.sa_family==AF_INET6)
      data_sa.in6.sin6_port=htons(port);
#endif
   else
      return INITIAL_STATE;

   return state;
}

int   Ftp::CatchDATE(int act,int)
{
   if(!array_for_info)
      return state;

   if(act/100==2)
   {
      if(strlen(line)>4 && is_ascii_digit(line[4]))
	 array_for_info[array_ptr].time=ConvertFtpDate(line+4);
      else
	 array_for_info[array_ptr].time=NO_DATE;
   }
   else if(act/100!=5)
      return -1;
   else
   {
      if(act==500 || act==502)
	 mdtm_supported=false;
      array_for_info[array_ptr].time=NO_DATE;
   }

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

   if(act/100==2 && strlen(line)>4 && is_ascii_digit(line[4]))
   {
      *opt_date=ConvertFtpDate(line+4);
      opt_date=0;
   }
   else
   {
      if(act==500 || act==502)
	 mdtm_supported=false;
      *opt_date=NO_DATE;
   }
   return state;
}

int   Ftp::CatchSIZE(int act,int)
{
   if(!array_for_info)
      return state;

   if(act/100==2)
   {
      if(strlen(line)>4 && is_ascii_digit(line[4]))
	 array_for_info[array_ptr].size=atol(line+4);
      else
	 array_for_info[array_ptr].size=-1;
   }
   else if(act/100!=5)
      return -1;
   else
   {
      if(act==500 || act==502)
	 size_supported=false;
      array_for_info[array_ptr].size=-1;
   }

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

   if(act/100==2 && strlen(line)>4 && is_ascii_digit(line[4]))
   {
      *opt_size=atol(line+4);
      opt_size=0;
   }
   else
   {
      if(act==500 || act==502)
	 size_supported=false;
      *opt_size=-1;
   }
   return state;
}


void Ftp::InitFtp()
{
   UpdateNow();

   control_sock=-1;
   data_sock=-1;
   aborted_data_sock=-1;

   nop_time=0;
   nop_count=0;
   nop_offset=0;
   resp=0;
   resp_size=0;
   resp_alloc=0;
   sync_wait=0;
   line=0;
   type=FTP_TYPE_A;
   send_cmd_buffer=0;
   send_cmd_alloc=0;
   send_cmd_count=0;
   send_cmd_ptr=0;
   result=NULL;
   result_size=0;
   state=NO_HOST_STATE;
   flags=SYNC_MODE;
   wait_flush=false;
   ignore_pass=false;
   skey_pass=0;
   allow_skey=true;
   force_skey=false;
   verify_data_address=true;
   list_options=0;
   use_stat=true;
   stat_interval=1;

   dos_path=false;
   vms_path=false;
   mdtm_supported=true;
   size_supported=true;
   site_chmod_supported=true;
   last_rest=0;

   RespQueue=0;
   RQ_alloc=0;
   EmptyRespQueue();

   anon_pass=0;
   anon_user=0;	  // will be set by Reconfig()

   ftp_next=ftp_chain;
   ftp_chain=this;

   copy_mode=COPY_NONE;
   copy_addr_valid=false;
   copy_passive=false;
   copy_done=false;
   copy_connection_open=false;
   stat_time=0;
   copy_allow_store=false;

   memset(&data_sa,0,sizeof(data_sa));

   Reconfig();
}
Ftp::Ftp() : super()
{
   InitFtp();
}
Ftp::Ftp(const Ftp *f) : super(f)
{
   InitFtp();

   if(f->state!=NO_HOST_STATE)
      state=INITIAL_STATE;
   flags=f->flags&MODES_MASK;
}

Ftp::~Ftp()
{
   Close();
   Disconnect();

   xfree(anon_user); anon_user=0;
   xfree(anon_pass); anon_pass=0;
   xfree(list_options);
   xfree(line); line=0;
   xfree(resp); resp=0;

   xfree(RespQueue); RespQueue=0;
   xfree(send_cmd_buffer); send_cmd_buffer=0;

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

bool Ftp::AbsolutePath(const char *s)
{
   if(!s)
      return false;
   int dev_len=device_prefix_len(s);
   return(s[0]=='/'
      || (((dos_path && dev_len==3) || (vms_path && dev_len>2))
	  && s[dev_len-1]=='/'));
}

void  Ftp::GetBetterConnection(int level)
{
   if(level==0 && cwd==0)
      return;

   for(Ftp *o=ftp_chain; o!=0; o=o->ftp_next)
   {
      if(o==this)
	 continue;
      if(o->control_sock==-1)
	 continue;
      if(!SameConnection(o))
	 continue;

      if(o->data_sock!=-1 || o->state!=EOF_STATE || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || o->priority>=priority)
	    continue;
	 if(o->data_sock!=-1 && o->RespQueueSize()<=1)
	 {
	    if((o->flags&NOREST_MODE) && o->real_pos>0x1000)
	       continue;
	    o->DataAbort();
	    o->DataClose();
	    if(o->control_sock==-1)
	       continue; // oops...
	 }
	 else
	 {
	    if(o->RespQueueSize()>0)
	       continue;
	 }
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

      if(home && !o->home)
      {
	 o->home=xstrdup(home);
	 o->dos_path=dos_path;
	 o->vms_path=vms_path;
      }
      else if(!home && o->home)
      {
	 home=xstrdup(o->home);
	 dos_path=o->dos_path;
	 vms_path=o->vms_path;
      }

      o->ExpandTildeInCWD();
      ExpandTildeInCWD();

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      // so borrow the connection
      MoveConnectionHere(o);
      return;
   }
}

int   Ftp::Do()
{
   char	 *str =(char*)alloca(xstrlen(cwd)+xstrlen(hostname)+xstrlen(proxy)+256);
   char	 *str1=(char*)alloca(xstrlen(file)+xstrlen(list_options)+20);
   int   res;
   socklen_t addr_len;
   unsigned char *a;
   unsigned char *p;
   automate_state oldstate;
   int	 m=STALL;

   // check if idle time exceeded
   if(mode==CLOSED && control_sock!=-1 && idle>0)
   {
      if(now-idle_start>=idle)
      {
	 DebugPrint("---- ",_("Closing idle connection"),1);
	 Disconnect();
	 return m;
      }
      TimeoutS(idle_start+idle-now);
   }

   if(Error())
      return m;

   if(!hostname)
      return m;

   switch(state)
   {
   case(INITIAL_STATE):
   {
      if(mode==CLOSED)
	 return m;

      // walk through ftp classes and try to find identical idle ftp session
      // first try "easy" cases of session take-over.
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0
		  || connection_limit>CountConnections()))
	    break;
	 GetBetterConnection(i);
	 if(state!=INITIAL_STATE)
	    return MOVED;
      }

      if(!ReconnectAllowed())
	 return m;

      if(Resolve(FTPPORT,"ftp","tcp")==MOVED)
	 m=MOVED;
      if(!peer)
	 return m;

      if(mode==CONNECT_VERIFY)
	 return m;

      if(!NextTry())
	 return MOVED;

      control_sock=socket(peer_sa.sa.sa_family,SOCK_STREAM,IPPROTO_TCP);
      if(control_sock==-1)
      {
	 if(peer_curr+1<peer_num)
	 {
	    peer_curr++;
	    retries--;
	    return MOVED;
	 }
	 sprintf(str,"socket: %s",strerror(errno));
         DebugPrint("**** ",str,0);
	 if(errno==ENFILE || errno==EMFILE)
	 {
	    // file table overflow - it could free sometime
	    TimeoutS(1);
	    return m;
	 }
	 sprintf(str,"cannot create socket of address family %d",
			peer_sa.sa.sa_family);
	 SetError(SEE_ERRNO,str);
	 return MOVED;
      }
      KeepAlive(control_sock);
      SetSocketBuffer(control_sock);
      SetSocketMaxseg(control_sock);
      NonBlock(control_sock);
      CloseOnExec(control_sock);

      SayConnectingTo();

      res=SocketConnect(control_sock,&peer_sa);
      if(res==-1
#ifdef EINPROGRESS
      && errno!=EINPROGRESS
#endif
      )
      {
	 sprintf(str,"connect: %s",strerror(errno));
         DebugPrint("**** ",str,0);
         close(control_sock);
	 control_sock=-1;
	 NextPeer();
	 if(NotSerious(errno))
	    return MOVED;
	 goto system_error;
      }
      state=CONNECTING_STATE;
      m=MOVED;
      event_time=now;

      // reset *_supported on reconnect - we can possibly connect
      // to a different server in fact.
      size_supported=true;
      mdtm_supported=true;
      site_chmod_supported=true;
      last_rest=0;
   }

   case(CONNECTING_STATE):
   {
      res=Poll(control_sock,POLLOUT);
      if(res==-1)
      {
	 Disconnect();
	 return MOVED;
      }
      if(!(res&POLLOUT))
	 goto usual_return;

      sync_wait=1; // we need to wait for RESP_READY

      AddResp(RESP_READY,INITIAL_STATE,CHECK_READY);

      char *user_to_use=(user?user:anon_user);
      if(proxy)
      {
	 char *combined=(char*)alloca(strlen(user_to_use)+1+strlen(hostname)+1+xstrlen(portname)+1);
	 sprintf(combined,"%s@%s",user_to_use,hostname);
	 if(portname)
	    sprintf(combined+strlen(combined),":%s",portname);
	 user_to_use=combined;

      	 if(proxy_user && proxy_pass)
	 {
	    AddResp(RESP_PASS_REQ,INITIAL_STATE,CHECK_USER_PROXY);
	    SendCmd2("USER",proxy_user);
	    AddResp(RESP_LOGGED_IN,INITIAL_STATE,CHECK_PASS_PROXY);
	    SendCmd2("PASS",proxy_pass);
	 }
      }

      xfree(skey_pass);
      skey_pass=0;

      ignore_pass=false;
      AddResp(RESP_PASS_REQ,INITIAL_STATE,CHECK_USER,allow_skey);
      SendCmd2("USER",user_to_use);

      state=USER_RESP_WAITING_STATE;
      m=MOVED;
   }

   case(USER_RESP_WAITING_STATE):
      if(((flags&SYNC_MODE) || (user && pass && allow_skey))
      && !RespQueueIsEmpty())
      {
	 m|=FlushSendQueue();
	 m|=ReceiveResp();
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
	 AddResp(RESP_LOGGED_IN,INITIAL_STATE,CHECK_PASS);
	 SendCmd2("PASS",pass_to_use);
      }

      // FIXME: site group/site gpass

      if(!home)
      {
	 // if we don't yet know the home location, try to get it
	 SendCmd("PWD");
	 AddResp(RESP_PWD_MKD_OK,INITIAL_STATE,CHECK_PWD);
      }

      set_real_cwd("~");   // starting point
      type=FTP_TYPE_A;	   // just after login we are in TEXT mode

      state=EOF_STATE;
      m=MOVED;

   case(EOF_STATE):
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=EOF_STATE)
	 return MOVED;

      if(mode==CLOSED || mode==CONNECT_VERIFY)
	 goto notimeout_return;

      if(mode==CHANGE_MODE && !site_chmod_supported)
      {
	 SetError(NO_FILE,_("SITE CHMOD is not supported by this site"));
	 state=NO_FILE_STATE;
	 return MOVED;
      }

      assert(peer!=0);
      assert(peer_curr<peer_num);

      if(home==0 && !RespQueueIsEmpty())
	 goto usual_return;

      ExpandTildeInCWD();

      if(mode!=CHANGE_DIR)
      {
	 expected_response *last_cwd=FindLastCWD();
	 if((!last_cwd && xstrcmp(cwd,real_cwd))
	    || (last_cwd && xstrcmp(last_cwd->path,cwd)))
	 {
	    SendCmd2("CWD",cwd);
	    AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,CHECK_CWD_CURR);
	    SetRespPath(cwd);
	 }
	 else if(last_cwd && !xstrcmp(last_cwd->path,cwd))
	 {
	    // no need for extra CWD, one's already sent.
	    last_cwd->check_case=CHECK_CWD_CURR;
	 }
      }
      state=CWD_CWD_WAITING_STATE;
      m=MOVED;

   case CWD_CWD_WAITING_STATE:
   {
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=CWD_CWD_WAITING_STATE)
	 return MOVED;

      // wait for all CWD to finish
      if(mode!=CHANGE_DIR && FindLastCWD())
	 goto usual_return;

      // address of peer is not known yet
      if(copy_mode!=COPY_NONE && !copy_passive && !copy_addr_valid)
	 goto usual_return;

      if(mode==STORE && (flags&NOREST_MODE) && pos>0)
	 pos=0;

      if(copy_mode==COPY_NONE
      && (mode==RETRIEVE || mode==STORE || mode==LIST || mode==LONG_LIST))
      {
	 assert(data_sock==-1);
	 data_sock=socket(peer_sa.sa.sa_family,SOCK_STREAM,IPPROTO_TCP);
	 if(data_sock==-1)
	 {
	    sprintf(str,"socket(data): %s",strerror(errno));
	    DebugPrint("**** ",str,0);
	    goto system_error;
	 }
   	 NonBlock(data_sock);
	 CloseOnExec(data_sock);
	 KeepAlive(data_sock);
	 SetSocketBuffer(data_sock);
	 SetSocketMaxseg(data_sock);
      }

      int old_type=type;
      if((flags&NOREST_MODE) || pos==0)
	 real_pos=0;
      else
	 real_pos=-1;	// we don't yet know if REST will succeed

      flags&=~IO_FLAG;

      switch((enum open_mode)mode)
      {
      case(RETRIEVE):
         type=FTP_TYPE_I;
         sprintf(str1,"RETR %s\n",file);
         break;
      case(STORE):
         type=FTP_TYPE_I;
	 if(!(bool)Query("rest-stor",hostname))
	    real_pos=0;	// some old servers don't handle REST/STOR properly.
         sprintf(str1,"STOR %s\n",file);
         break;
      case(LONG_LIST):
         type=FTP_TYPE_A;
         if(!rest_list)
	    real_pos=0;	// some ftp servers do not do REST/LIST.
	 strcpy(str1,"LIST");
	 if(list_options && list_options[0])
	 {
	    strcat(str1," ");
	    strcat(str1,list_options);
	 }
	 if(file && file[0])
	 {
	    strcat(str1," ");
            strcat(str1,file);
	 }
	 strcat(str1,"\n");
         break;
      case(LIST):
         type=FTP_TYPE_A;
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
	    int len=xstrlen(real_cwd);
	    if(!vms_path && !AbsolutePath(file) && real_cwd
	    && !strncmp(file,real_cwd,len) && file[len]=='/')
	       sprintf(str1,"CWD .%s\n",file+len);
	    else
	       sprintf(str1,"CWD %s\n",file);
	    SendCmd(str1);
	    AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,CHECK_CWD);
	    SetRespPath(file);
	 }
	 goto pre_WAITING_STATE;
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
	 real_pos=0;
	 sprintf(str1,"%s\n",file);
	 break;
      case(RENAME):
	 sprintf(str1,"RNFR %s\n",file);
	 break;
      case(ARRAY_INFO):
	 type=FTP_TYPE_I;
	 break;
      case(CHANGE_MODE):
	 sprintf(str1,"SITE CHMOD %03o %s\n",chmod_mode,file);
	 break;
      case(CONNECT_VERIFY):
      case(CLOSED):
	 abort(); // can't happen
      }
      if(ascii)
	 type=FTP_TYPE_A;
      if(old_type!=type)
      {
         strcpy(str,type==FTP_TYPE_I?"TYPE I\n":"TYPE A\n");
	 SendCmd(str);
	 AddResp(RESP_TYPE_OK,INITIAL_STATE);
      }

      if(opt_size && size_supported)
      {
	 SendCmd2("SIZE",file);
	 AddResp(RESP_RESULT_HERE,INITIAL_STATE,CHECK_SIZE_OPT);
      }
      if(opt_date && mdtm_supported)
      {
	 SendCmd2("MDTM",file);
	 AddResp(RESP_RESULT_HERE,INITIAL_STATE,CHECK_MDTM_OPT);
      }

      if(mode==ARRAY_INFO)
      {
      array_info_send_more:
	 for(int i=array_ptr; i<array_cnt; i++)
	 {
	    bool sent=false;
	    if(array_for_info[i].get_time && mdtm_supported)
	    {
	       SendCmd2("MDTM",ExpandTildeStatic(array_for_info[i].file));
	       AddResp(RESP_RESULT_HERE,INITIAL_STATE,CHECK_MDTM);
	       sent=true;
	    }
	    else
	    {
	       array_for_info[i].time=NO_DATE;
	    }
	    if(array_for_info[i].get_size && size_supported)
	    {
	       SendCmd2("SIZE",ExpandTildeStatic(array_for_info[i].file));
	       AddResp(RESP_RESULT_HERE,INITIAL_STATE,CHECK_SIZE);
	       sent=true;
	    }
	    else
	    {
	       array_for_info[i].size=-1;
	    }
	    if(!sent)
	    {
	       if(i==array_ptr)
		  array_ptr++;	 // if it is first one, just skip it.
	       else
		  break;	 // otherwise, wait until it is first.
	    }
	 }
	 goto pre_WAITING_STATE;
      }

      if(mode==QUOTE_CMD || mode==CHANGE_MODE
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
		  SendCmd2("MKD",file);
		  AddResp(0,INITIAL_STATE,CHECK_IGNORE);
		  *sl='/';
	       }
	       sl=strchr(sl+1,'/');
	    }
	 }
	 SendCmd(str1);
	 if(mode==REMOVE_DIR || mode==REMOVE)
	    AddResp(RESP_CWD_RMD_DELE_OK,INITIAL_STATE,CHECK_FILE_ACCESS);
	 else if(mode==MAKE_DIR)
	    AddResp(RESP_PWD_MKD_OK,INITIAL_STATE,CHECK_FILE_ACCESS);
	 else if(mode==QUOTE_CMD)
	    AddResp(0,INITIAL_STATE,CHECK_IGNORE,true);
	 else if(mode==RENAME)
	    AddResp(350,INITIAL_STATE,CHECK_RNFR);
	 else
	    AddResp(200,INITIAL_STATE,CHECK_FILE_ACCESS);

	 FreeResult();
	 goto pre_WAITING_STATE;
      }

      if((copy_mode==COPY_NONE && (flags&PASSIVE_MODE))
      || (copy_mode!=COPY_NONE && copy_passive))
      {
	 if(peer_sa.sa.sa_family==AF_INET)
	 {
	    goto ipv4_pasv;   // make it used
	 ipv4_pasv:
	    SendCmd("PASV");
	    AddResp(227,INITIAL_STATE,CHECK_PASV);
	    addr_received=0;
	 }
	 else
	 {
#if INET6
	    if(peer_sa.sa.sa_family==AF_INET6
	    && IN6_IS_ADDR_V4MAPPED(&peer_sa.in6.sin6_addr))
	       goto ipv4_pasv;
	    SendCmd("EPSV");
	    AddResp(229,INITIAL_STATE,CHECK_EPSV);
#else
	    Fatal("unsupported network protocol");
	    return MOVED;
#endif
	 }
      }
      else
      {
	 addr_len=sizeof(data_sa);
	 if(copy_mode!=COPY_NONE)
	    data_sa=copy_addr;
	 else
	 {
	    getsockname(control_sock,&data_sa.sa,&addr_len);
	    if(data_sa.sa.sa_family==AF_INET)
	       data_sa.in.sin_port=0;
#if INET6
	    else if(data_sa.sa.sa_family==AF_INET6)
	       data_sa.in6.sin6_port=0;
#endif
	    else
	    {
	       Fatal("unsupported network protocol");
	       return MOVED;
	    }
	    bind(data_sock,&data_sa.sa,addr_len);
	    // get the port allocated
	    getsockname(data_sock,&data_sa.sa,&addr_len);
	    listen(data_sock,1);
	 }
	 if(data_sa.sa.sa_family==AF_INET)
	 {
	    a=(unsigned char*)&data_sa.in.sin_addr;
	    p=(unsigned char*)&data_sa.in.sin_port;
	    goto ipv4_port;   // make it used
	 ipv4_port:
	    sprintf(str,"PORT %d,%d,%d,%d,%d,%d\n",a[0],a[1],a[2],a[3],p[0],p[1]);
	    SendCmd(str);
	    AddResp(RESP_PORT_OK,INITIAL_STATE,CHECK_PORT);
	 }
	 else
	 {
#if INET6
	    if(data_sa.sa.sa_family==AF_INET6
	       && IN6_IS_ADDR_V4MAPPED(&data_sa.in6.sin6_addr))
	    {
	       a=((unsigned char*)&data_sa.in6.sin6_addr)+12;
	       p=(unsigned char*)&data_sa.in6.sin6_port;
	       goto ipv4_port;
	    }
	    SendCmd2("EPRT",encode_eprt(&data_sa));
	    AddResp(RESP_PORT_OK,INITIAL_STATE,CHECK_PORT);
#else
	    Fatal("unsupported network protocol");
	    return MOVED;
#endif
	 }
      }
      // some broken servers don't reset REST after a transfer,
      // so check if last_rest was different.
      if(real_pos==-1 || last_rest!=real_pos)
      {
	 real_pos=-1;
         sprintf(str,"REST %ld\n",pos);
	 SendCmd(str);
	 AddResp(RESP_REST_OK,INITIAL_STATE,CHECK_REST);
      }
      if(copy_mode!=COPY_DEST || copy_allow_store)
      {
	 SendCmd(str1);
	 AddResp(RESP_TRANSFER_OK,mode==STORE?STORE_FAILED_STATE:INITIAL_STATE,
		  CHECK_TRANSFER);
      }
      m=MOVED;
      if(copy_mode!=COPY_NONE && !copy_passive)
	 goto pre_WAITING_STATE;
      if((copy_mode==COPY_NONE && (flags&PASSIVE_MODE))
      || (copy_mode!=COPY_NONE && copy_passive))
      {
	 state=DATASOCKET_CONNECTING_STATE;
      	 goto datasocket_connecting_state;
      }
      state=ACCEPTING_STATE;
   }
   case(ACCEPTING_STATE):
      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=ACCEPTING_STATE)
         return MOVED;

      res=Poll(data_sock,POLLIN);
      if(res==-1)
      {
	 Disconnect();
         return MOVED;
      }

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
	    DebugPrint("**** ",strerror(errno),0);
	    Disconnect();
	    return MOVED;
	 }
	 goto system_error;
      }

      close(data_sock);
      data_sock=res;
      NonBlock(data_sock);
      CloseOnExec(data_sock);
      KeepAlive(data_sock);
      SetSocketBuffer(data_sock);
      SetSocketMaxseg(data_sock);

      if(!data_address_ok())
      {
	 Disconnect();
	 return MOVED;
      }

      goto pre_data_open;

   case(DATASOCKET_CONNECTING_STATE):
   datasocket_connecting_state:
      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=DATASOCKET_CONNECTING_STATE)
         return MOVED;

      if(addr_received==0)
	 goto usual_return;

      if(addr_received==1)
      {
	 if(copy_mode==COPY_NONE
	 && !data_address_ok(&data_sa,/*port_verify*/false))
	 {
	    Disconnect();
	    return MOVED;
	 }

	 addr_received=2;
	 if(copy_mode!=COPY_NONE)
	 {
	    memcpy(&copy_addr,&data_sa,sizeof(data_sa));
	    copy_addr_valid=true;
	    goto pre_WAITING_STATE;
	 }

	 sprintf(str,_("Connecting data socket to (%s) port %u"),
	    SocketNumericAddress(&data_sa),SocketPort(&data_sa));
	 DebugPrint("---- ",str,5);

	 res=SocketConnect(data_sock,&data_sa);
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
	 m=MOVED;
      }
      res=Poll(data_sock,POLLOUT);
      if(res==-1)
      {
	 Disconnect();
         return MOVED;
      }

      if(!(res&POLLOUT))
	 goto usual_return;

   pre_data_open:
      assert(rate_limit==0);
      rate_limit=new RateLimit();
      state=DATA_OPEN_STATE;
      m=MOVED;

   case(DATA_OPEN_STATE):
   {
      if(RespQueueIsEmpty())
      {
	 // When ftp server has sent "Transfer complete" it is idle,
	 // but the data can be still unsent in server side kernel buffer.
	 // So the ftp server can decide the connection is idle for too long
	 // time and disconnect. This hack is to prevent the above.
	 if(now-nop_time>=nop_interval)
	 {
	    // prevent infinite NOOP's
	    if(nop_offset==pos && nop_count*nop_interval>=timeout)
	    {
	       DebugPrint("**** ",_("Timeout - reconnecting"),0);
	       Disconnect();
	       return MOVED;
	    }
	    if(nop_time!=0)
	    {
	       nop_count++;
	       SendCmd("NOOP");
	       AddResp(0,0,CHECK_IGNORE);
	    }
	    nop_time=now;
	    if(nop_offset!=pos)
	       nop_count=0;
	    nop_offset=pos;
	 }
	 TimeoutS(nop_interval-(now-nop_time));
      }

      int ev=(mode==STORE?POLLOUT:POLLIN);
      oldstate=state;

      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=oldstate)
         return MOVED;

      if(data_sock!=-1)
      {
	 if(Poll(data_sock,ev)==-1)
	 {
	    DataClose();
	    Disconnect();
	    return MOVED;
	 }
      }
      CheckTimeout();

      if(state!=oldstate)
         return MOVED;

      goto usual_return;
   }

   pre_WAITING_STATE:
      if(copy_mode!=COPY_NONE)
	 retries=0;  // it is enough to get here in copying.
      state=WAITING_STATE;
      m=MOVED;
   case(WAITING_STATE):
   {
      oldstate=state;

      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=oldstate)
         return MOVED;

      // more work to do?
      if(RespQueueIsEmpty() && mode==ARRAY_INFO && array_ptr<array_cnt)
	 goto array_info_send_more;

      if(copy_mode==COPY_DEST && !copy_allow_store)
	 goto notimeout_return;

      if(copy_mode==COPY_DEST && !copy_done && copy_connection_open
      && RespQueueSize()==1 && use_stat)
      {
	 if(stat_time+stat_interval<=now)
	 {
	    // send STAT to know current position.
	    SendUrgentCmd("STAT");
	    AddResp(213,INITIAL_STATE,CHECK_TRANSFER);
	    FlushSendQueue(true);
	    m=MOVED;
	 }
	 else
	    TimeoutS(stat_time+stat_interval-now);
      }

      // store mode is special - the data can be buffered
      // so is COPY_* - no data connection at all.
      if(mode==STORE || copy_mode!=COPY_NONE)
	 goto notimeout_return;

      goto usual_return;
   }
   default:
      // error state - need intervension of other task
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
	 Block(data_sock,POLLIN);
      else if(state==DATASOCKET_CONNECTING_STATE)
      {
	 if(addr_received==2) // that is connect in progress
	    Block(data_sock,POLLOUT);
      }
      else if(state==DATA_OPEN_STATE)
      {
	 int bytes_allowed = rate_limit->BytesAllowed();
	 // guard against unimplemented REST: if we have sent REST command
	 // (real_pos==-1) and did not yet receive the response
	 // (RespQueueSize()>1), don't allow to read/write the data
	 // since REST could fail.
	 if(!(RespQueueSize()>1 && real_pos==-1)
	 && bytes_allowed>0) // and we are allowed to xfer
	    Block(data_sock,(mode==STORE?POLLOUT:POLLIN));
	 if(bytes_allowed==0)
	    TimeoutS(1);
      }
      else
      {
	 // should not get here
	 abort();
      }
   }
   if(control_sock!=-1)
   {
      if(state==CONNECTING_STATE)
	 Block(control_sock,POLLOUT);
      else
      {
	 Block(control_sock,POLLIN);
	 if(send_cmd_count>0 && ((flags&SYNC_MODE)==0 || sync_wait==0))
	    Block(control_sock,POLLOUT);
      }
   }
   return m;

system_error:
   if(errno==ENFILE || errno==EMFILE)
   {
      // file table overflow - it could free sometime
      TimeoutS(1);
      return m;
   }
   SwitchToState(SYSTEM_ERROR_STATE);
   return MOVED;
}

void  Ftp::LogResp(const char *l)
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

int Ftp::ReplyLogPriority(int code)
{
   // Greeting messages
   if(code==220 || code==230)
      return 3;
   if(code==250 && mode==CHANGE_DIR)
      return 3;
   // Error messages
   // 221 is the reply to QUIT, but we don't expect it.
   if(code>=400 || code==221)
      return 0;
   return 4;
}

int  Ftp::ReceiveResp()
{
   char  *store;
   char  *nl;
   int   code;
   int	 m=STALL;

   if(control_sock==-1)
      return m;

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

	    if(strlen(line)>=3 && is_ascii_digit(line[0])
	    && is_ascii_digit(line[1]) && is_ascii_digit(line[2]))
	       code=atoi(line);

	    DebugPrint("<--- ",line,
		  ReplyLogPriority(multiline_code?multiline_code:code));
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
	       if(multiline_code!=code || line[3]!=' ')
		  continue;   // Multiline response can terminate only with
			      // the same code as it started with.
			      // The space is required.
	       multiline_code=0;
	    }
	    if(sync_wait>0 && code/100!=1)
	       sync_wait--; // clear the flag to send next command
	    break;
	 }
	 else
	 {
	    struct pollfd pfd;
	    pfd.fd=control_sock;
	    pfd.events=POLLIN;
	    int res=poll(&pfd,1,0);
	    if(res<=0)
	       return m;
	    if(CheckHangup(&pfd,1))
	    {
	       ControlClose();
	       Disconnect();
	       return MOVED;
	    }

	    res=read(control_sock,resp+resp_size,resp_alloc-resp_size-1);
	    if(res==-1)
	    {
	       if(errno==EAGAIN || errno==EINTR)
		  return m;
	       if(NotSerious(errno))
	       {
		  DebugPrint("**** ",strerror(errno),0);
		  Disconnect();
		  return MOVED;
	       }
	       SwitchToState(SYSTEM_ERROR_STATE);
	       return MOVED;
	    }
	    if(res==0)
	    {
	       DebugPrint("**** ",_("Peer closed connection"),0);
	       ControlClose();
	       Disconnect();
	       return MOVED;
	    }

	    // workaround for \0 characters in response
	    for(int i=0; i<res; i++)
	       if(resp[resp_size+i]==0)
		  resp[resp_size+i]='!';

	    event_time=now;
	    resp_size+=res;
	    m=MOVED;
	 }
      }

      int newstate=CheckResp(code);
      if(newstate!=-1 && newstate!=state)
      {
	 if(newstate==FATAL_STATE || newstate==NO_FILE_STATE)
	 {
	    if(!Error())
	    {
	       char *m=line;
	       if(newstate==NO_FILE_STATE
	       && file && file[0] && !strstr(line,file))
	       {
		  m=string_alloca(strlen(line)+2+strlen(file)+2);
		  sprintf(m,"%s (%s)",line,file);
	       }
	       SetError(StateToError(),m);
	    }
	 }
	 SwitchToState((automate_state)newstate);
	 if(resp_size==0)
	    return m;
      }
   }
   return m;
}

void Ftp::SendUrgentCmd(const char *cmd)
{
   FlushSendQueue(/*all=*/true);
   static const char pre_cmd[]={TELNET_IAC,TELNET_IP,TELNET_IAC,TELNET_SYNCH};
   /* send only first byte as OOB due to OOB braindamage in many unices */
   send(control_sock,pre_cmd,1,MSG_OOB);
   send(control_sock,pre_cmd+1,sizeof(pre_cmd)-1,0);
   SendCmd(cmd);
}

void  Ftp::DataAbort()
{
   if(control_sock==-1 || state==CONNECTING_STATE)
      return;

   if(data_sock==-1 && copy_mode==COPY_NONE)
      return; // nothing to abort

   if(copy_mode!=COPY_NONE)
   {
      if(RespQueueIsEmpty())
	 return; // the transfer seems to be finished
      if(!copy_addr_valid)
	 return; // data connection cannot be established at this time
      if(!copy_connection_open && RespQueueSize()==1)
      {
	 // wu-ftpd-2.6.0 cannot interrupt accept() or connect().
	 Disconnect();
	 return;
      }
   }
   copy_connection_open=false;

   // if transfer has been completed then ABOR is not needed
   if(data_sock!=-1 && RespQueueIsEmpty())
      return;

   if(!(bool)Query("use-abor",hostname))
   {
      if(copy_mode==COPY_NONE)
	 DataClose();	// just close data connection
      else
	 Disconnect();	// nothing to close but control connection.
      return;
   }

   SendUrgentCmd("ABOR");
   AddResp(226,0,CHECK_ABOR);
   FlushSendQueue(true);
   AbortedClose();
   // don't close it now, wait for ABOR result
   aborted_data_sock=data_sock;
   data_sock=-1;
}

void Ftp::ControlClose()
{
   if(control_sock!=-1)
   {
      DebugPrint("---- ",_("Closing control socket"),7);
      close(control_sock);
      control_sock=-1;
   }
   resp_size=0;
   EmptyRespQueue();
   EmptySendQueue();
}

void Ftp::AbortedClose()
{
   if(aborted_data_sock!=-1)
   {
      close(aborted_data_sock);
      aborted_data_sock=-1;
   }
}

void  Ftp::Disconnect()
{
   if(control_sock==-1)
      return;  // already disconnected.

   /* protect against re-entering from FlushSendQueue */
   static bool disconnect_in_progress=false;
   if(disconnect_in_progress)
      return;
   disconnect_in_progress=true;

   DataAbort();
   DataClose();
   if(control_sock>=0 && state!=CONNECTING_STATE)
   {
      EmptySendQueue();
      SendCmd("QUIT");
      FlushSendQueue(true);
   }
   ControlClose();
   AbortedClose();

   if(state==CONNECTING_STATE)
      NextPeer();

   if(copy_mode!=COPY_NONE)
   {
      if(copy_addr_valid)
	 state=COPY_FAILED;
      else
	 state=INITIAL_STATE;
   }
   else if(mode==STORE && (flags&IO_FLAG))
      state=STORE_FAILED_STATE;
   else
      state=INITIAL_STATE;

   disconnect_in_progress=false;
}

void  Ftp::EmptySendQueue()
{
   sync_wait=0;
   xfree(send_cmd_buffer);
   send_cmd_buffer=send_cmd_ptr=0;
   send_cmd_alloc=send_cmd_count=0;
}

void  Ftp::DataClose()
{
   if(data_sock>=0)
   {
      DebugPrint("---- ",_("Closing data socket"),7);
      close(data_sock);
      data_sock=-1;
   }
   nop_time=0;
   nop_offset=0;
   nop_count=0;
   FreeResult();
   if(rate_limit)
   {
      delete rate_limit;
      rate_limit=0;
   }
}

int  Ftp::FlushSendQueue(bool all)
{
   int res;
   struct pollfd pfd;
   int m=STALL;

   pfd.events=POLLOUT;
   pfd.fd=control_sock;
   res=poll(&pfd,1,0);
   if(res<=0)
      return m;
   if(CheckHangup(&pfd,1))
   {
      ControlClose();
      Disconnect();
      return MOVED;
   }
   if(!(pfd.revents&POLLOUT))
      return m;

   char *cmd_begin=send_cmd_ptr;

   while(send_cmd_count>0 && (all || (flags&SYNC_MODE)==0 || sync_wait==0))
   {
      int to_write=send_cmd_count;

      char *line_end=(char*)memchr(send_cmd_ptr,'\n',send_cmd_count);
      if(line_end==NULL)
	 return m;
      to_write=line_end+1-send_cmd_ptr;

      res=write(control_sock,send_cmd_ptr,to_write);
      if(res==0)
	 return m;
      if(res==-1)
      {
	 if(errno==EAGAIN || errno==EINTR)
	    return m;
	 if(NotSerious(errno) || errno==EPIPE)
	 {
	    DebugPrint("**** ",strerror(errno),0);
	    Disconnect();
	    return MOVED;
	 }
	 SwitchToState(SYSTEM_ERROR_STATE);
	 return MOVED;
      }
      send_cmd_count-=res;
      send_cmd_ptr+=res;
      event_time=now;

      sync_wait++;
   }
   if(send_cmd_ptr>cmd_begin)
   {
      send_cmd_ptr[-1]=0;
      char *p=strstr(cmd_begin,"PASS ");

      bool may_show = (skey_pass!=0) || (user==0) || pass_open;
      if(proxy && proxy_user) // can't distinguish here
	 may_show=false;
      if(p && !may_show)
      {
	 // try to hide password
	 if(p>cmd_begin)
	 {
	    p[-1]=0;
	    DebugPrint("---> ",cmd_begin,5);
	 }
	 DebugPrint("---> ","PASS XXXX",5);
	 char *eol=strchr(p,'\n');
	 if(eol)
	 {
	    *eol=0;
	    DebugPrint("---> ",eol+1,5);
	 }
      }
      else
      {
	 DebugPrint("---> ",cmd_begin,5);
      }
   }
   return m;
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

void Ftp::SendCmd2(const char *cmd,const char *f)
{
   char *s=string_alloca(strlen(cmd)+1+strlen(f)+2);
   sprintf(s,"%s %s\n",cmd,f);
   SendCmd(s);
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

   flags&=~NOREST_MODE;	// can depend on a particular file

   Resume();
   ExpandTildeInCWD();
   DataAbort();
   DataClose();
   if(control_sock!=-1)
   {
      switch(state)
      {
      case(CONNECTING_STATE):
      case(USER_RESP_WAITING_STATE):
	 Disconnect();
	 break;
      case(ACCEPTING_STATE):
      case(DATASOCKET_CONNECTING_STATE):
      case(CWD_CWD_WAITING_STATE):
      case(WAITING_STATE):
      case(DATA_OPEN_STATE):
      case(NO_FILE_STATE):
      case(STORE_FAILED_STATE):
      case(FATAL_STATE):
      case(SYSTEM_ERROR_STATE):
      case(COPY_FAILED):
	 state=(control_sock==-1 ? INITIAL_STATE : EOF_STATE);
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
   copy_mode=COPY_NONE;
   copy_addr_valid=false;
   copy_done=false;
   copy_connection_open=false;
   stat_time=0;
   copy_allow_store=false;
   CloseRespQueue();
   super::Close();
}

void Ftp::CloseRespQueue()
{
   for(int i=RQ_head; i<RQ_tail; i++)
   {
      check_case_t cc=RespQueue[i].check_case;
      switch(cc)
      {
      case(CHECK_IGNORE):
      case(CHECK_PWD):
      case(CHECK_USER):
      case(CHECK_USER_PROXY):
      case(CHECK_PASS):
      case(CHECK_PASS_PROXY):
      case(CHECK_READY):
      case(CHECK_ABOR):
      case(CHECK_CWD_STALE):
	 break;
      case(CHECK_CWD_CURR):
      case(CHECK_CWD):
	 if(RespQueue[i].path==0)
	 {
	    Disconnect();
	    return;  // can't Close() with this in queue
	 }
	 RespQueue[i].check_case=CHECK_CWD_STALE;
	 break;
      case(CHECK_NONE):
      case(CHECK_REST):
      case(CHECK_SIZE):
      case(CHECK_SIZE_OPT):
      case(CHECK_MDTM):
      case(CHECK_MDTM_OPT):
      case(CHECK_PASV):
      case(CHECK_EPSV):
      case(CHECK_PORT):
      case(CHECK_FILE_ACCESS):
      case(CHECK_RNFR):
      case(CHECK_TRANSFER):
	 RespQueue[i].check_case=CHECK_IGNORE;
	 break;
      }
      if(cc!=CHECK_USER)
	 RespQueue[i].log_resp=false;
   }
}

Ftp::expected_response *Ftp::FindLastCWD()
{
   for(int i=RQ_tail-1; i>=RQ_head; i--)
   {
      switch(RespQueue[i].check_case)
      {
      case(CHECK_CWD_CURR):
      case(CHECK_CWD_STALE):
      case(CHECK_CWD):
	 return &RespQueue[i];
      default:
	 ;
      }
   }
   return 0;
}

bool  Ftp::IOReady()
{
   return (state==DATA_OPEN_STATE || state==WAITING_STATE)
      && real_pos!=-1 && IsOpen();
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

   if(state==WAITING_STATE && RespQueueIsEmpty())
   {
      if(result_size==0)
      {
	 SwitchToState(EOF_STATE);
	 return(0);
      }
      if(result_size<size)
	 size=result_size;
      if(norest_manual && real_pos==0 && pos>0)
	 return DO_AGAIN;
      if(real_pos<pos)
      {
	 int skip=pos-real_pos;
	 if(skip>result_size)
	    skip=result_size;
	 size=skip;
      }
      memcpy(buf,result,size);
      memmove(result,result+size,result_size-=size);
      if(real_pos==pos)
	 pos+=size;
      real_pos+=size;
      return(size);
   }

read_again:
   if(state!=DATA_OPEN_STATE)
      return DO_AGAIN;

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
   {
      DataClose();
      Disconnect();
      return(DO_AGAIN);
   }
   {
      int allowed=rate_limit->BytesAllowed();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(norest_manual && real_pos==0 && pos>0)
      return DO_AGAIN;
   res=read(data_sock,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return DO_AGAIN;
      if(NotSerious(errno))
      {
	 DebugPrint("**** ",strerror(errno),0);
	 Disconnect();
	 return DO_AGAIN;
      }
      SwitchToState(SYSTEM_ERROR_STATE);
      return(StateToError());
   }
   if(res==0)
   {
   we_have_eof:
      DataClose();
      if(RespQueueIsEmpty())
      {
	 SwitchToState(EOF_STATE);
	 return(0);
      }
      else
	 return DO_AGAIN;
   }
   retries=0;
   rate_limit->BytesUsed(res);
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
   {
      DataClose();
      Disconnect();
      return(DO_AGAIN);
   }
   {
      int allowed=rate_limit->BytesAllowed();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size==0)
      return 0;
   res=write(data_sock,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return DO_AGAIN;
      if(NotSerious(errno) || errno==EPIPE)
      {
	 DebugPrint("**** ",strerror(errno),0);
	 Disconnect();
	 return DO_AGAIN;
      }
      SwitchToState(SYSTEM_ERROR_STATE);
      return(StateToError());
   }
   retries=0;
   rate_limit->BytesUsed(res);
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
   if(ns==SYSTEM_ERROR_STATE)
      saved_errno=errno;

   if(ns==state)
      return;

   switch(ns)
   {
   // Error states that require Disconnect(). Set state here; don't check
   // for STORE_FAILED_STATE or COPY_FAILED possibly set by Disconnect.
   case(SYSTEM_ERROR_STATE):
   case(FATAL_STATE):
   case(LOGIN_FAILED_STATE):
      Disconnect();
      state=ns;
      return;

   case(INITIAL_STATE):
   case(STORE_FAILED_STATE):
      Disconnect();  // this is not an error, but Disconnect can set state.
      break;
   case(EOF_STATE):
      DataAbort();
      DataClose();
      xfree(file); file=0;
      set_idle_start();
      mode=CLOSED;
      break;
   case(NO_FILE_STATE):
      break;
      Disconnect();
      state=ns;
      return;
   case(LOOKUP_ERROR_STATE):
      break;
   case(COPY_FAILED):
      break;
   case(DATA_OPEN_STATE): // special case for NLIST to emulate eof
      DataClose();
      break;
   default:
      // don't translate - this message just indicates bug in lftp
      fprintf(stderr,"SwitchToState called with invalid state\n");
      abort();
   }
   if(state==COPY_FAILED || state==STORE_FAILED_STATE)
      return; // don't set state, 'cause we've already failed
   if(ns==STORE_FAILED_STATE && mode!=STORE)
      state=INITIAL_STATE;
   else
      state=ns;
}

void  Ftp::AddResp(int exp,int fail,check_case_t ck,bool log)
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
   RespQueue[RQ_tail].check_case=ck;
   RespQueue[RQ_tail].log_resp=log;
   RespQueue[RQ_tail].path=0;
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
void  Ftp::MoveConnectionHere(Ftp *o)
{
   EmptyRespQueue();
   EmptySendQueue();

   RQ_head=o->RQ_head;
   RQ_tail=o->RQ_tail;
   multiline_code=o->multiline_code;
   RespQueue=o->RespQueue;
   RQ_alloc=o->RQ_alloc;

   o->RespQueue=0;
   o->EmptyRespQueue();
   CloseRespQueue(); // we need not handle other session's replies.

   sync_wait=o->sync_wait;
   send_cmd_buffer=o->send_cmd_buffer;
   send_cmd_ptr=o->send_cmd_ptr;
   send_cmd_alloc=o->send_cmd_alloc;
   send_cmd_count=o->send_cmd_count;

   o->send_cmd_buffer=0;
   o->EmptySendQueue();

   o->state=INITIAL_STATE;
   assert(control_sock==-1);
   control_sock=o->control_sock;
   o->control_sock=-1;
   assert(aborted_data_sock==-1);
   aborted_data_sock=o->aborted_data_sock;
   o->aborted_data_sock=-1;
   if(peer_curr>=peer_num)
      peer_curr=0;
   type=o->type;
   event_time=o->event_time;

   size_supported=o->size_supported;
   mdtm_supported=o->mdtm_supported;
   site_chmod_supported=o->site_chmod_supported;
   last_rest=o->last_rest;

   set_real_cwd(o->real_cwd);
   o->set_real_cwd(0);
   o->Disconnect();
   state=EOF_STATE;
}

int   Ftp::CheckResp(int act)
{
   int new_state=-1;

   if(act==150)
   {
      copy_connection_open=true;
      stat_time=now+2;
   }

   if(act==150 && mode==RETRIEVE && opt_size && *opt_size==-1)
   {
      // try to catch size
      char *s=strrchr(line,'(');
      if(s && is_ascii_digit(s[1]))
      {
	 *opt_size=atol(s+1);
      	 DebugPrint("---- ",_("saw file size in response"),7);
      }
   }

   if(act/100==1) // intermediate responses are ignored
      return -1;

   if(act==421 || act==221)  // timeout or something else
   {
      DebugPrint("**** ",_("remote end closes connection"),3);
      return(INITIAL_STATE);
   }

   if(RespQueueIsEmpty())
   {
      DebugPrint("**** ",_("extra server response"),3);
      if(act/100==2) // some buggy servers send several 226 replies
	 return -1;  // ignore them.
      return(INITIAL_STATE);
   }

   int exp=RespQueue[RQ_head].expect;

   // some servers mess all up
   if(act==331 && exp==220 && !(flags&SYNC_MODE) && RespQueueSize()>1)
   {
      DebugPrint("---- ","Turning on sync-mode",2);
      ResMgr::Set("ftp:sync-mode",hostname,"on");
      try_time=0; // retry immediately
      return INITIAL_STATE;
   }

   bool match=(act/100==exp/100);
   check_case_t cc=RespQueue[RQ_head].check_case;

   switch(cc)
   {
   case CHECK_NONE:
      break;

   case CHECK_IGNORE:
   ignore:
      new_state=state;
      break;

   case CHECK_READY:
      // M$ can't get it right... I'm really tired of setting sync-mode manually.
      if(!(flags&SYNC_MODE) && strstr(line,"Microsoft FTP Service"))
      {
	 DebugPrint("---- ","Turning on sync-mode",2);
	 flags|=SYNC_MODE;
	 ResMgr::Set("ftp:sync-mode",hostname,"on");
	 try_time=0; // retry immediately
	 new_state=INITIAL_STATE;
      }
      if(!match)
	 try_time=now;	// count the reconnect-interval from this moment
      break;

   case CHECK_REST:
      new_state=RestCheck(act,exp);
      break;

   case CHECK_CWD:
   case CHECK_CWD_CURR:
      if(act/100==5)
	 new_state=NO_FILE_STATE;
      if(match)
      {
	 if(cc==CHECK_CWD)
	 {
	    xfree(cwd);
	    cwd=xstrdup(RespQueue[RQ_head].path);
	 }
	 set_real_cwd(cwd);
      }
      break;

   case CHECK_CWD_STALE:
      if(match)
	 set_real_cwd(RespQueue[RQ_head].path);
      goto ignore;

   case CHECK_ABOR:
      AbortedClose();
      goto ignore;

   case CHECK_SIZE:
      new_state=CatchSIZE(act,exp);
      break;
   case CHECK_SIZE_OPT:
      new_state=CatchSIZE_opt(act,exp);
      break;
   case CHECK_MDTM:
      new_state=CatchDATE(act,exp);
      break;
   case CHECK_MDTM_OPT:
      new_state=CatchDATE_opt(act,exp);
      break;

   case CHECK_FILE_ACCESS:
   file_access:
      if(mode==CHANGE_MODE && (act==500 || act==501 || act==502))
      {
	 site_chmod_supported=false;
	 new_state=NO_FILE_STATE;
	 break;
      }
      new_state=NoFileCheck(act,exp);
      break;

   case CHECK_PASV:
   case CHECK_EPSV:
      if(!match && copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 new_state=COPY_FAILED;
	 break;
      }
      memset(&data_sa,0,sizeof(data_sa));
      if(strlen(line)<=4)
	 goto passive_off;
      if(match)
      {
	 if(cc==CHECK_PASV)
	    new_state=Handle_PASV();
	 else // cc==CHECK_EPSV
	    new_state=Handle_EPSV();

	 if(new_state==INITIAL_STATE)
	    goto passive_off;
	 addr_received=1;
      }
      if(act/100==5)
      {
      passive_off:
	 DebugPrint("---- ",_("Switching passive mode off"),2);
	 SetFlag(PASSIVE_MODE,0);
	 new_state=INITIAL_STATE;
      }
      break;

   case CHECK_PORT:
      if(!match && copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 new_state=COPY_FAILED;
	 break;
      }
      if(act/100==5)
      {
	 DebugPrint("---- ",_("Switching passive mode on"),2);
	 SetFlag(PASSIVE_MODE,1);
	 new_state=INITIAL_STATE;
      }
      break;

   case CHECK_PWD:
      if(match && !home)
	 home=ExtractPWD();   // it allocates space.
      new_state=state;
      break;

   case CHECK_RNFR:
      if(match)
      {
	 SendCmd2("RNTO",file1);
	 AddResp(250,INITIAL_STATE,CHECK_FILE_ACCESS);
      }
      else
	 goto file_access;
      break;

   case CHECK_USER_PROXY:
      new_state=proxy_NoPassReqCheck(act,exp);
      break;
   case CHECK_USER:
      new_state=NoPassReqCheck(act,exp);
      break;
   case CHECK_PASS_PROXY:
      new_state=proxy_LoginCheck(act,exp);
      break;
   case CHECK_PASS:
      new_state=LoginCheck(act,exp);
      break;

   case CHECK_TRANSFER:
      new_state=TransferCheck(act,exp);
      break;

   } /* end switch */

   if(new_state==-1 && !match)
      new_state=RespQueue[RQ_head].fail_state;
   PopResp();
   return(new_state);
}

void  Ftp::SetRespPath(const char *p)
{
   if(RQ_tail>RQ_head)
      RespQueue[RQ_tail-1].path=xstrdup(p);
}

void  Ftp::PopResp()
{
   if(RQ_head!=RQ_tail)
   {
      xfree(RespQueue[RQ_head].path);
      RQ_head=RQ_head+1;
   }
}

const char *Ftp::CurrentStatus()
{
   if(Error())
      return StrError(error_code);
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
	 if(!ReconnectAllowed())
	    return DelayingMessage();
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
      {
	 if(mode==STORE)
	    return(_("Sending data"));
         else
	    return(_("Receiving data"));
      }
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
   case(COPY_FAILED):
      // user will not see this
      return("Copy failed");
   }
   abort();
}

int   Ftp::StateToError()
{
   if(Error())
      return error_code;
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
   int year,month,day,hour,minute,second;

   int skip=0;
   int n=sscanf(s,"%4d%n",&year,&skip);

   if(n==1 && year==1910)  // try to workaround server's y2k bug
   {
      n=sscanf(s,"%5d%n",&year,&skip);
      if(year>=19100)
	 year=year-19100+2000;
   }

   if(n!=1)
      return NO_DATE;

   n=sscanf(s+skip,"%2d%2d%2d%2d%2d",&month,&day,&hour,&minute,&second);

   if(n!=5)
      return NO_DATE;

   tm.tm_year=year-1900;
   tm.tm_mon=month-1;
   tm.tm_mday=day;
   tm.tm_hour=hour;
   tm.tm_min=minute;
   tm.tm_sec=second;

   return mktime_from_utc(&tm);
}

void  Ftp::SetFlag(int flag,bool val)
{
   flag&=MODES_MASK;  // only certain flags can be changed
   if(val)
      flags|=flag;
   else
      flags&=~flag;
}

bool  Ftp::SameSiteAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   Ftp *o=(Ftp*)fa;
   return(!xstrcmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && !xstrcmp(group,o->group) && !xstrcmp(gpass,o->gpass));
}

bool  Ftp::SameConnection(const Ftp *o)
{
   if(!strcmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
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

   if(mode==ARRAY_INFO)
   {
      if(state==WAITING_STATE && RespQueueIsEmpty() && array_ptr==array_cnt)
	 return(OK);
      return(IN_PROGRESS);
   }

   if(copy_mode==COPY_DEST && !copy_allow_store)
      return(IN_PROGRESS);

   if(mode==CHANGE_DIR || mode==RENAME
   || mode==MAKE_DIR || mode==REMOVE_DIR || mode==REMOVE || mode==CHANGE_MODE
   || copy_mode!=COPY_NONE)
   {
      if(state==WAITING_STATE && RespQueueIsEmpty())
	 return(OK);
      return(IN_PROGRESS);
   }
   if(mode==CONNECT_VERIFY)
   {
      if(state!=INITIAL_STATE)
	 return OK;
      return(peer?OK:IN_PROGRESS);
   }
   abort();
}

void Ftp::Connect(const char *new_host,const char *new_port)
{
   super::Connect(new_host,new_port);
   flags=0;

   dos_path=false;
   vms_path=false;
   mdtm_supported=true;
   size_supported=true;
   site_chmod_supported=true;

   Reconfig();
   state=INITIAL_STATE;
}

void Ftp::Reconfig(const char *name)
{
   xfree(closure);
   closure=xstrdup(hostname);

   super::Reconfig(name);

   const char *c=closure;

   SetFlag(SYNC_MODE,	Query("sync-mode",c));
   SetFlag(PASSIVE_MODE,Query("passive-mode",c));
   rest_list = Query("rest-list",c);

   nop_interval = Query("nop-interval",c);

   allow_skey = Query("skey-allow",c);
   force_skey = Query("skey-force",c);
   verify_data_address = Query("verify-address",c);
   verify_data_port = Query("verify-port",c);

   use_stat = Query("use-stat",c);
   stat_interval = Query("stat-interval",c);

   xfree(list_options);
   list_options = xstrdup(Query("list-options",c));

   xfree(anon_user);
   anon_user=xstrdup(Query("anon-user",c));
   xfree(anon_pass);
   anon_pass=xstrdup(Query("anon-pass",c));

   if(!NoProxy())
      SetProxy(Query("proxy",c));

   if(proxy && proxy_port==0)
      proxy_port=xstrdup(FTPPORT);

   if(nop_interval<30)
      nop_interval=30;

   if(control_sock!=-1)
      SetSocketBuffer(control_sock);
   if(data_sock!=-1)
      SetSocketBuffer(data_sock);
}

void Ftp::Cleanup()
{
   if(hostname==0)
      return;

   for(Ftp *o=ftp_chain; o!=0; o=o->ftp_next)
   {
      if(o->control_sock==-1 || o->mode!=CLOSED)
	 continue;
      if(!xstrcmp(hostname,o->hostname))
	 o->Disconnect();
   }
}
void Ftp::CleanupThis()
{
   if(control_sock==-1 || mode!=CLOSED)
      return;
   Disconnect();
}

ListInfo *Ftp::MakeListInfo()
{
   return new FtpListInfo(this);
}
Glob *Ftp::MakeGlob(const char *pattern)
{
   return new FtpGlob(this,pattern);
}
DirList *Ftp::MakeDirList(ArgV *args)
{
   return new FtpDirList(args,this);
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

   DebugPrint("---- ","found s/key substring");

   int skey_sequence=0;
   char *buf=(char*)alloca(strlen(cp));

   if(sscanf(cp,"%d %s",&skey_sequence,buf)!=2 || skey_sequence<1)
      return 0;

   return calculate_skey_response(skey_sequence,buf,pass);
}

int Ftp::Buffered()
{
#ifdef TIOCOUTQ
   if(!TIOCOUTQ_works)
      return 0;
   if(state!=DATA_OPEN_STATE || data_sock==-1 || mode!=STORE)
      return 0;
   int buffer=0;
   if(TIOCOUTQ_returns_free_space)
   {
      socklen_t len=sizeof(buffer);
      if(getsockopt(data_sock,SOL_SOCKET,SO_SNDBUF,(char*)&buffer,&len)==-1)
	 return 0;
      int avail=buffer;
      if(ioctl(data_sock,TIOCOUTQ,&avail)==-1)
	 return 0;
      if(avail>buffer)
	 return 0; // something wrong
      buffer-=avail;
      buffer=buffer*3/4; // approx...
   }
   else
   {
      if(ioctl(data_sock,TIOCOUTQ,&buffer)==-1)
	 return 0;
   }
   if(pos>=0 && buffer>pos)
      buffer=pos;
   return buffer;
#else
   return 0;
#endif
}

#ifdef MODULE
CDECL void module_init()
{
   Ftp::ClassInit();
}
#endif
