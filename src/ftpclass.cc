/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <netinet/in.h>
#include <arpa/inet.h>
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
#include "misc.h"

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

#if defined(HAVE_INET_ATON) && !defined(HAVE_INET_ATON_DECL)
CDECL int inet_aton(const char *,struct in_addr *);
#endif

#include "xalloca.h"

#ifdef USE_SSL
# include "lftp_ssl.h"
#else
# define control_ssl 0
const bool Ftp::ftps=false;
#endif


#define FTP_DEFAULT_PORT "ftp"
#define FTPS_DEFAULT_PORT "990"
#define FTP_DATA_PORT 20
#define FTPS_DATA_PORT 989

#define super NetAccess
#define peer_sa (peer[peer_curr])

#define is5XX(code) ((code)>=500 && (code)<600)
#define is4XX(code) ((code)>=400 && (code)<500)
#define is3XX(code) ((code)>=300 && (code)<400)
#define is2XX(code) ((code)>=200 && (code)<300)
#define is1XX(code) ((code)>=100 && (code)<200)

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

#ifdef USE_SSL
   Register("ftps",FtpS::New);
#endif

   test_TIOCOUTQ();
}


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
      if(d.in.sin_port!=htons(FTP_DATA_PORT)
      && d.in.sin_port!=htons(FTPS_DATA_PORT))
	 goto wrong_port;
   }
#endif

   if(d.sa.sa_family!=c.sa.sa_family)
      return false;
   if(d.sa.sa_family==AF_INET)
   {
      if(memcmp(&d.in.sin_addr,&c.in.sin_addr,sizeof(d.in.sin_addr)))
	 goto address_mismatch;
      if(d.in.sin_port!=htons(FTP_DATA_PORT)
      && d.in.sin_port!=htons(FTPS_DATA_PORT))
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
      if(d.in6.sin6_port!=htons(FTP_DATA_PORT)
      && d.in6.sin6_port!=htons(FTPS_DATA_PORT))
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
/* General rule: check for valid replies first, errors second. */

void Ftp::RestCheck(int act)
{
   if(is2XX(act) || is3XX(act))
   {
      real_pos=rest_pos;  // REST successful
      last_rest=rest_pos;
      return;
   }
   if(pos==0)
      return;
   if(is5XX(act))
   {
      DebugPrint("---- ",_("Switching to NOREST mode"),2);
      flags|=NOREST_MODE;
      real_pos=0;
      if(mode==STORE)
	 pos=0;
      if(copy_mode!=COPY_NONE)
	 copy_failed=true;
      return;
   }
   Disconnect();
}

void Ftp::NoFileCheck(int act)
{
   if(is2XX(act))
      return;
   if(act==RESP_NOT_IMPLEMENTED || act==RESP_NOT_UNDERSTOOD)
   {
      SetError(FATAL,line);
      return;
   }
   if(is5XX(act))
   {
      // retry on these errors (ftp server ought to send 4xx code instead)
      if((strstr(line,"Broken pipe") && (!file || !strstr(file,"Broken pipe")))
      || (strstr(line,"Too many")    && (!file || !strstr(file,"Too many")))
      || (strstr(line,"timed out")   && (!file || !strstr(file,"timed out")))
      // if there were some data received, assume it is temporary error.
      || (mode!=STORE && (flags&IO_FLAG)))
      {
	 Disconnect();
	 return;
      }
      if(real_pos>0 && !(flags&IO_FLAG) && copy_mode==COPY_NONE)
      {
	 DataClose();
	 DebugPrint("---- ",_("Switching to NOREST mode"),2);
	 flags|=NOREST_MODE;
	 real_pos=0;
	 if(mode==STORE)
	    pos=0;
	 state=EOF_STATE; // retry
	 return;
      }
      SetError(NO_FILE,line);
      return;
   }
   Disconnect();
}

// 226 Transfer complete.
void Ftp::TransferCheck(int act)
{
   if(act==225 || act==226) // data connection is still open or ABOR worked.
   {
      copy_done=true;
      AbortedClose();
   }
   if(act==211)
   {
      // permature STAT?
      stat_time=now+3;
      return;
   }
   if(act==213)	  // this must be a STAT reply.
   {
      stat_time=now;

      long long p;
      // first try Serv-U format:
      //    Status for user UUU from X.X.X.X
      //    Stored 1 files, 0 Kbytes
      //    Retrieved 0 files, 0 Kbytes
      //    Receiving file XXX (YYY bytes)
      char *r=strstr(all_lines,"Receiving file");
      if(r)
      {
	 r=strrchr(r,'(');
	 char c=0;
	 if(r && sscanf(r,"(%lld bytes%c",&p,&c)==2 && c==')')
	    goto found_offset;
      }
      // wu-ftpd format:
      //    Status: XXX of YYY bytes transferred
      // or
      //    Status: XXX bytes transferred
      //
      // find the first number.
      for(char *b=line+4; ; b++)
      {
	 if(*b==0)
	    return;
	 if(!is_ascii_digit(*b))
	    continue;
	 if(sscanf(b,"%lld",&p)==1)
	    break;
      }
   found_offset:
      if(copy_mode==COPY_DEST)
	 real_pos=pos=p;
      return;
   }
   if(copy_mode!=COPY_NONE && act==425 && strstr(line,"port theft"))
   {
      copy_passive=!copy_passive;
      copy_failed=true;
      return;
   }
   if(act==RESP_NO_FILE && mode==LIST)
   {
      DataClose();
      state=EOF_STATE;
      eof=true; // simulate eof
      return;
   }
   if(act==RESP_BROKEN_PIPE && copy_mode==COPY_NONE)
   {
      if(data_sock==-1 && strstr(line,"Broken pipe"))
   	 return;
   }
   NoFileCheck(act);
}

void Ftp::LoginCheck(int act)
{
   if(ignore_pass)
      return;
   if(act==530) // login incorrect or overloaded server
   {
      const char *rexp=Query("retry-530",hostname);
      if(re_match(all_lines,rexp,REG_ICASE))
      {
	 DebugPrint("---- ",_("Server reply matched ftp:retry-530, retrying"));
	 goto retry;
      }
      if(!user)
      {
	 rexp=Query("retry-530-anonymous",hostname);
	 if(re_match(all_lines,rexp,REG_ICASE))
	 {
	    DebugPrint("---- ",_("Server reply matched ftp:retry-530-anonymous, retrying"));
	    goto retry;
	 }
      }
   }
   if(is5XX(act))
   {
      SetError(LOGIN_FAILED,line);
      return;
   }

   if(!is2XX(act) && !is3XX(act))
   {
   retry:
      Disconnect();
      NextPeer();
      if(peer_curr==0)
	 try_time=now;	// count the reconnect-interval from this moment
   }
   if(is3XX(act))
   {
      if(!QueryStringWithUserAtHost("acct"))
      {
	 Disconnect();
	 SetError(LOGIN_FAILED,_("Account is required, set ftp:acct variable"));
      }
   }
}

void Ftp::FreeResult()
{
   xfree(result);
   result=0;
   result_size=0;
}

void Ftp::NoPassReqCheck(int act) // for USER command
{
   if(is2XX(act)) // in some cases, ftpd does not ask for pass.
   {
      ignore_pass=true;
      return;
   }
   if(act==331 && allow_skey && user && pass && result)
   {
      skey_pass=xstrdup(make_skey_reply());
      FreeResult();
      if(force_skey && skey_pass==0)
      {
	 SetError(LOGIN_FAILED,_("ftp:skey-force is set and server does not support OPIE nor S/KEY"));
	 return;
      }
   }
   if(is3XX(act))
      return;
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
	 SetError(LOGIN_FAILED,line);
	 return;
      }
      goto def_ret;
   }
   if(is5XX(act))
   {
      // proxy interprets USER as user@host, so we check for host name validity
      if(proxy && (strstr(line,"host") || strstr(line,"resolve")))
      {
	 DebugPrint("---- ",_("assuming failed host name lookup"));
	 SetError(LOOKUP_ERROR,line);
	 return;
      }
      SetError(LOGIN_FAILED,line);
      return;
   }
def_ret:
   Disconnect();
   try_time=now;	// count the reconnect-interval from this moment
}

// login to proxy.
void Ftp::proxy_LoginCheck(int act)
{
   if(is2XX(act))
      return;
   if(is5XX(act))
   {
      SetError(LOGIN_FAILED,line);
      return;
   }
   Disconnect();
   try_time=now;	// count the reconnect-interval from this moment
}

void Ftp::proxy_NoPassReqCheck(int act)
{
   if(is3XX(act) || is2XX(act))
      return;
   if(is5XX(act))
   {
      SetError(LOGIN_FAILED,line);
      return;
   }
   Disconnect();
   try_time=now;	// count the reconnect-interval from this moment
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

   const char *scan=strchr(line,'"');
   if(scan==0)
      return 0;

   char *store=pwd;
   scan++;
   for(;;)
   {
      if(*scan==0)
	 return 0;
      if(*scan=='"')
      {
	 if(scan[1]=='"')
	 {
	    *store++='"';
	    scan++;
	 }
	 else if(scan[1]!=0 && scan[1]!=' ') // some ftpd don't encode '"'
	    *store++=*scan;
	 else
	    break;
      }
      else
	 *store++=*scan;
      scan++;
   }

   if(store==pwd)
      return 0;	  // empty home not allowed.
   *store=0;

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

static bool InPrivateNetwork(const sockaddr_u *u)
{
   if(u->sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&u->in.sin_addr;
      return (a[0]==10)
	  || (a[0]==172 && a[1]>=16 && a[1]<32)
	  || (a[0]==192 && a[1]==168);
   }
   return false;
}
static bool IsLoopback(const sockaddr_u *u)
{
   if(u->sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&u->in.sin_addr;
      return (a[0]==127 && a[1]==0 && a[2]==0 && a[3]==1);
   }
#if INET6
   if(u->sa.sa_family==AF_INET6)
      return IN6_IS_ADDR_LOOPBACK(&u->in6.sin6_addr);
#endif
   return false;
}

int Ftp::Handle_PASV()
{
   unsigned a0,a1,a2,a3,p0,p1;
   /*
    * Extract address. RFC1123 says:
    * "...must scan the reply for the first digit..."
    */
   for(char *b=line+4; ; b++)
   {
      if(*b==0)
      {
	 Disconnect();
	 return 0;
      }
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
   {
      Disconnect();
      return 0;
   }

   a[0]=a0; a[1]=a1; a[2]=a2; a[3]=a3;
   p[0]=p0; p[1]=p1;

   if((a0==0 && a1==0 && a2==0 && a3==0)
   || ((bool)Query("fix-pasv-address",hostname)
       && InPrivateNetwork(&data_sa) && !InPrivateNetwork(&peer_sa)))
   {
      // broken server, try to fix up
      fixed_pasv=true;
      if(data_sa.sa.sa_family==AF_INET)
	 memcpy(a,&peer_sa.in.sin_addr,sizeof(peer_sa.in.sin_addr));
#if INET6
      else if(data_sa.in.sin_family==AF_INET6)	// peer_sa should be V4MAPPED
	 memcpy(a,&peer_sa.in6.sin6_addr.s6_addr[12],4);
#endif
   }

   return 1;
}

int Ftp::Handle_EPSV()
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
      DebugPrint("**** ",_("cannot parse EPSV response"),0);
      Disconnect();
      return 0;
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
   {
      Disconnect();
      return 0;
   }
   return 1;
}

void Ftp::CatchDATE(int act)
{
   if(!array_for_info)
      return;

   if(is2XX(act))
   {
      if(strlen(line)>4 && is_ascii_digit(line[4]))
	 array_for_info[array_ptr].time=ConvertFtpDate(line+4);
      else
	 array_for_info[array_ptr].time=NO_DATE;
   }
   else	if(is5XX(act))
   {
      if(act==500 || act==502)
	 mdtm_supported=false;
      array_for_info[array_ptr].time=NO_DATE;
   }
   else
   {
      Disconnect();
      return;
   }

   array_for_info[array_ptr].get_time=false;
   if(!array_for_info[array_ptr].get_size)
      array_ptr++;

   retries=0;
   persist_retries=0;
}
void Ftp::CatchDATE_opt(int act)
{
   if(!opt_date)
      return;

   if(is2XX(act) && strlen(line)>4 && is_ascii_digit(line[4]))
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
}

void Ftp::CatchSIZE(int act)
{
   if(!array_for_info)
      return;

   if(is2XX(act))
   {
      if(strlen(line)>4 && is_ascii_digit(line[4]))
	 array_for_info[array_ptr].size=atol(line+4);
      else
	 array_for_info[array_ptr].size=-1;
   }
   else	if(is5XX(act))
   {
      if(act==500 || act==502)
	 size_supported=false;
      array_for_info[array_ptr].size=-1;
   }
   else
   {
      Disconnect();
      return;
   }

   array_for_info[array_ptr].get_size=false;
   if(!array_for_info[array_ptr].get_time)
      array_ptr++;

   retries=0;
   persist_retries=0;
}
void Ftp::CatchSIZE_opt(int act)
{
   if(is2XX(act) && strlen(line)>4 && is_ascii_digit(line[4]))
   {
      entity_size=atol(line+4);
   }
   else
   {
      if(act==500 || act==502)
	 size_supported=false;
      entity_size=NO_SIZE;
   }
   if(opt_size)
   {
      *opt_size=entity_size;
      opt_size=0;
   }
}


void Ftp::InitFtp()
{
   UpdateNow();

   control_sock=-1;
   data_sock=-1;
   aborted_data_sock=-1;
   quit_sent=false;
   fixed_pasv=false;

#ifdef USE_SSL
   control_ssl=0;
   control_ssl_connected=false;
   data_ssl=0;
   data_ssl_connected=false;
   prot='C';	  // current protection scheme 'C'lear or 'P'rivate
   ftps=false;	  // ssl and prot='P' by default (port 990)
   auth_tls_sent=false;
#endif

   nop_time=0;
   nop_count=0;
   nop_offset=0;
   resp=0;
   resp_size=0;
   resp_alloc=0;
   sync_wait=0;
   line=0;
   all_lines=0;
   eof=false;
   type=FTP_TYPE_A;
   send_cmd_buffer=0;
   send_cmd_alloc=0;
   send_cmd_count=0;
   send_cmd_ptr=0;
   result=NULL;
   result_size=0;
   state=INITIAL_STATE;
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
   rest_pos=0;

   RespQueue=0;
   RQ_alloc=0;
   RQ_head=RQ_tail=0;
   multiline_code=0;

   anon_pass=0;
   anon_user=0;	  // will be set by Reconfig()
   home_auto=0;

   copy_mode=COPY_NONE;
   copy_addr_valid=false;
   copy_passive=false;
   copy_done=false;
   copy_connection_open=false;
   stat_time=0;
   copy_allow_store=false;
   copy_failed=false;

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

   state=INITIAL_STATE;
   flags=f->flags&MODES_MASK;
   xfree(home_auto);
   home_auto=xstrdup(f->home_auto);
}

Ftp::~Ftp()
{
   Close();
   quit_sent=true;
   Disconnect();

   xfree(anon_user);
   xfree(anon_pass);
   xfree(home_auto);
   xfree(list_options);
   xfree(line);
   xfree(all_lines);
   xfree(resp);

   xfree(RespQueue);
   xfree(send_cmd_buffer);

   xfree(skey_pass);
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

void Ftp::PropagateHomeAuto()
{
   if(!home_auto)
      return;
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Ftp *o=(Ftp*)fo; // we are sure it is Ftp.
      if(!o->home_auto)
      {
	 o->home_auto=xstrdup(home_auto);
	 o->dos_path=dos_path;
	 o->vms_path=vms_path;
	 if(!o->home)
	    o->set_home(home_auto);
      }
   }
}
const char *Ftp::FindHomeAuto()
{
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Ftp *o=(Ftp*)fo; // we are sure it is Ftp.
      if(o->home_auto)
	 return o->home_auto;
   }
   return 0;
}

void  Ftp::GetBetterConnection(int level,int count)
{
   if(level==0 && cwd==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Ftp *o=(Ftp*)fo; // we are sure it is Ftp.

      if(o->IsConnected()<2)
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
	       return; // oops...
	 }
	 else
	 {
	    if(o->RespQueueSize()>0)
	       continue;
	 }
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

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      // so borrow the connection
      MoveConnectionHere(o);
      return;
   }
}

void  Ftp::HandleTimeout()
{
   quit_sent=true;
   super::HandleTimeout();
}

int   Ftp::Do()
{
   char	 *str=(char*)alloca(xstrlen(cwd)+xstrlen(hostname)+xstrlen(proxy)+256);
   const char *command=0;
   bool	       append_file=false;
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
	 if(control_sock!=-1)
	    idle_start=now+timeout;
	 return m;
      }
      TimeoutS(idle_start+idle-now);
   }

   if(Error() || eof || quit_sent || mode==CLOSED)
   {
      // inactive behavior
      if(control_sock!=-1)
      {
	 m|=FlushSendQueue();
	 m|=ReceiveResp();
	 if(quit_sent && RespQueueIsEmpty())
	 {
	    Disconnect();
	    return MOVED;
	 }
      }
      if(eof || mode==CLOSED)
	 goto notimeout_return;
      goto usual_return;
   }

   if(!hostname)
      return m;

   switch(state)
   {
   case(INITIAL_STATE):
   {
      // walk through ftp classes and try to find identical idle ftp session
      // first try "easy" cases of session take-over.
      int count=CountConnections();
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>count))
	    break;
	 GetBetterConnection(i,count);
	 if(state!=INITIAL_STATE)
	    return MOVED;
      }

      if(!ReconnectAllowed())
	 return m;

      if(ftps)
	 m|=Resolve(FTPS_DEFAULT_PORT,"ftps","tcp");
      else
	 m|=Resolve(FTP_DEFAULT_PORT,"ftp","tcp");
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
	 sprintf(str,_("cannot create socket of address family %d"),
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
      state=CONNECTING_STATE;
      if(res==-1
#ifdef EINPROGRESS
      && errno!=EINPROGRESS
#endif
      )
      {
	 sprintf(str,"connect: %s",strerror(errno));
         DebugPrint("**** ",str,0);
	 if(NotSerious(errno))
	 {
	    Disconnect();
	    return MOVED;
	 }
	 goto system_error;
      }
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
      assert(control_sock!=-1);
      res=Poll(control_sock,POLLOUT);
      if(res==-1)
      {
	 Disconnect();
	 return MOVED;
      }
      if(!(res&POLLOUT))
	 goto usual_return;

#ifdef USE_SSL
      if(proxy?!strncmp(proxy,"ftps://",7):ftps)
      {
	 control_ssl=lftp_ssl_new(control_sock);
	 control_ssl_connected=false;
	 prot='P';
      }
#endif

      state=CONNECTED_STATE;
      m=MOVED;
      sync_wait=1; // we need to wait for RESP_READY
      AddResp(RESP_READY,CHECK_READY);

#ifdef USE_SSL
      // ssl for anonymous does not make sense.
      if(!ftps && (bool)Query("ssl-allow") && user && pass)
      {
	 const char *auth=Query("ssl-auth");
	 SendCmd2("AUTH",auth);
	 AddResp(234,CHECK_AUTH_TLS);
	 auth_tls_sent=true;
	 if(!strcmp(auth,"TLS")
	 || !strcmp(auth,"TLS-C"))
	    prot='C';
	 else
	    prot='P';
      }
#endif
      /* fallthrough */
   case CONNECTED_STATE:
   {
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=CONNECTED_STATE || Error())
	 return MOVED;

#ifdef USE_SSL
      if(auth_tls_sent && !RespQueueIsEmpty())
	 goto usual_return;
      if(control_ssl)
      {
	 SendCmd("PBSZ 0");
	 AddResp(0,CHECK_IGNORE);
	 const char *want_prot=(bool)Query("ssl-protect-data",hostname)?"P":"C";
	 if(*want_prot!=prot)
	 {
	    SendCmd2("PROT",want_prot);
	    AddResp(200,CHECK_PROT);
	 }
      }
#endif // USE_SSL

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
	    AddResp(RESP_PASS_REQ,CHECK_USER_PROXY);
	    SendCmd2("USER",proxy_user);
	    AddResp(RESP_LOGGED_IN,CHECK_PASS_PROXY);
	    SendCmd2("PASS",proxy_pass);
	 }
      }

      xfree(skey_pass);
      skey_pass=0;

      ignore_pass=false;
      AddResp(RESP_PASS_REQ,CHECK_USER,allow_skey);
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
	 if(state!=USER_RESP_WAITING_STATE || Error())
	    return MOVED;
	 if(!RespQueueIsEmpty())
	    goto usual_return;
      }

      if(!ignore_pass)
      {
	 const char *pass_to_use=pass?pass:anon_pass;
	 if(allow_skey && skey_pass)
	    pass_to_use=skey_pass;
	 AddResp(RESP_LOGGED_IN,CHECK_PASS);
	 SendCmd2("PASS",pass_to_use);
      }

      SendAcct();
      SendSiteGroup();
      SendSiteIdle();

      if(!home_auto)
      {
	 // if we don't yet know the home location, try to get it
	 SendCmd("PWD");
	 AddResp(RESP_PWD_MKD_OK,CHECK_PWD);
      }

      set_real_cwd(0);
      type=FTP_TYPE_A;	   // just after login we are in TEXT mode
      state=EOF_STATE;
      m=MOVED;

   case(EOF_STATE):
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=EOF_STATE || Error())
	 return MOVED;

      if(mode==CONNECT_VERIFY)
	 goto notimeout_return;

      if(mode==CHANGE_MODE && !site_chmod_supported)
      {
	 SetError(NO_FILE,_("SITE CHMOD is not supported by this site"));
	 return MOVED;
      }

      if(takeover_time!=NO_DATE && takeover_time+1-priority>now
      && connection_limit>0 && connection_limit<=CountConnections()+1)
      {
	 TimeoutS(takeover_time+1-priority-now);
	 goto notimeout_return;
      }
      takeover_time=NO_DATE;

      assert(peer!=0);
      assert(peer_curr<peer_num);

      if(home==0 && !RespQueueIsEmpty())
	 goto usual_return;

      if(real_cwd==0)
	 set_real_cwd(home_auto);

      ExpandTildeInCWD();

      if(mode!=CHANGE_DIR)
      {
	 expected_response *last_cwd=FindLastCWD();
	 if((!last_cwd && xstrcmp(cwd,real_cwd))
	    || (last_cwd && xstrcmp(last_cwd->path,cwd)))
	 {
	    SendCmd2("CWD",cwd);
	    AddResp(RESP_CWD_RMD_DELE_OK,CHECK_CWD_CURR);
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
      if(state!=CWD_CWD_WAITING_STATE || Error())
	 return MOVED;

      // wait for all CWD to finish
      if(mode!=CHANGE_DIR && FindLastCWD())
	 goto usual_return;

      // address of peer is not known yet
      if(copy_mode!=COPY_NONE && !copy_passive && !copy_addr_valid)
	 goto usual_return;

      if(entity_size!=NO_SIZE && entity_size!=NO_SIZE_YET && entity_size<=pos)
      {
	 eof=true;
	 goto pre_WAITING_STATE; // simulate eof.
      }

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

	 if(flags&PASSIVE_MODE)
	 {
	    if((bool)Query("bind-data-socket")
	    && !IsLoopback(&peer_sa))
	    {
	       // connect should come from the same address, else server can refuse.
	       addr_len=sizeof(data_sa);
	       getsockname(control_sock,&data_sa.sa,&addr_len);
	       if(data_sa.sa.sa_family==AF_INET)
		  data_sa.in.sin_port=0;
      #if INET6
	       else if(data_sa.sa.sa_family==AF_INET6)
		  data_sa.in6.sin6_port=0;
      #endif
	       if(bind(data_sock,&data_sa.sa,addr_len)<0)
	       {
		  sprintf(str,"bind(data_sock): %s",strerror(errno));
		  DebugPrint("**** ",str,0);
	       }
	    }
	 }
	 else // !PASSIVE_MODE
	 {
	    addr_len=sizeof(data_sa);
	    if(copy_mode!=COPY_NONE)
	       data_sa=copy_addr;
	    else
	    {
	       getsockname(control_sock,&data_sa.sa,&addr_len);

	       Range range(Query("port-range"));

	       for(int t=0; ; t++)
	       {
		  if(t>=10)
		  {
		     close(data_sock);
		     data_sock=-1;
		     TimeoutS(10);	 // retry later.
		     return m;
		  }
		  if(t==9)
		     ReuseAddress(data_sock);   // try to reuse address.

		  int port=0;
		  if(!range.IsFull())
		     port=range.Random();

		  if(data_sa.sa.sa_family==AF_INET)
		     data_sa.in.sin_port=htons(port);
   #if INET6
		  else if(data_sa.sa.sa_family==AF_INET6)
		     data_sa.in6.sin6_port=htons(port);
   #endif
		  else
		  {
		     Fatal("unsupported network protocol");
		     return MOVED;
		  }

		  if(bind(data_sock,&data_sa.sa,addr_len)==0)
		     break;

		  // Fail unless socket was already taken
		  if(errno!=EINVAL && errno!=EADDRINUSE)
		  {
		     sprintf(str,"bind: %s",strerror(errno));
		     DebugPrint("**** ",str,0);
		     goto system_error;
		  }
	       }
	       // get the allocated port
	       getsockname(data_sock,&data_sa.sa,&addr_len);
	       listen(data_sock,1);
	    }
	 }
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
	 if(file[0]==0)
	    goto long_list;
         type=FTP_TYPE_I;
	 command="RETR";
	 append_file=true;
         break;
      case(STORE):
         type=FTP_TYPE_I;
	 if(!(bool)Query("rest-stor",hostname))
	    real_pos=0;	// some old servers don't handle REST/STOR properly.
         command="STOR";
	 append_file=true;
         break;
      long_list:
      case(LONG_LIST):
         type=FTP_TYPE_A;
         if(!rest_list)
	    real_pos=0;	// some ftp servers do not do REST/LIST.
	 command="LIST";
	 if(list_options && list_options[0])
	 {
	    char *c=string_alloca(5+strlen(list_options)+1);
	    sprintf(c,"LIST %s",list_options);
	    command=c;
	 }
	 if(file && file[0])
	    append_file=true;
         break;
      case(LIST):
         type=FTP_TYPE_A;
         real_pos=0; // REST doesn't work for NLST
	 command="NLST";
	 if(file && file[0])
            append_file=true;
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
	    char *path_to_use=file;
	    if(!vms_path && !AbsolutePath(file) && real_cwd
	    && !strncmp(file,real_cwd,len) && file[len]=='/')
	       path_to_use=file+len+1;
	    SendCmd2("CWD",path_to_use);
	    AddResp(RESP_CWD_RMD_DELE_OK,CHECK_CWD);
	    SetRespPath(file);
	 }
	 goto pre_WAITING_STATE;
      case(MAKE_DIR):
	 command="MKD";
	 append_file=true;
	 break;
      case(REMOVE_DIR):
	 command="RMD";
	 append_file=true;
	 break;
      case(REMOVE):
	 command="DELE";
	 append_file=true;
	 break;
      case(QUOTE_CMD):
	 real_pos=0;
	 command="";
	 append_file=true;
	 break;
      case(RENAME):
	 command="RNFR";
	 append_file=true;
	 break;
      case(ARRAY_INFO):
	 type=FTP_TYPE_I;
	 break;
      case(CHANGE_MODE):
	 {
	    char *c=string_alloca(11+30);
	    sprintf(c,"SITE CHMOD %03o",chmod_mode);
	    command=c;
	    append_file=true;
	    break;
	 }
      case(CONNECT_VERIFY):
      case(CLOSED):
	 abort(); // can't happen
      }
      if(ascii)
	 type=FTP_TYPE_A;
      if(old_type!=type)
      {
	 SendCmd2("TYPE",type==FTP_TYPE_I?"I":"A");
	 AddResp(RESP_TYPE_OK);
      }

      if(opt_size && size_supported && file[0])
      {
	 SendCmd2("SIZE",file);
	 AddResp(RESP_RESULT_HERE,CHECK_SIZE_OPT);
      }
      if(opt_date && mdtm_supported && file[0])
      {
	 SendCmd2("MDTM",file);
	 AddResp(RESP_RESULT_HERE,CHECK_MDTM_OPT);
      }

      if(mode==ARRAY_INFO)
      {
	 SendArrayInfoRequests();
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
		  AddResp(0,CHECK_IGNORE);
		  *sl='/';
	       }
	       sl=strchr(sl+1,'/');
	    }
	 }

	 if(append_file)
	    SendCmd2(command,file);
	 else
	    SendCmd(command);

	 if(mode==REMOVE_DIR || mode==REMOVE)
	    AddResp(RESP_CWD_RMD_DELE_OK,CHECK_FILE_ACCESS);
	 else if(mode==MAKE_DIR)
	    AddResp(RESP_PWD_MKD_OK,CHECK_FILE_ACCESS);
	 else if(mode==QUOTE_CMD)
	    AddResp(0,CHECK_IGNORE,true);
	 else if(mode==RENAME)
	    AddResp(350,CHECK_RNFR);
	 else
	    AddResp(200,CHECK_FILE_ACCESS);

	 FreeResult();
	 goto pre_WAITING_STATE;
      }

      if((copy_mode==COPY_NONE && (flags&PASSIVE_MODE))
      || (copy_mode!=COPY_NONE && copy_passive))
      {
	 if(peer_sa.sa.sa_family==AF_INET)
	 {
#if INET6
	 ipv4_pasv:
#endif
	    SendCmd("PASV");
	    AddResp(227,CHECK_PASV);
	    addr_received=0;
	 }
	 else
	 {
#if INET6
	    if(peer_sa.sa.sa_family==AF_INET6
	    && IN6_IS_ADDR_V4MAPPED(&peer_sa.in6.sin6_addr))
	       goto ipv4_pasv;
	    SendCmd("EPSV");
	    AddResp(229,CHECK_EPSV);
	    addr_received=0;
#else
	    Fatal(_("unsupported network protocol"));
	    return MOVED;
#endif
	 }
      }
      else
      {
	 if(copy_mode!=COPY_NONE)
	    data_sa=copy_addr;
	 if(data_sa.sa.sa_family==AF_INET)
	 {
	    a=(unsigned char*)&data_sa.in.sin_addr;
	    p=(unsigned char*)&data_sa.in.sin_port;
#if INET6
	 ipv4_port:
#endif
	    const char *port_ipv4=Query("port-ipv4",hostname);
	    struct in_addr fake_ip;
	    if(port_ipv4 && port_ipv4[0])
	    {
	       if(inet_aton(port_ipv4,&fake_ip))
		  a=(unsigned char*)&fake_ip;
	    }
	    sprintf(str,"PORT %d,%d,%d,%d,%d,%d\n",a[0],a[1],a[2],a[3],p[0],p[1]);
	    SendCmd(str);
	    AddResp(RESP_PORT_OK,CHECK_PORT);
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
	    AddResp(RESP_PORT_OK,CHECK_PORT);
#else
	    Fatal(_("unsupported network protocol"));
	    return MOVED;
#endif
	 }
      }
      // some broken servers don't reset REST after a transfer,
      // so check if last_rest was different.
      if(real_pos==-1 || last_rest!=real_pos)
      {
         rest_pos=real_pos!=-1?real_pos:pos;
	 sprintf(str,"REST %lld\n",(long long)rest_pos);
	 real_pos=-1;
	 SendCmd(str);
	 AddResp(RESP_REST_OK,CHECK_REST);
      }
      if(copy_mode!=COPY_DEST || copy_allow_store)
      {
	 if(append_file)
	    SendCmd2(command,file);
	 else
	    SendCmd(command);
	 AddResp(RESP_TRANSFER_OK,CHECK_TRANSFER);
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

      if(state!=ACCEPTING_STATE || Error())
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

      if(state!=DATASOCKET_CONNECTING_STATE || Error())
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
	 if(fixed_pasv)
	 {
	    DebugPrint("---- ",_("Switching passive mode off"),2);
	    SetFlag(PASSIVE_MODE,0);
	 }
	 Disconnect();
         return MOVED;
      }

      if(!(res&POLLOUT))
	 goto usual_return;

   pre_data_open:
      assert(rate_limit==0);
      rate_limit=new RateLimit(hostname);
      state=DATA_OPEN_STATE;
      m=MOVED;

#ifdef USE_SSL
      if(prot=='P')
      {
	 data_ssl=lftp_ssl_new(data_sock);
	 // share session id between control and data connections.
	 SSL_copy_session_id(data_ssl,control_ssl);
	 data_ssl_connected=false;
      }
#endif

   case(DATA_OPEN_STATE):
   {
      if(RespQueueIsEmpty() && data_sock!=-1)
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
	       HandleTimeout();
	       return MOVED;
	    }
	    if(nop_time!=0)
	    {
	       nop_count++;
	       SendCmd("NOOP");
	       AddResp(0,CHECK_IGNORE);
	    }
	    nop_time=now;
	    if(nop_offset!=pos)
	       nop_count=0;
	    nop_offset=pos;
	 }
	 TimeoutS(nop_interval-(now-nop_time));
      }

      oldstate=state;

      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=oldstate || Error())
         return MOVED;

      CheckTimeout();

      if(state!=oldstate)
         return MOVED;

      goto usual_return;
   }

   pre_WAITING_STATE:
      if(copy_mode!=COPY_NONE)
      {
	 retries=0;  // it is enough to get here in copying.
	 persist_retries=0;
      }
      state=WAITING_STATE;
      m=MOVED;
   case(WAITING_STATE):
   {
      oldstate=state;

      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=oldstate || Error())
         return MOVED;

      // more work to do?
      if(RespQueueIsEmpty() && mode==ARRAY_INFO && array_ptr<array_cnt)
      {
	 SendArrayInfoRequests();
	 return MOVED;
      }

      if(copy_mode==COPY_DEST && !copy_allow_store)
	 goto notimeout_return;

      if(copy_mode==COPY_DEST && !copy_done && copy_connection_open
      && RespQueueSize()==1 && use_stat && !control_ssl)
      {
	 if(stat_time+stat_interval<=now)
	 {
	    // send STAT to know current position.
	    SendUrgentCmd("STAT");
	    AddResp(213,CHECK_TRANSFER);
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
   } /* end of switch */
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
	 assert(rate_limit!=0);
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
   quit_sent=true;
   Disconnect();
   SetError(SEE_ERRNO,0);
   return MOVED;
}

void Ftp::SendSiteIdle()
{
   if(!(bool)Query("use-site-idle"))
      return;
   SendCmd2("SITE IDLE",idle);
   AddResp(0,CHECK_IGNORE);
}
const char *Ftp::QueryStringWithUserAtHost(const char *var)
{
   const char *u=user?user:"anonymous";
   const char *h=hostname?hostname:"";
   char *closure=string_alloca(strlen(u)+1+strlen(h)+1);
   sprintf(closure,"%s@%s",u,h);
   const char *val=Query(var,closure);
   if(!val || !val[0])
      val=Query(var,hostname);
   if(!val || !val[0])
      return 0;
   return val;
}
void Ftp::SendAcct()
{
   const char *acct=QueryStringWithUserAtHost("acct");
   if(!acct)
      return;
   SendCmd2("ACCT",acct);
   AddResp(0,CHECK_IGNORE);
}
void Ftp::SendSiteGroup()
{
   const char *group=QueryStringWithUserAtHost("site-group");
   if(!group)
      return;
   SendCmd2("SITE GROUP",group);
   AddResp(0,CHECK_IGNORE);
}

void Ftp::SendArrayInfoRequests()
{
   for(int i=array_ptr; i<array_cnt; i++)
   {
      bool sent=false;
      if(array_for_info[i].get_time && mdtm_supported)
      {
	 SendCmd2("MDTM",ExpandTildeStatic(array_for_info[i].file));
	 AddResp(RESP_RESULT_HERE,CHECK_MDTM);
	 sent=true;
      }
      else
      {
	 array_for_info[i].time=NO_DATE;
      }
      if(array_for_info[i].get_size && size_supported)
      {
	 SendCmd2("SIZE",ExpandTildeStatic(array_for_info[i].file));
	 AddResp(RESP_RESULT_HERE,CHECK_SIZE);
	 sent=true;
      }
      else
      {
	 array_for_info[i].size=-1;
      }
      if(!sent)
      {
	 if(i==array_ptr)
	    array_ptr++;   // if it is first one, just skip it.
	 else
	    break;	   // otherwise, wait until it is first.
      }
      else
      {
	 if(flags&SYNC_MODE)
	    break;	   // don't flood the queues.
      }
   }
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
   if(code==550 && mode==ARRAY_INFO
   && !RespQueueIsEmpty() && RespQueue[RQ_head].check_case==CHECK_MDTM)
      return 4;
   // Error messages
   // 221 is the reply to QUIT, but we don't expect it.
   if(code>=400 || (code==221 && RespQueue[RQ_head].expect!=221))
      return 0;
   return 4;
}

#ifdef USE_SSL
void Ftp::BlockOnSSL(SSL *ssl)
{
   int fd=SSL_get_fd(ssl);
   if(SSL_want_read(ssl))
      current->Block(fd,POLLIN);
   if(SSL_want_write(ssl))
      current->Block(fd,POLLOUT);
}
#endif

int  Ftp::ReceiveResp()
{
   char  *store;
   char  *nl;
   int   code;
   int	 m=STALL;
   int	 res;

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
	    m=MOVED;

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

	    int all_lines_len=xstrlen(all_lines);
	    if(multiline_code==0 || all_lines_len==0)
	       all_lines_len=-1; // not continuation
	    all_lines=(char*)xrealloc(all_lines,all_lines_len+1+strlen(line)+1);
	    if(all_lines_len>0)
	       all_lines[all_lines_len]='\n';
	    strcpy(all_lines+all_lines_len+1,line);

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
#ifdef USE_SSL
	    if(control_ssl)
	    {
	       if(!control_ssl_connected)
	       {
		  res=SSL_connect(control_ssl);
		  if(res<=0)
		  {
		     if(BIO_sock_should_retry(res))
		     {
			BlockOnSSL(control_ssl);
			return m;
		     }
		     else if (SSL_want_x509_lookup(control_ssl))
			return m;
		     else // error
		     {
			SetError(FATAL,lftp_ssl_strerror("SSL connect"));
			return MOVED;
		     }
		  }
		  control_ssl_connected=true;
	       }
	       res=SSL_read(control_ssl,resp+resp_size,resp_alloc-resp_size-1);
	       if(res<0)
	       {
		  if(BIO_sock_should_retry(res))
		  {
		     BlockOnSSL(control_ssl);
		     return m;
		  }
		  if(NotSerious(errno))
		     DebugPrint("**** ",strerror(errno),0);
		  else
		     SetError(SEE_ERRNO,"SSL_read(control_ssl)");
		  quit_sent=true;
		  Disconnect();
		  return MOVED;
	       }
	    }
	    else  // note the following block
#endif // USE_SSL
	    {
	       res=read(control_sock,resp+resp_size,resp_alloc-resp_size-1);
	       if(res==-1)
	       {
		  if(E_RETRY(errno))
		     return m;
		  if(NotSerious(errno))
		     DebugPrint("**** ",strerror(errno),0);
		  else
		     SetError(SEE_ERRNO,"read(control_socket)");
		  quit_sent=true;
		  Disconnect();
		  return MOVED;
	       }
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

      CheckResp(code);
      m=MOVED;
      if(error_code==NO_FILE || error_code==LOGIN_FAILED)
      {
	 if(error_code==LOGIN_FAILED)
	    try_time=now;	// count the reconnect-interval from this moment
	 if(persist_retries++<max_persist_retries)
	 {
	    error_code=OK;
	    Disconnect();
	    DebugPrint("---- ",_("Persist and retry"),4);
	    return m;
	 }
      }

      if(resp_size==0)
	 return m;
   }
   return m;
}

void Ftp::SendUrgentCmd(const char *cmd)
{
   assert(!control_ssl);   // no way to send urgent data over ssl
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
	 quit_sent=true;
	 Disconnect();
	 return;
      }
   }
   copy_connection_open=false;

   // if transfer has been completed then ABOR is not needed
   if(data_sock!=-1 && RespQueueIsEmpty())
      return;

   CloseRespQueue();

   if(!(bool)Query("use-abor",hostname) || control_ssl
   || RespQueueSize()>1)
   {
      if(copy_mode==COPY_NONE
      && !((flags&PASSIVE_MODE) && addr_received<2))
	 DataClose();	// just close data connection
      else
      {
	 quit_sent=true;
	 Disconnect();	// nothing to close but control connection.
      }
      return;
   }

   if(aborted_data_sock!=-1)  // don't allow double ABOR.
   {
      quit_sent=true;
      Disconnect();
      return;
   }

   SendUrgentCmd("ABOR");
   AddResp(226,CHECK_ABOR);
   FlushSendQueue(true);
   AbortedClose();
   // don't close it now, wait for ABOR result
   aborted_data_sock=data_sock;
   data_sock=-1;

   if((bool)Query("web-mode"))
      Disconnect();
}

void Ftp::ControlClose()
{
#ifdef USE_SSL
   if(control_ssl)
   {
      SSL_free(control_ssl);
      control_ssl=0;
      control_ssl_connected=false;
      prot='C';	  // current protection scheme 'C'lear or 'P'rivate
      auth_tls_sent=false;
   }
#endif
   if(control_sock!=-1)
   {
      DebugPrint("---- ",_("Closing control socket"),7);
      close(control_sock);
      control_sock=-1;
   }
   resp_size=0;
   EmptyRespQueue();
   EmptySendQueue();
   quit_sent=false;
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
   /* protect against re-entering from FlushSendQueue */
   static bool disconnect_in_progress=false;
   if(disconnect_in_progress)
      return;
   disconnect_in_progress=true;

   DataAbort();
   DataClose();
   if(control_sock>=0 && state!=CONNECTING_STATE && !quit_sent
   && RespQueueSize()<2 && (bool)Query("use-quit",hostname))
   {
      SendCmd("QUIT");
      AddResp(221);
      quit_sent=true;
      goto out;
   }
   ControlClose();
   AbortedClose();

   if(state==CONNECTING_STATE)
      NextPeer();

   if(copy_mode!=COPY_NONE)
   {
      if(copy_addr_valid)
	 copy_failed=true;
   }
   else
   {
      if(mode==STORE && (flags&IO_FLAG))
	 SetError(STORE_FAILED,0);
   }
   state=INITIAL_STATE;

out:
   Timeout(0);

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
#ifdef USE_SSL
   if(data_ssl)
   {
      SSL_free(data_ssl);
      data_ssl=0;
      data_ssl_connected=false;
   }
#endif
   if(data_sock>=0)
   {
      DebugPrint("---- ",_("Closing data socket"),7);
      close(data_sock);
      data_sock=-1;
      if((bool)Query("web-mode"))
	 Disconnect();
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
   fixed_pasv=false;
   if(state==DATA_OPEN_STATE)
      state=WAITING_STATE;
}

int  Ftp::FlushSendQueue(bool all)
{
   int res;
   int m=STALL;

   char *cmd_begin=send_cmd_ptr;

   while(send_cmd_count>0 && (all || (flags&SYNC_MODE)==0 || sync_wait==0))
   {
      int to_write=send_cmd_count;

      char *line_end=(char*)memchr(send_cmd_ptr,'\n',send_cmd_count);
      if(line_end==NULL)
	 return m;
      to_write=line_end+1-send_cmd_ptr;

#ifdef USE_SSL
      if(control_ssl)
      {
	 if(!control_ssl_connected)
	 {
	    res=SSL_connect(control_ssl);
	    if(res<=0)
	    {
	       if(BIO_sock_should_retry(res))
	       {
		  BlockOnSSL(control_ssl);
		  return m;
	       }
	       else if (SSL_want_x509_lookup(control_ssl))
		  return m;
	       else // error
	       {
		  SetError(FATAL,lftp_ssl_strerror("SSL connect"));
		  return MOVED;
	       }
	    }
	    control_ssl_connected=true;
	 }
	 res=SSL_write(control_ssl,send_cmd_ptr,to_write);
	 if(res<=0)
	 {
	    if(BIO_sock_should_retry(res))
	    {
	       BlockOnSSL(control_ssl);
	       return m;
	    }
	    if(NotSerious(errno))
	       DebugPrint("**** ",strerror(errno),0);
	    else
	       SetError(SEE_ERRNO,"SSL_write(control_ssl)");
	    quit_sent=true;
	    Disconnect();
	    return MOVED;
	 }
      }
      else  // note the following block
#endif // USE_SSL
      {
	 res=write(control_sock,send_cmd_ptr,to_write);
	 if(res==0)
	    return m;
	 if(res==-1)
	 {
	    if(E_RETRY(errno))
	       return m;
	    if(NotSerious(errno) || errno==EPIPE)
	       DebugPrint("**** ",strerror(errno),0);
	    else
	       SetError(SEE_ERRNO,"write(control_socket)");
	    quit_sent=true;
	    Disconnect();
	    return MOVED;
	 }
      }
      send_cmd_count-=res;
      send_cmd_ptr+=res;
      event_time=now;

      sync_wait++;
   }
   // FIXME: \0 in commands can make logging fail.
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

void  Ftp::SendCmd(const char *cmd,int len)
{
   if(len==-1)
      len=strlen(cmd);
   char ch,prev_ch;
   if(send_cmd_count==0)
      send_cmd_ptr=send_cmd_buffer;
   prev_ch=0;
   while(len>0)
   {
      ch=*cmd++;
      len--;
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
	 len++;
      }
      else if(ch=='\377') // double chr(255) as in telnet protocol
      	 send_cmd_ptr[send_cmd_count++]='\377';
      send_cmd_ptr[send_cmd_count++]=prev_ch=ch;
      if(len==0 && ch!='\n')
      {
	 cmd="\n";
	 len=1;
      }
   }
}

void Ftp::SendCmd2(const char *cmd,const char *f)
{
   char *s=string_alloca(strlen(cmd)+1+strlen(f)+2);
   strcpy(s,cmd);
   char *store=s+strlen(s);
   if(store>s)
      *store++=' ';
   while(*f)
   {
      if(*f=='\n')
	 *store++='\0';
      else
	 *store++=*f;
      f++;
   }
   SendCmd(s,store-s);
}

void Ftp::SendCmd2(const char *cmd,int v)
{
   char buf[32];
   sprintf(buf,"%d",v);
   SendCmd2(cmd,buf);
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
   eof=false;

   Resume();
   ExpandTildeInCWD();
   DataAbort();
   DataClose();
   if(control_sock!=-1)
   {
      switch(state)
      {
      case(CONNECTING_STATE):
      case(CONNECTED_STATE):
      case(USER_RESP_WAITING_STATE):
	 Disconnect();
	 break;
      case(ACCEPTING_STATE):
      case(DATASOCKET_CONNECTING_STATE):
      case(CWD_CWD_WAITING_STATE):
      case(WAITING_STATE):
      case(DATA_OPEN_STATE):
	 state=(control_sock==-1 ? INITIAL_STATE : EOF_STATE);
	 break;
      case(INITIAL_STATE):
      case(EOF_STATE):
	 break;
      }
   }
   else
   {
      state=INITIAL_STATE;
   }
   copy_mode=COPY_NONE;
   copy_addr_valid=false;
   copy_done=false;
   copy_connection_open=false;
   stat_time=0;
   copy_allow_store=false;
   copy_failed=false;
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
      case(CHECK_PASV):
      case(CHECK_EPSV):
      case(CHECK_TRANSFER_CLOSED):
#ifdef USE_SSL
      case(CHECK_AUTH_TLS):
      case(CHECK_PROT):
#endif
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
      case(CHECK_PORT):
      case(CHECK_FILE_ACCESS):
      case(CHECK_RNFR):
	 RespQueue[i].check_case=CHECK_IGNORE;
	 break;
      case(CHECK_TRANSFER):
	 RespQueue[i].check_case=CHECK_TRANSFER_CLOSED;
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
   if(Error())
      return(error_code);

   if(mode==CLOSED || eof)
      return(0);

   if(state==WAITING_STATE && RespQueueIsEmpty())
   {
      if(result_size==0)
      {
	 state=EOF_STATE;
	 DataAbort();
	 DataClose();
	 set_idle_start();
	 eof=true;
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

   {
      assert(rate_limit!=0);
      int allowed=rate_limit->BytesAllowed();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(norest_manual && real_pos==0 && pos>0)
      return DO_AGAIN;
#ifdef USE_SSL
   if(data_ssl)
   {
      if(!data_ssl_connected)
      {
	 res=SSL_connect(data_ssl);
	 if(res<=0)
	 {
	    if(BIO_sock_should_retry(res))
	    {
	       BlockOnSSL(control_ssl);
	       return DO_AGAIN;
	    }
	    else if (SSL_want_x509_lookup(data_ssl))
	       return DO_AGAIN;
	    else // error
	    {
	       SetError(FATAL,lftp_ssl_strerror("SSL connect"));
	       return error_code;
	    }
	 }
	 data_ssl_connected=true;
      }
      res=SSL_read(data_ssl,(char*)buf,size);
      if(res<0)
      {
	 if(BIO_sock_should_retry(res))
	 {
	    BlockOnSSL(control_ssl);
	    return DO_AGAIN;
	 }
	 if(NotSerious(errno))
	    DebugPrint("**** ",strerror(errno),0);
	 else
	    SetError(SEE_ERRNO,"SSL_read(data_ssl)");
	 quit_sent=true;
	 Disconnect();
	 return(error_code);
      }
   }
   else  // note the following block
#endif // USE_SSL
   {
      res=read(data_sock,buf,size);
      if(res==-1)
      {
	 if(E_RETRY(errno))
	    return DO_AGAIN;
	 if(NotSerious(errno))
	 {
	    DebugPrint("**** ",strerror(errno),0);
	    quit_sent=true;
	    Disconnect();
	    return DO_AGAIN;
	 }
	 SetError(SEE_ERRNO,"read(data_socket)");
	 quit_sent=true;
	 Disconnect();
	 return(error_code);
      }
   }
   event_time=now;
   if(res==0)
   {
   we_have_eof:
      DataClose();
      if(RespQueueIsEmpty())
      {
	 eof=true;
	 return(0);
      }
      else
	 return DO_AGAIN;
   }
   retries=0;
   persist_retries=0;
   assert(rate_limit!=0);
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
   Since that, we have to leave re-putting up to caller
*/
int   Ftp::Write(const void *buf,int size)
{
   int res;

   if(mode!=STORE)
      return(0);

   Resume();
   Do();
   if(Error())
      return(error_code);

   if(state!=DATA_OPEN_STATE || (RespQueueSize()>1 && real_pos==-1))
      return DO_AGAIN;

   {
      assert(rate_limit!=0);
      int allowed=rate_limit->BytesAllowed();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size==0)
      return 0;
#ifdef USE_SSL
   if(data_ssl)
   {
      if(!data_ssl_connected)
      {
	 res=SSL_connect(data_ssl);
	 if(res<=0)
	 {
	    if(BIO_sock_should_retry(res))
	    {
	       BlockOnSSL(control_ssl);
	       return DO_AGAIN;
	    }
	    else if (SSL_want_x509_lookup(data_ssl))
	       return DO_AGAIN;
	    else // error
	    {
	       SetError(FATAL,lftp_ssl_strerror("SSL connect"));
	       return error_code;
	    }
	 }
	 data_ssl_connected=true;
      }
      res=SSL_write(data_ssl,(char*)buf,size);
      if(res<=0)
      {
	 if(BIO_sock_should_retry(res))
	 {
	    BlockOnSSL(control_ssl);
	    return DO_AGAIN;
	 }
	 if(NotSerious(errno))
	    DebugPrint("**** ",strerror(errno),0);
	 else
	    SetError(SEE_ERRNO,"SSL_write(data_ssl)");
	 quit_sent=true;
	 Disconnect();
	 return(error_code);
      }
   }
   else  // note the following block
#endif // USE_SSL
   {
      res=write(data_sock,buf,size);
      if(res==-1)
      {
	 if(E_RETRY(errno))
	    return DO_AGAIN;
	 if(NotSerious(errno) || errno==EPIPE)
	 {
	    DebugPrint("**** ",strerror(errno),0);
	    quit_sent=true;
	    Disconnect();
	    return DO_AGAIN;
	 }
	 SetError(SEE_ERRNO,"write(data_socket)");
	 quit_sent=true;
	 Disconnect();
	 return(error_code);
      }
   }
   event_time=now;
   retries=0;
   persist_retries=0;
   assert(rate_limit!=0);
   rate_limit->BytesUsed(res);
   pos+=res;
   real_pos+=res;
   flags|=IO_FLAG;
   return(res);
}

int   Ftp::StoreStatus()
{
   if(Error())
      return(error_code);

   if(mode!=STORE)
      return(OK);

   if(state==WAITING_STATE && RespQueueIsEmpty())
   {
      eof=true;
      return(OK);
   }
   if(state==DATA_OPEN_STATE)
   {
      // have not send EOT by SendEOT, do it now
      SendEOT();
   }
   return(IN_PROGRESS);
}

void  Ftp::AddResp(int exp,check_case_t ck,bool log)
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
   RespQueue[RQ_tail].check_case=ck;
   RespQueue[RQ_tail].log_resp=log;
   RespQueue[RQ_tail].path=0;
   RQ_tail=newtail;
}

void  Ftp::EmptyRespQueue()
{
   while(!RespQueueIsEmpty())
      PopResp();
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

   RQ_head=o->RQ_head; o->RQ_head=0;
   RQ_tail=o->RQ_tail; o->RQ_tail=0;
   multiline_code=o->multiline_code; o->multiline_code=0;
   RespQueue=o->RespQueue; o->RespQueue=0;
   RQ_alloc=o->RQ_alloc; o->RQ_alloc=0;

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

#ifdef USE_SSL
   control_ssl=o->control_ssl;
   o->control_ssl=0;
   control_ssl_connected=o->control_ssl_connected;
   prot=o->prot;
   auth_tls_sent=o->auth_tls_sent;
#endif

   size_supported=o->size_supported;
   mdtm_supported=o->mdtm_supported;
   site_chmod_supported=o->site_chmod_supported;
   last_rest=o->last_rest;

   if(!home)
      set_home(home_auto);

   set_real_cwd(o->real_cwd);
   o->set_real_cwd(0);
   o->Disconnect();
   state=EOF_STATE;
}

void Ftp::CheckResp(int act)
{
   if(act==150 && flags&PASSIVE_MODE && aborted_data_sock!=-1)
      AbortedClose();

   if(act==150 && state==WAITING_STATE && RespQueueSize()==1)
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
	 long long size_ll;
	 if(1==sscanf(s+1,"%lld",&size_ll))
	 {
	    *opt_size=size_ll;
	    DebugPrint("---- ",_("saw file size in response"),7);
	 }
      }
   }

   if(is1XX(act)) // intermediate responses are ignored
      return;

   if(act==421 || act==221)  // timeout or something else
   {
      if(RespQueueIsEmpty() || RespQueue[RQ_head].expect!=221)
	 DebugPrint("**** ",_("remote end closes connection"),3);
      quit_sent=true;
      Disconnect();
      return;
   }

   if(RespQueueIsEmpty())
   {
      DebugPrint("**** ",_("extra server response"),3);
      if(is2XX(act)) // some buggy servers send several 226 replies
	 return;     // ignore them.
      Disconnect();
      return;
   }

   int exp=RespQueue[RQ_head].expect;

   // some servers mess all up
   if(act==331 && exp==220 && !(flags&SYNC_MODE) && RespQueueSize()>1)
   {
      DebugPrint("---- ",_("Turning on sync-mode"),2);
      ResMgr::Set("ftp:sync-mode",hostname,"on");
      Disconnect();
      try_time=0; // retry immediately
      return;
   }

   bool match=(act/100==exp/100);
   check_case_t cc=RespQueue[RQ_head].check_case;

   switch(cc)
   {
   case CHECK_NONE:
      if(match) // default rule.
	 break;
      Disconnect();
      break;

   case CHECK_IGNORE:
   ignore:
      break;

   case CHECK_READY:
      if(!(flags&SYNC_MODE) && re_match(all_lines,Query("auto-sync-mode",hostname)))
      {
	 DebugPrint("---- ",_("Turning on sync-mode"),2);
	 ResMgr::Set("ftp:sync-mode",hostname,"on");
	 assert(flags&SYNC_MODE);
	 Disconnect();
	 try_time=0; // retry immediately
      }
      if(!match)
      {
	 Disconnect();
	 NextPeer();
	 if(peer_curr==0)
	    try_time=now;  // count the reconnect-interval from this moment
      }
      break;

   case CHECK_REST:
      RestCheck(act);
      break;

   case CHECK_CWD:
   case CHECK_CWD_CURR:
      if(is2XX(act))
      {
	 if(cc==CHECK_CWD)
	 {
	    xfree(cwd);
	    cwd=xstrdup(RespQueue[RQ_head].path);
	 }
	 set_real_cwd(cwd);
	 break;
      }
      if(is5XX(act))
      {
	 SetError(NO_FILE,line);
	 break;
      }
      Disconnect();
      break;

   case CHECK_CWD_STALE:
      if(is2XX(act))
	 set_real_cwd(RespQueue[RQ_head].path);
      goto ignore;

   case CHECK_ABOR:
      AbortedClose();
      goto ignore;

   case CHECK_SIZE:
      CatchSIZE(act);
      break;
   case CHECK_SIZE_OPT:
      CatchSIZE_opt(act);
      break;
   case CHECK_MDTM:
      CatchDATE(act);
      break;
   case CHECK_MDTM_OPT:
      CatchDATE_opt(act);
      break;

   case CHECK_FILE_ACCESS:
   file_access:
      if(mode==CHANGE_MODE && (act==500 || act==501 || act==502))
      {
	 site_chmod_supported=false;
	 SetError(NO_FILE,line);
	 break;
      }
      NoFileCheck(act);
      break;

   case CHECK_PASV:
   case CHECK_EPSV:
      if(is2XX(act))
      {
	 if(strlen(line)<=4)
	    goto passive_off;

	 memset(&data_sa,0,sizeof(data_sa));

	 if(cc==CHECK_PASV)
	    addr_received=Handle_PASV();
	 else // cc==CHECK_EPSV
	    addr_received=Handle_EPSV();

	 if(!addr_received)
	    goto passive_off;

	 if(aborted_data_sock!=-1)
	    SocketConnect(aborted_data_sock,&data_sa);

      	 break;
      }
      if(copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 Disconnect();
	 break;
      }
      if(is5XX(act))
      {
      passive_off:
	 DebugPrint("---- ",_("Switching passive mode off"),2);
	 SetFlag(PASSIVE_MODE,0);
      }
      Disconnect();
      break;

   case CHECK_PORT:
      if(is2XX(act))
	 break;
      if(copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 Disconnect();
	 break;
      }
      if(is5XX(act))
      {
	 DebugPrint("---- ",_("Switching passive mode on"),2);
	 SetFlag(PASSIVE_MODE,1);
      }
      Disconnect();
      break;

   case CHECK_PWD:
      if(is2XX(act))
      {
	 if(!home_auto)
	 {
	    home_auto=ExtractPWD();   // it allocates space.
	    PropagateHomeAuto();
	 }
	 if(!home)
	    set_home(home_auto);
	 break;
      }
      break;

   case CHECK_RNFR:
      if(is3XX(act))
      {
	 SendCmd2("RNTO",file1);
	 AddResp(250,CHECK_FILE_ACCESS);
      	 break;
      }
      goto file_access;

   case CHECK_USER_PROXY:
      proxy_NoPassReqCheck(act);
      break;
   case CHECK_USER:
      NoPassReqCheck(act);
      break;
   case CHECK_PASS_PROXY:
      proxy_LoginCheck(act);
      break;
   case CHECK_PASS:
      LoginCheck(act);
      break;

   case CHECK_TRANSFER:
      TransferCheck(act);
      break;

   case CHECK_TRANSFER_CLOSED:
      if(strstr(line,"ABOR")
      && RespQueueSize()>=2 && RespQueue[RQ_head+1].check_case==CHECK_ABOR)
      {
	 DebugPrint("**** ","server bug: 426 reply missed",1);
	 PopResp();
	 AbortedClose();
      }
      break;

#ifdef USE_SSL
   case CHECK_AUTH_TLS:
      if(is2XX(act))
      {
	 control_ssl=lftp_ssl_new(control_sock);
	 control_ssl_connected=false;
      }
      else
      {
	 if((bool)Query("ssl-force"))
	    SetError(LOGIN_FAILED,_("ftp:ssl-force is set and server does not support or allow SSL"));
      }
      break;
   case CHECK_PROT:
      if(is2XX(act))
	 prot^='C'^'P';
      break;
#endif // USE_SSL

   } /* end switch */

   PopResp();
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
      return(_("Not connected"));
   case(CONNECTING_STATE):
      return(_("Connecting..."));
   case(CONNECTED_STATE):
#ifdef USE_SSL
      if(auth_tls_sent)
	 return _("TLS negotiation...");
#endif
      return _("Connected");
   case(USER_RESP_WAITING_STATE):
      return(_("Logging in..."));
   case(DATASOCKET_CONNECTING_STATE):
      if(addr_received==0)
	 return(_("Waiting for response..."));
      return(_("Making data connection..."));
   case(CWD_CWD_WAITING_STATE):
      return(_("Changing remote directory..."));
   case(WAITING_STATE):
      if(copy_mode!=COPY_NONE)
      {
	 if(RespQueueIsEmpty())
	    return _("Waiting for other copy peer...");
      }
      if(mode==STORE && copy_mode==COPY_NONE)
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
   }
   abort();
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
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && ftps==o->ftps);
}

bool  Ftp::SameConnection(const Ftp *o)
{
   if(!strcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && (user || !xstrcmp(anon_user,o->anon_user))
   && (pass || !xstrcmp(anon_pass,o->anon_pass))
   && ftps==o->ftps)
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
      if(home && o->home && strcmp(home,o->home))
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

   if(Error())
      return(error_code);

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

void Ftp::ResetLocationData()
{
   super::ResetLocationData();
   flags=0;
   dos_path=false;
   vms_path=false;
   mdtm_supported=true;
   size_supported=true;
   site_chmod_supported=true;
   xfree(home_auto);
   home_auto=xstrdup(FindHomeAuto());
   Reconfig(0);
   state=INITIAL_STATE;
}

void Ftp::Reconfig(const char *name)
{
   xfree(closure);
   closure=xstrdup(hostname);

   super::Reconfig(name);

   if(!xstrcmp(name,"net:idle") || !xstrcmp(name,"ftp:use-site-idle"))
   {
      if(data_sock==-1 && control_sock!=-1 && state==EOF_STATE && !quit_sent)
	 SendSiteIdle();
      return;
   }

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

   const char *h=QueryStringWithUserAtHost("home");
   if(h && h[0] && AbsolutePath(h))
      set_home(h);
   else
      set_home(home_auto);

   if(NoProxy(hostname))
      SetProxy(0);
   else
      SetProxy(Query("proxy",c));

   if(proxy && proxy_port==0)
      proxy_port=xstrdup(FTP_DEFAULT_PORT);

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

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}
void Ftp::CleanupThis()
{
   if(control_sock==-1 || mode!=CLOSED)
      return;
   Disconnect();
}

void Ftp::SetError(int ec,const char *e)
{
   switch((status)ec)
   {
   case(SEE_ERRNO):
   case(LOOKUP_ERROR):
   case(NO_HOST):
   case(FATAL):
   case(LOGIN_FAILED):
      Disconnect();
      break;
   case(IN_PROGRESS):
   case(OK):
   case(NOT_OPEN):
   case(NO_FILE):
   case(FILE_MOVED):
   case(STORE_FAILED):
   case(DO_AGAIN):
   case(NOT_SUPP):
      break;
   }
   super::SetError(ec,e);
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

const char *Ftp::ProtocolSubstitution(const char *host)
{
   if(NoProxy(host))
      return 0;
   const char *proxy=ResMgr::Query("ftp:proxy",host);
   if(proxy && !strncmp(proxy,"http://",7))
      return "hftp";
   return 0;
}


#ifdef USE_SSL
#undef super
#define super Ftp
FtpS::FtpS()
{
   ftps=true;
   res_prefix="ftp";
}
FtpS::~FtpS()
{
}
FtpS::FtpS(const FtpS *o) : super(o)
{
   ftps=true;
   res_prefix="ftp";
   Reconfig(0);
}
FileAccess *FtpS::New(){ return new FtpS();}
#endif

#include "modconfig.h"
#ifdef MODULE_PROTO_FTP
void module_init()
{
   Ftp::ClassInit();
}
#endif
