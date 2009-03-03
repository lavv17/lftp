/*
 * lftp and utils
 *
 * Copyright (c) 1996-2008 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <ctype.h>

#include "ftpclass.h"
#include "xstring.h"
#include "url.h"
#include "FtpListInfo.h"
#include "FileGlob.h"
#include "FtpDirList.h"
#include "log.h"
#include "FileCopyFtp.h"
#include "LsCache.h"
#include "buffer_ssl.h"

#include "ascii_ctype.h"
#include "misc.h"
#include "strftime.h"

#define TELNET_IAC	255		/* interpret as command: */
#define	TELNET_IP	244		/* interrupt process--permanently */
#define	TELNET_DM	242		/* for telfunc calls */
#define	TELNET_WILL	251
#define	TELNET_WONT	252
#define	TELNET_DO	253
#define	TELNET_DONT	254

#include <errno.h>
#include <time.h>

#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

CDECL_BEGIN
#include "regex.h"
CDECL_END

#if USE_SSL
# include "lftp_ssl.h"
#else
# define control_ssl 0
const bool Ftp::ftps=false;
#endif

#define max_buf 0x10000

#define FTP_DEFAULT_PORT "21"
#define FTPS_DEFAULT_PORT "990"
#define FTP_DATA_PORT 20
#define FTPS_DATA_PORT 989
#define HTTP_DEFAULT_PROXY_PORT "3128"

#define super NetAccess

#define is5XX(code) ((code)>=500 && (code)<600)
#define is4XX(code) ((code)>=400 && (code)<500)
#define is3XX(code) ((code)>=300 && (code)<400)
#define is2XX(code) ((code)>=200 && (code)<300)
#define is1XX(code) ((code)>=100 && (code)<200)
#define cmd_unsupported(code) ((code)==500 || (code)==502)
#define site_cmd_unsupported(code) (cmd_unsupported((code)) || (code)==501)

#define debug(a) Log::global->Format a

#ifndef EINPROGRESS
#define EINPROGRESS -1
#endif

FileAccess *Ftp::New() { return new Ftp(); }

void  Ftp::ClassInit()
{
   // register the class
   Register("ftp",Ftp::New);
   FileCopy::fxp_create=FileCopyFtp::New;

#if USE_SSL
   Register("ftps",FtpS::New);
#endif
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

const char *Ftp::encode_eprt(const sockaddr_u *a)
{
   int proto;
   if(a->sa.sa_family==AF_INET)
      proto=1;
   else if(a->sa.sa_family==AF_INET6)
      proto=2;
   else
      return 0;
   return xstring::format("|%d|%s|%d|",proto,a->address(),a->port());
}
#endif

bool Ftp::Connection::data_address_ok(const sockaddr_u *dp,bool verify_address,bool verify_port)
{
   sockaddr_u d;
   sockaddr_u c;
   socklen_t len;
   len=sizeof(d);
   if(dp)
      d=*dp;
   else if(getpeername(data_sock,&d.sa,&len)==-1)
   {
      LogError(0,"getpeername(data_sock): %s\n",strerror(errno));
      return !verify_address && !verify_port;
   }
   len=sizeof(c);
   if(getpeername(control_sock,&c.sa,&len)==-1)
   {
      LogError(0,"getpeername(control_sock): %s\n",strerror(errno));
      return !verify_address;
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
   if(!verify_port)
      return true;
   LogError(0,_("Data connection peer has wrong port number"));
   return false;

address_mismatch:
   if(!verify_address)
      return true;
   LogError(0,_("Data connection peer has mismatching address"));
   return false;
}

/* Procedures for checking for a special answers */
/* General rule: check for valid replies first, errors second. */

void Ftp::RestCheck(int act)
{
   if(is2XX(act) || is3XX(act))
   {
      real_pos=conn->rest_pos;  // REST successful
      conn->last_rest=conn->rest_pos;
      return;
   }
   real_pos=0;
   if(pos==0)
      return;
   if(is5XX(act))
   {
      if(cmd_unsupported(act))
	 conn->rest_supported=false;
      LogNote(2,_("Switching to NOREST mode"));
      flags|=NOREST_MODE;
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
   if(cmd_unsupported(act))
   {
      SetError(FATAL,all_lines);
      return;
   }
   if(is5XX(act) && !Transient5XX(act))
   {
      if(real_pos>0 && !GetFlag(IO_FLAG) && copy_mode==COPY_NONE)
      {
	 DataClose();
	 LogNote(2,_("Switching to NOREST mode"));
	 flags|=NOREST_MODE;
	 real_pos=0;
	 if(mode==STORE)
	    pos=0;
	 state=EOF_STATE; // retry
	 return;
      }
      SetError(NO_FILE,all_lines);
      return;
   }
   if(copy_mode!=COPY_NONE)
   {
      copy_failed=true;
      return;
   }
   DataClose();
   state=EOF_STATE;
   eof=false;
   if(mode==STORE && GetFlag(IO_FLAG))
      SetError(STORE_FAILED,0);
   else if(NextTry())
      retry_timer.Set(2); // retry after 2 seconds
}

/* 5xx that aren't errors at all */
bool Ftp::NonError5XX(int act)
{
   return (mode==LIST && act==550 && (!file || !file[0]))
       // ...and proftpd workaround.
       || (mode==LIST && act==450 && strstr(line,"No files found"));
}

/* 5xx that are really transient like 4xx */
bool Ftp::Transient5XX(int act)
{
   if(!is5XX(act))
      return false;

   // retry on these errors (ftp server ought to send 4xx code instead)
   if((strstr(line,"Broken pipe") && (!file || !strstr(file,"Broken pipe")))
   || (strstr(line,"Too many")    && (!file || !strstr(file,"Too many")))
   || (strstr(line,"timed out")   && (!file || !strstr(file,"timed out")))
   || (strstr(line,"closed by the remote host"))
   // if there were some data received, assume it is temporary error.
   || (mode!=STORE && GetFlag(IO_FLAG)))
      return true;

   return false;
}

#if USE_SSL
const char *Ftp::get_protect_res()
{
   if(mode==LIST || mode==MP_LIST || (mode==LONG_LIST && !use_stat_for_list))
      return "ftp:ssl-protect-list";
   else if(mode==RETRIEVE || mode==STORE)
      return "ftp:ssl-protect-data";
   return 0;
}
#endif

// 226 Transfer complete.
void Ftp::TransferCheck(int act)
{
   if(act==225 || act==226) // data connection is still open or ABOR worked.
   {
      copy_done=true;
      conn->CloseAbortedDataConnection();

      if(!conn->received_150 && state!=DATA_OPEN_STATE)
	 goto simulate_eof;
   }
   if(act==211)
   {
      // permature STAT?
      conn->stat_timer.ResetDelayed(3);
      return;
   }
   if(act==213)	  // this must be a STAT reply.
   {
      conn->stat_timer.Reset();

      long long p;
      // first try Serv-U format:
      //    Status for user UUU from X.X.X.X
      //    Stored 1 files, 0 Kbytes
      //    Retrieved 0 files, 0 Kbytes
      //    Receiving file XXX (YYY bytes)
      const char *r=strstr(all_lines,"Receiving file");
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
      for(const char *b=line+4; ; b++)
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
   if(copy_mode!=COPY_NONE && is4XX(act))
   {
      copy_passive=!copy_passive;
      copy_failed=true;
      return;
   }
   if(NonError5XX(act))
      goto simulate_eof;
   if(act==426 && copy_mode==COPY_NONE)
   {
      if(conn->data_sock==-1 && strstr(line,"Broken pipe"))
   	 return;
   }
   if(is2XX(act) && conn->data_sock==-1)
      eof=true;
#if USE_SSL
   if(conn->auth_supported && act==522 && conn->prot=='C') {
      const char *res=get_protect_res();
      if(res) {
	 // try again with PROT P
	 DataClose();
	 ResMgr::Set(res,hostname,"yes");
	 state=EOF_STATE;
	 return;
      }
   }
#endif
   NoFileCheck(act);
   return;

simulate_eof:
   DataClose();
   state=EOF_STATE;
   eof=true;
   return;
}

void Ftp::LoginCheck(int act)
{
   if(conn->ignore_pass)
      return;
   if(act==530) // login incorrect or overloaded server
   {
      const char *rexp=Query("retry-530",hostname);
      if(re_match(all_lines,rexp,REG_ICASE))
      {
	 LogNote(9,_("Server reply matched ftp:retry-530, retrying"));
	 goto retry;
      }
      if(!user)
      {
	 rexp=Query("retry-530-anonymous",hostname);
	 if(re_match(all_lines,rexp,REG_ICASE))
	 {
	    LogNote(9,_("Server reply matched ftp:retry-530-anonymous, retrying"));
	    goto retry;
	 }
      }
   }
   if(is5XX(act))
   {
      SetError(LOGIN_FAILED,all_lines);
      return;
   }

   if(!is2XX(act) && !is3XX(act))
   {
   retry:
      Disconnect();
      NextPeer();
      if(peer_curr==0)
	 try_time=now;	// count the reconnect-interval from this moment
      last_connection_failed=true;
   }
   if(is3XX(act) && !expect->Has(Expect::ACCT_PROXY))
   {
      if(!QueryStringWithUserAtHost("acct"))
      {
	 Disconnect();
	 SetError(LOGIN_FAILED,_("Account is required, set ftp:acct variable"));
      }
   }
}

void Ftp::NoPassReqCheck(int act) // for USER command
{
   if(is2XX(act)) // in some cases, ftpd does not ask for pass.
   {
      conn->ignore_pass=true;
      return;
   }
   if(act==331 && allow_skey && user && pass)
   {
      skey_pass.set(make_skey_reply());
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
	 LogNote(9,_("Saw `unknown', assume failed login"));
	 SetError(LOGIN_FAILED,all_lines);
	 return;
      }
      goto def_ret;
   }
   if(is5XX(act))
   {
      // proxy interprets USER as user@host, so we check for host name validity
      if(proxy && (strstr(line,"host") || strstr(line,"resolve")))
      {
	 LogNote(9,_("assuming failed host name lookup"));
	 SetError(LOOKUP_ERROR,all_lines);
	 return;
      }
      SetError(LOGIN_FAILED,all_lines);
      return;
   }
def_ret:
   Disconnect();
   try_time=now;	// count the reconnect-interval from this moment
   last_connection_failed=true;
}

// login to proxy.
void Ftp::proxy_LoginCheck(int act)
{
   if(is2XX(act))
      return;
   if(is5XX(act))
   {
      SetError(LOGIN_FAILED,all_lines);
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
      SetError(LOGIN_FAILED,all_lines);
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
   char *pwd=string_alloca(line.length()+1);

   const char *scan=strchr(line,'"');
   if(scan==0)
      return 0;
   scan++;
   const char *right_quote=strrchr(scan,'"');
   if(!right_quote)
      return 0;

   char *store=pwd;
   while(scan<right_quote)
   {
      // This is the method of quote encoding.
      if(*scan=='"' && scan[1]=='"')
	 scan++;
      *store++=*scan++;
   }

   if(store==pwd)
      return 0;	  // empty home not allowed.
   *store=0;

   int dev_len=device_prefix_len(pwd);
   if(pwd[dev_len]=='[')
   {
      conn->vms_path=true;
      normalize_path_vms(pwd);
   }
   else if(dev_len==2 || dev_len==3)
   {
      conn->dos_path=true;
   }

   if(!strchr(pwd,'/') || conn->dos_path)
   {
      // for safety -- against dosish ftpd
      for(char *s=pwd; *s; s++)
	 if(*s=='\\')
	    *s='/';
   }
   return xstrdup(pwd);
}

void Ftp::SendCWD(const char *path,Expect::expect_t c,const char *arg)
{
   conn->SendCmd2("CWD",path);
   expect->Push(new Expect(c,arg?arg:path));
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

Ftp::pasv_state_t Ftp::Handle_PASV()
{
   unsigned a0,a1,a2,a3,p0,p1;
   /*
    * Extract address. RFC1123 says:
    * "...must scan the reply for the first digit..."
    */
   for(const char *b=line+4; ; b++)
   {
      if(*b==0)
      {
	 Disconnect();
	 return PASV_NO_ADDRESS_YET;
      }
      if(!is_ascii_digit(*b))
	 continue;
      if(sscanf(b,"%u,%u,%u,%u,%u,%u",&a0,&a1,&a2,&a3,&p0,&p1)==6)
         break;
   }
   unsigned char *a,*p;
   conn->data_sa.sa.sa_family=conn->peer_sa.sa.sa_family;
   if(conn->data_sa.sa.sa_family==AF_INET)
   {
      a=(unsigned char*)&conn->data_sa.in.sin_addr;
      p=(unsigned char*)&conn->data_sa.in.sin_port;
   }
#if INET6
   else if(conn->data_sa.sa.sa_family==AF_INET6)
   {
      a=((unsigned char*)&conn->data_sa.in6.sin6_addr)+12;
      a[-1]=a[-2]=0xff; // V4MAPPED
      p=(unsigned char*)&conn->data_sa.in6.sin6_port;
   }
#endif
   else
   {
      Disconnect();
      return PASV_NO_ADDRESS_YET;
   }

   a[0]=a0; a[1]=a1; a[2]=a2; a[3]=a3;
   p[0]=p0; p[1]=p1;

   bool ignore_pasv_address = QueryBool("ignore-pasv-address",hostname);
   if(ignore_pasv_address)
      LogNote(2,"Address returned by PASV is ignored according to ftp:ignore-pasv-address setting");
   else if((a0==0 && a1==0 && a2==0 && a3==0)
	   || (QueryBool("fix-pasv-address",hostname) && !conn->proxy_is_http
	       && (InPrivateNetwork(&conn->data_sa) != InPrivateNetwork(&conn->peer_sa)
		   || IsLoopback(&conn->data_sa) != IsLoopback(&conn->peer_sa))))
   {
      // broken server, try to fix up
      ignore_pasv_address=true;
      conn->fixed_pasv=true;
      LogNote(2,"Address returned by PASV seemed to be incorrect and has been fixed");
   }

   if(ignore_pasv_address)
   {
      if(conn->data_sa.sa.sa_family==AF_INET)
	 memcpy(a,&conn->peer_sa.in.sin_addr,sizeof(conn->peer_sa.in.sin_addr));
#if INET6
      else if(conn->data_sa.in.sin_family==AF_INET6) // peer_sa should be V4MAPPED
	 memcpy(a,&conn->peer_sa.in6.sin6_addr.s6_addr[12],4);
#endif
   }

   return PASV_HAVE_ADDRESS;
}

Ftp::pasv_state_t Ftp::Handle_EPSV()
{
   char delim;
   char *format=alloca_strdup("|||%u|");
   unsigned port;

   const char *c=strchr(line,'(');
   c=c?c+1:line+4;
   delim=*c;

   for(char *p=format; *p; p++)
      if(*p=='|')
	 *p=delim;

   if(sscanf(c,format,&port)!=1)
   {
      LogError(0,_("cannot parse EPSV response"));
      Disconnect();
      return PASV_NO_ADDRESS_YET;
   }

   socklen_t len=sizeof(conn->data_sa);
   getpeername(conn->control_sock,&conn->data_sa.sa,&len);
   if(conn->data_sa.sa.sa_family==AF_INET)
      conn->data_sa.in.sin_port=htons(port);
#if INET6
   else if(conn->data_sa.sa.sa_family==AF_INET6)
      conn->data_sa.in6.sin6_port=htons(port);
#endif
   else
   {
      Disconnect();
      return PASV_NO_ADDRESS_YET;
   }
   return PASV_HAVE_ADDRESS;
}

void Ftp::CatchDATE(int act)
{
   if(!array_for_info)
      return;

   if(is2XX(act))
   {
      if(line.length()>4 && is_ascii_digit(line[4]))
	 array_for_info[array_ptr].time=ConvertFtpDate(line+4);
      else
	 array_for_info[array_ptr].time=NO_DATE;
   }
   else	if(is5XX(act))
   {
      if(cmd_unsupported(act))
	 conn->mdtm_supported=false;
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

   TrySuccess();
}
void Ftp::CatchDATE_opt(int act)
{
   if(!opt_date)
      return;

   if(is2XX(act) && line.length()>4 && is_ascii_digit(line[4]))
   {
      *opt_date=ConvertFtpDate(line+4);
      opt_date=0;
   }
   else
   {
      if(cmd_unsupported(act))
	 conn->mdtm_supported=false;
      *opt_date=NO_DATE;
   }
}

void Ftp::CatchSIZE(int act)
{
   if(!array_for_info)
      return;

   long long size=NO_SIZE;

   if(is2XX(act))
   {
      if(line.length()>4) {
	 if(sscanf(line+4,"%lld",&size)!=1)
	    size=NO_SIZE;
      }
   }
   else	if(is5XX(act))
   {
      if(cmd_unsupported(act))
	 conn->size_supported=false;
   }
   else
   {
      Disconnect();
      return;
   }

   if(size<1)
      size=NO_SIZE;

   array_for_info[array_ptr].size=size;

   array_for_info[array_ptr].get_size=false;
   if(!array_for_info[array_ptr].get_time)
      array_ptr++;

   TrySuccess();
}
void Ftp::CatchSIZE_opt(int act)
{
   long long size=NO_SIZE;

   if(is2XX(act))
   {
      if(line.length()>4) {
	 if(sscanf(line+4,"%lld",&size)!=1)
	    size=NO_SIZE;
      }
   }
   else
   {
      if(cmd_unsupported(act))
	 conn->size_supported=false;
   }

   // SIZE 0 is ignored (for some buggy servers).
   if(size<1)
      return;

   if(mode==RETRIEVE)
      entity_size=size;

   if(opt_size)
   {
      *opt_size=size;
      opt_size=0;
   }
}

Ftp::Connection::Connection(const char *c)
   : closure(c), send_cmd_buffer(DirectedBuffer::PUT)
{
   control_sock=-1;
   data_sock=-1;
   aborted_data_sock=-1;
#if USE_SSL
   prot='C';  // current protection scheme 'C'lear or 'P'rivate
   auth_sent=false;
   auth_supported=true;
   cpsv_supported=false;
   sscn_supported=true;
   sscn_on=false;
#endif
   type='A';
   last_rest=0;
   rest_pos=0;

   quit_sent=false;
   fixed_pasv=false;
   translation_activated=false;
   sync_wait=1;	// expect server greetings
   multiline_code=0;
   ignore_pass=false;
   try_feat_after_login=false;
   tune_after_login=false;
   utf8_activated=false;

   dos_path=false;
   vms_path=false;
   have_feat_info=false;
   mdtm_supported=true;
   size_supported=true;
   rest_supported=true;
   site_chmod_supported=true;
   site_utime_supported=true;
   site_utime2_supported=true;
   pret_supported=false;
   utf8_supported=false;
   lang_supported=false;
   mlst_supported=false;
   clnt_supported=false;
   host_supported=false;
   mfmt_supported=false;
   epsv_supported=false;

   proxy_is_http=false;
   may_show_password=false;

   nop_time=0;
   nop_count=0;
   nop_offset=0;

   abor_close_timer.SetResource("ftp:abor-max-wait",closure);
   stat_timer.SetResource("ftp:stat-interval",closure);
   waiting_150_timer.SetResource("ftp:waiting-150-timeout",closure);
#if USE_SSL
   waiting_ssl_shutdown.SetResource("ftp:ssl-shutdown-timeout",closure);
#endif
}

void Ftp::InitFtp()
{
#if USE_SSL
   ftps=false;	  // ssl and prot='P' by default (port 990)
#endif

   eof=false;
   state=INITIAL_STATE;
   flags=SYNC_MODE;
   allow_skey=true;
   force_skey=false;
   verify_data_address=true;
   use_stat=true;
   use_stat_for_list=true;
   use_mdtm=true;
   use_size=true;
   use_telnet_iac=true;
   use_pret=true;
   use_mlsd=false;

   copy_mode=COPY_NONE;
   copy_addr_valid=false;
   copy_passive=false;
   copy_protect=false;
   copy_ssl_connect=false;
   copy_done=false;
   copy_connection_open=false;
   copy_allow_store=false;
   copy_failed=false;

   disconnect_on_close=false;
   last_connection_failed=false;

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

   Reconfig();
}

Ftp::Connection::~Connection()
{
   CloseAbortedDataConnection();
   CloseDataConnection();
   if(control_sock!=-1)
   {
      LogNote(7,_("Closing control socket"));
      close(control_sock);
   }
}

Ftp::~Ftp()
{
   Enter();
   Disconnect();
   if(conn)
   {
      FlushSendQueue();
      ReceiveResp();
   }
   Disconnect();
   Leave();
}

bool Ftp::AbsolutePath(const char *s)
{
   if(!s)
      return false;
   int dev_len=device_prefix_len(s);
   return(s[0]=='/'
      || (((conn->dos_path && dev_len==3) || (conn->vms_path && dev_len>2))
	  && s[dev_len-1]=='/'));
}

// returns true if we need to sleep instead of moving to higher level.
bool Ftp::GetBetterConnection(int level,bool limit_reached)
{
   bool need_sleep=false;

//    if(level==0 && cwd==0)
//       return need_sleep;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Ftp *o=(Ftp*)fo; // we are sure it is Ftp.

      if(o->GetConnectLevel()!=CL_LOGGED_IN)
	 continue;
      if(!SameConnection(o))
	 continue;

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      if(o->conn->data_sock!=-1 || o->state!=EOF_STATE || o->mode!=CLOSED)
      {
	 /* session is in use; last resort is to takeover an active connection */
	 if(level<2)
	    continue;
	 /* only take over lower priority or suspended jobs */
	 if(!connection_takeover || (o->priority>=priority && !o->IsSuspended()))
	    continue;
	 if(o->conn->data_sock!=-1 && o->expect->Count()<=1)
	 {
	    /* don't take over active connections if they won't be able to resume */
	    if((o->flags&NOREST_MODE) && o->real_pos>0x1000)
	       continue;
	    if(o->QueryBool("web-mode",o->hostname))
	       continue;
	    o->DataAbort();
	    o->DataClose();
	    if(!o->conn)
	       return need_sleep; // oops...
	 }
	 else
	 {
	    if(!o->expect->IsEmpty() || o->disconnect_on_close)
	       continue;
	 }
      }
      else
      {
	 if(limit_reached)
	 {
	    /* wait until job is diff seconds idle before taking it over */
	    int diff=o->last_priority-priority;
	    if(diff>0)
	    {
	       /* number of seconds the task has been idle */
	       if(o->idle_timer.TimePassed()<diff)
	       {
		  TimeoutS(1);
		  need_sleep=true;
		  continue;
	       }
	    }
	 }
      }

      // so borrow the connection
      MoveConnectionHere(o);
      return false;
   }
   return need_sleep;
}

void  Ftp::HandleTimeout()
{
   if(conn)
      conn->quit_sent=true;
   super::HandleTimeout();
}

// Create buffers after control socket had been connected.
void Ftp::Connection::MakeBuffers()
{
#if USE_SSL
   control_ssl=0;
#endif
   control_send=new IOBufferFDStream(
      new FDStream(control_sock,"control-socket"),IOBuffer::PUT);
   control_recv=new IOBufferFDStream(
      new FDStream(control_sock,"control-socket"),IOBuffer::GET);
}
void Ftp::Connection::InitTelnetLayer()
{
   if(telnet_layer_send)
      return;
   control_send=telnet_layer_send=new IOBufferTelnet(control_send.borrow());
   control_recv=new IOBufferTelnet(control_recv.borrow());
}

bool Ftp::ProxyIsHttp()
{
   if(!proxy_proto)
      return false;
   return !strcmp(proxy_proto,"http")
       || !strcmp(proxy_proto,"https");
}

int   Ftp::Do()
{
   const char *command=0;
   bool	 append_file=false;
   int	 res;
   socklen_t addr_len;
   const unsigned char *a;
   const unsigned char *p;
   automate_state oldstate;
   int	 m=STALL;

   // check if idle time exceeded
   if(mode==CLOSED && conn && idle_timer.Stopped())
   {
      LogNote(1,_("Closing idle connection"));
      Disconnect();
      if(conn)
	 idle_timer.Reset();
      return m;
   }

   if(conn && conn->quit_sent)
   {
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(expect && expect->IsEmpty())
      {
	 Disconnect();
	 return MOVED;
      }
      goto usual_return;
   }

   /* Some servers cannot detect ABOR, help them by reading remaining data
      and closing data connection in few seconds */
   if(conn && conn->aborted_data_sock!=-1)
   {
      char discard[0x2000];
      int res=read(conn->aborted_data_sock,discard,sizeof(discard));
      if(res==0 || conn->abor_close_timer.Stopped())
	 conn->CloseAbortedDataConnection();
      else
	 Block(conn->aborted_data_sock,POLLIN);
   }

   if(Error() || eof || mode==CLOSED)
   {
      // inactive behavior
      if(conn)
      {
	 m|=FlushSendQueue();
	 m|=ReceiveResp();
      }
      if(eof || mode==CLOSED)
	 goto notimeout_return;
      goto usual_return;
   }

   if(!hostname)
      return m;

   if(mode==CHANGE_MODE && !QueryBool("use-site-chmod"))
   {
      SetError(NOT_SUPP,_("SITE CHMOD is disabled by ftp:use-site-chmod"));
      return MOVED;
   }
   if(mode==MP_LIST && !use_mlsd)
   {
      SetError(NOT_SUPP,_("MLSD is disabled by ftp:use-mlsd"));
      return MOVED;
   }

   switch(state)
   {
   case(INITIAL_STATE):
   {
      // walk through ftp classes and try to find identical idle ftp session
      // first try "easy" cases of session take-over.
      for(int i=0; i<3; i++)
      {
	 bool limit_reached=last_connection_failed
	       || (connection_limit>0 && connection_limit<=CountConnections());
	 if(i>=2 && !limit_reached)
	    break;
	 bool need_sleep=GetBetterConnection(i,limit_reached);
	 if(state!=INITIAL_STATE)
	    return MOVED;
	 if(need_sleep)
	    return m;
      }

      if(!resolver && mode!=CONNECT_VERIFY && !ReconnectAllowed())
	 return m;

      if(ftps)
	 m|=Resolve(FTPS_DEFAULT_PORT,"ftps","tcp");
      else
	 m|=Resolve(FTP_DEFAULT_PORT,"ftp","tcp");
      if(!peer)
	 return m;

      if(mode==CONNECT_VERIFY)
	 return m;

      if(!ReconnectAllowed())
	 return m;

      if(!NextTry())
	 return MOVED;

      last_connection_failed=false;
      assert(!conn);
      assert(!expect);
      conn=new Connection(hostname);
      expect=new ExpectQueue();

      conn->proxy_is_http=ProxyIsHttp();
      if(conn->proxy_is_http)
	 SetFlag(PASSIVE_MODE,1);

      conn->peer_sa=peer[peer_curr];
      conn->control_sock=SocketCreateTCP(conn->peer_sa.sa.sa_family);
      if(conn->control_sock==-1)
      {
	 conn=0;
	 expect=0;
	 if(peer_curr+1<peer.count())
	 {
	    try_time=0;
	    peer_curr++;
	    retries--;
	    return MOVED;
	 }
	 int saved_errno=errno;
	 LogError(9,"socket: %s",strerror(saved_errno));
	 if(NonFatalError(saved_errno))
	    return m;
	 xstring& str=xstring::format(_("cannot create socket of address family %d"),
		     conn->peer_sa.sa.sa_family);
	 errno=saved_errno;
	 SetError(SEE_ERRNO,str);
	 return MOVED;
      }
      KeepAlive(conn->control_sock);
      MinimizeLatency(conn->control_sock);
      SetSocketBuffer(conn->control_sock);
      SetSocketMaxseg(conn->control_sock);
      NonBlock(conn->control_sock);
      CloseOnExec(conn->control_sock);

      SayConnectingTo();

      res=SocketConnect(conn->control_sock,&conn->peer_sa);
      state=CONNECTING_STATE;
      if(res==-1 && errno!=EINPROGRESS)
      {
	 int e=errno;
	 LogError(0,"connect(control_sock): %s",strerror(e));
	 if(NotSerious(e))
	 {
	    Disconnect();
	    return MOVED;
	 }
	 goto system_error;
      }
      m=MOVED;
      timeout_timer.Reset();
   }
   /* fallthrough */
   case(CONNECTING_STATE):
      assert(conn && conn->control_sock!=-1);
      res=Poll(conn->control_sock,POLLOUT);
      if(res==-1)
      {
	 Disconnect();
	 return MOVED;
      }
      if(!(res&POLLOUT))
	 goto usual_return;

#if USE_SSL
      if(proxy && (!xstrcmp(proxy_proto,"ftps")
	        || !xstrcmp(proxy_proto,"https")))
      {
	 conn->MakeSSLBuffers(hostname);
      }
      else // note the following block
#endif
      {
	 conn->MakeBuffers();
      }

      if(!proxy || !conn->proxy_is_http)
	 goto pre_CONNECTED_STATE;

      state=HTTP_PROXY_CONNECTED;
      m=MOVED;
      HttpProxySendConnect();
   /* fallthrough */
   case HTTP_PROXY_CONNECTED:
      if(!HttpProxyReplyCheck(conn->control_recv))
	 goto usual_return;

   pre_CONNECTED_STATE:
#if USE_SSL
      if(ftps && (!proxy || conn->proxy_is_http))
      {
	 conn->MakeSSLBuffers(hostname);
	 const char *initial_prot=ResMgr::Query("ftps:initial-prot",hostname);
	 conn->prot=initial_prot[0];
      }
#endif
      if(use_telnet_iac)
	 conn->InitTelnetLayer();

      state=CONNECTED_STATE;
      m=MOVED;
      expect->Push(Expect::READY);

      if(use_feat)
      {
	 if(!proxy || conn->proxy_is_http)
	 {
	    conn->SendCmd("FEAT");
	    expect->Push(Expect::FEAT);
	 }
	 else
	 {
	    conn->try_feat_after_login=true;
	 }
      }
   /* fallthrough */
   case CONNECTED_STATE:
   {
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=CONNECTED_STATE || Error())
	 return MOVED;

      if(expect->Has(Expect::FEAT))
	 goto usual_return;

#if USE_SSL
      if(QueryBool((user && pass)?"ssl-allow":"ssl-allow-anonymous",hostname)
      && !ftps && (!proxy || conn->proxy_is_http))
	 SendAuth(Query("ssl-auth",hostname));
      if(state!=CONNECTED_STATE)
	 return MOVED;

      if(conn->auth_sent && !expect->IsEmpty())
	 goto usual_return;
#endif
      // Do connection tuning after AUTH TLS.
      if(conn->have_feat_info)
	 TuneConnectionAfterFEAT();

      const char *user_to_use=(user?user.get():anon_user.get());
      const char *proxy_auth_type=Query("proxy-auth-type",proxy);
      if(proxy && !conn->proxy_is_http)
      {
	 if(!strcmp(proxy_auth_type,"joined") && proxy_user && proxy_pass)
	 {
	    user_to_use=xstring::cat(user_to_use,"@",proxy_user.get(),"@",
		  hostname.get(),portname?":":NULL,portname.get(),NULL);
	 }
	 else if(!strcmp(proxy_auth_type,"joined-acct") && proxy_user && proxy_pass)
	 {
	    user_to_use=xstring::cat(user_to_use,"@",hostname.get(),
		  portname?":":"",portname?portname.get():"",
		  " ",proxy_user.get(),NULL);
	    // proxy_pass is sent later with ACCT command
	 }
	 else if(!strcmp(proxy_auth_type,"proxy-user@host") && proxy_user && proxy_pass)
	 {
	    expect->Push(Expect::USER_PROXY);
	    conn->SendCmd2("USER",xstring::cat(proxy_user.get(),"@",hostname.get(),
		  portname?":":"",portname?portname.get():"",NULL));
	    expect->Push(Expect::PASS_PROXY);
	    conn->SendCmd2("PASS",proxy_pass);
	 }
	 else // no proxy auth, or type is `open' or `user'.
	 {
	    bool use_open=!strcmp(proxy_auth_type,"open");
	    if(!use_open)
	    {
	       user_to_use=xstring::cat(user_to_use,"@",hostname.get(),
		     portname?":":NULL,portname.get(),NULL);
	    }
	    if(proxy_user && proxy_pass)
	    {
	       expect->Push(Expect::USER_PROXY);
	       conn->SendCmd2("USER",proxy_user);
	       expect->Push(Expect::PASS_PROXY);
	       conn->SendCmd2("PASS",proxy_pass);
	    }
	    if(use_open)
	    {
	       expect->Push(Expect::OPEN_PROXY);
	       conn->SendCmd2("OPEN",xstring::cat(hostname.get(),
		     portname?":":NULL,portname.get(),NULL));
	    }
	 }
      }

      skey_pass.set(0);

      expect->Push(Expect::USER);
      conn->SendCmd2("USER",user_to_use);

      state=USER_RESP_WAITING_STATE;
      m=MOVED;
   }
   /* fallthrough */
   case(USER_RESP_WAITING_STATE):
   {
      if((GetFlag(SYNC_MODE) || (user && pass && allow_skey))
      && !expect->IsEmpty())
      {
	 m|=FlushSendQueue();
	 m|=ReceiveResp();
	 if(state!=USER_RESP_WAITING_STATE || Error())
	    return MOVED;
	 if(!expect->IsEmpty())
	    goto usual_return;
      }

      const char *proxy_auth_type=Query("proxy-auth-type",proxy);
      if(!conn->ignore_pass)
      {
	 conn->may_show_password = (skey_pass!=0) || (user==0) || pass_open;
	 const char *pass_to_use=(pass?pass:anon_pass);
	 if(allow_skey && skey_pass)
	    pass_to_use=skey_pass;
	 else if(proxy && !conn->proxy_is_http && proxy_user && proxy_pass
	 && !strcmp(proxy_auth_type,"joined"))
	    pass_to_use=xstring::cat(pass_to_use,"@",proxy_pass.get(),NULL);
	 expect->Push(Expect::PASS);
	 conn->SendCmd2("PASS",pass_to_use);

      }
      if(proxy && !conn->proxy_is_http && proxy_user && proxy_pass
      && !strcmp(proxy_auth_type,"joined-acct"))
      {
	 expect->Push(Expect::ACCT_PROXY);
	 conn->SendCmd2("ACCT",proxy_pass);
      }
      SendAcct();
      if(conn->try_feat_after_login)
      {
	 conn->SendCmd("FEAT");
	 expect->Push(Expect::FEAT);
      }
      else if(conn->tune_after_login)
	 TuneConnectionAfterFEAT();
      SendSiteGroup();
      SendSiteIdle();

      if(!home_auto)
      {
	 // if we don't yet know the home location, try to get it
	 conn->SendCmd("PWD");
	 expect->Push(Expect::PWD);
      }

#if USE_SSL
      if(conn->ssl_is_activated())
      {
	 conn->SendCmd("PBSZ 0");
	 expect->Push(Expect::IGNORE);
	 if(QueryBool("ssl-protect-data") && QueryBool("ssl-protect-list"))
	    SendPROT('P');
	 if(QueryBool("ssl-use-ccc"))
	 {
	    conn->SendCmd("CCC");
	    expect->Push(Expect::CCC);
	 }
      }
#endif // USE_SSL

      set_real_cwd(0);
   }
   /* fallthrough */
   pre_EOF_STATE:
      state=EOF_STATE;
      m=MOVED;
   case(EOF_STATE):
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=EOF_STATE || Error())
	 return MOVED;

      if(expect->Has(Expect::FEAT)
      || expect->Has(Expect::OPTS_UTF8)
      || expect->Has(Expect::LANG))
	 goto usual_return;

#if USE_SSL
      if(expect->Has(Expect::CCC))
	 goto usual_return;
#endif // USE_SSL

      if(!conn->utf8_activated && charset && *charset)
	 conn->SetControlConnectionTranslation(charset);

      if(mode==CONNECT_VERIFY)
	 goto notimeout_return;

      if(mode==CHANGE_MODE && !conn->site_chmod_supported)
      {
	 SetError(NOT_SUPP,_("SITE CHMOD is not supported by this site"));
	 return MOVED;
      }
      if(mode==MP_LIST && !conn->mlst_supported)
      {
	 SetError(NOT_SUPP,_("MLST and MLSD are not supported by this site"));
	 return MOVED;
      }

      if(home.path==0 && !expect->IsEmpty())
	 goto usual_return;

      if(!retry_timer.Stopped())
	 goto usual_return;

      if(real_cwd==0)
	 set_real_cwd(home_auto);

      ExpandTildeInCWD();

      if(!CheckRetries())
	 return MOVED;

      if(mode!=CHANGE_DIR)
      {
	 Expect *last_cwd=expect->FindLastCWD();
	 // Send CWD if we have a CWD in flight and it differs from wanted cwd
	 //          or we don't have a CWD and read_cwd differs from wanted cwd
	 if((!last_cwd && xstrcmp(cwd,real_cwd) && !(real_cwd==0 && !xstrcmp(cwd,"~")))
	    || (last_cwd && xstrcmp(last_cwd->arg,cwd)))
	 {
	    SendCWD(cwd,Expect::CWD_CURR);
	 }
	 else if(last_cwd && !xstrcmp(last_cwd->arg,cwd))
	 {
	    // no need for extra CWD, one's already sent.
	    last_cwd->check_case=Expect::CWD_CURR;
	 }
      }
#if USE_SSL
      if(conn->ssl_is_activated() || (conn->auth_supported && conn->auth_sent))
      {
	 char want_prot=conn->prot;
	 const char *protect_res=get_protect_res();
	 if(protect_res)
	    want_prot=QueryBool(protect_res,hostname)?'P':'C';
	 if(copy_mode!=COPY_NONE)
	    want_prot=copy_protect?'P':'C';

	 bool want_sscn=conn->sscn_on;
	 if(copy_mode!=COPY_NONE)
	    want_sscn=copy_protect && copy_ssl_connect
		      && !(copy_passive && conn->cpsv_supported);
	 else if(mode==RETRIEVE || mode==STORE || mode==LIST || mode==MP_LIST
		  || (mode==LONG_LIST && !use_stat_for_list))
	    want_sscn=false;

	 if(conn->sscn_supported && want_sscn!=conn->sscn_on)
	 {
	    conn->SendCmd2("SSCN",want_sscn?"ON":"OFF");
	    expect->Push(new Expect(Expect::SSCN,want_sscn?'Y':'N'));
	 }
	 SendPROT(want_prot);
      }
#endif
      state=CWD_CWD_WAITING_STATE;
      m=MOVED;
   /* fallthrough */
   case CWD_CWD_WAITING_STATE:
   {
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=CWD_CWD_WAITING_STATE || Error())
	 return MOVED;

      // wait for all CWD to finish
      if(mode!=CHANGE_DIR && expect->FindLastCWD())
	 goto usual_return;
#if USE_SSL
      // PROT and SSCN are critical for data transfers
      if(expect->Has(Expect::PROT)
      || expect->Has(Expect::SSCN))
	 goto usual_return;
#endif

      // address of peer is not known yet
      if(copy_mode!=COPY_NONE && !copy_passive && !copy_addr_valid)
	 goto usual_return;

      if(entity_size>=0 && entity_size<=pos
      && (mode==RETRIEVE || (mode==STORE && entity_size>0)))
      {
	 if(mode==STORE)
	    SendUTimeRequest();
	 eof=true;
	 goto pre_WAITING_STATE; // simulate eof.
      }

      if(!conn->rest_supported)
	 flags|=NOREST_MODE;

      if(mode==STORE && GetFlag(NOREST_MODE) && pos>0)
	 pos=0;

      if(copy_mode==COPY_NONE
      && (mode==RETRIEVE || mode==STORE || mode==LIST || mode==MP_LIST
          || (mode==LONG_LIST && !use_stat_for_list)))
      {
	 assert(conn->data_sock==-1);
	 conn->data_sock=socket(conn->peer_sa.sa.sa_family,SOCK_STREAM,IPPROTO_TCP);
	 if(conn->data_sock==-1)
	 {
	    LogError(0,"socket(data): %s",strerror(errno));
	    goto system_error;
	 }
	 NonBlock(conn->data_sock);
	 CloseOnExec(conn->data_sock);
	 KeepAlive(conn->data_sock);
	 MaximizeThroughput(conn->data_sock);
	 SetSocketBuffer(conn->data_sock);
	 SetSocketMaxseg(conn->data_sock);

	 addr_len=sizeof(conn->data_sa);
	 getsockname(conn->control_sock,&conn->data_sa.sa,&addr_len);

	 // Try to assign a port from given range
	 Range range(Query("port-range"));
	 for(int t=0; ; t++)
	 {
	    if(t>=10)
	    {
	       close(conn->data_sock);
	       conn->data_sock=-1;
	       TimeoutS(1);	 // retry later.
	       return m;
	    }
	    if(t==9)
	       ReuseAddress(conn->data_sock);   // try to reuse address.

	    int port=0;
	    if(!range.IsFull())
	       port=range.Random();

	    bool do_addr_bind=QueryBool("bind-data-socket")
			   && !IsLoopback(&conn->peer_sa);

	    if(!do_addr_bind && !port)
		break;	// nothing to bind

	    if(conn->data_sa.sa.sa_family==AF_INET)
	    {
	       conn->data_sa.in.sin_port=htons(port);
	       if(!do_addr_bind)
		  memset(&conn->data_sa.in.sin_addr,0,sizeof(conn->data_sa.in.sin_addr));
	    }
#if INET6
	    else if(conn->data_sa.sa.sa_family==AF_INET6)
	    {
	       conn->data_sa.in6.sin6_port=htons(port);
	       if(!do_addr_bind)
		  memset(&conn->data_sa.in6.sin6_addr,0,sizeof(conn->data_sa.in6.sin6_addr));
	    }
#endif
	    else
	    {
	       Fatal("unsupported network protocol");
	       return MOVED;
	    }

	    if(bind(conn->data_sock,&conn->data_sa.sa,addr_len)==0)
	       break;
	    int saved_errno=errno;

	    // Fail unless socket was already taken
	    if(errno!=EINVAL && errno!=EADDRINUSE)
	    {
	       LogError(0,"bind(data_sock,[%s]:%d): %s",
		  SocketNumericAddress(&conn->data_sa),port,strerror(saved_errno));
	       close(conn->data_sock);
	       conn->data_sock=-1;
	       if(NonFatalError(errno))
	       {
		  TimeoutS(1);
		  return m;
	       }
	       SetError(SEE_ERRNO,"Cannot bind data socket for ftp:port-range");
	       return MOVED;
	    }
	    LogError(10,"bind(data_sock,[%s]:%d): %s",
	       SocketNumericAddress(&conn->data_sa),port,strerror(saved_errno));
	 }

	 if(!GetFlag(PASSIVE_MODE))
	    listen(conn->data_sock,1);

	 // get the allocated port
	 addr_len=sizeof(conn->data_sa);
	 getsockname(conn->data_sock,&conn->data_sa.sa,&addr_len);
      }

      char want_type=(ascii?'A':'I');
      if(GetFlag(NOREST_MODE) || pos==0)
	 real_pos=0;
      else
	 real_pos=-1;	// we don't yet know if REST will succeed

      flags&=~IO_FLAG;
      last_priority=priority;
      conn->received_150=false;

      switch((enum open_mode)mode)
      {
      case(RETRIEVE):
	 if(file[0]==0)
	    goto long_list;
	 command="RETR";
	 append_file=true;
         break;
      case(STORE):
	 if(!QueryBool("rest-stor",hostname))
	 {
	    real_pos=0;	// some old servers don't handle REST/STOR properly.
	    pos=0;
	 }
         command="STOR";
	 append_file=true;
         break;
      long_list:
      case(LONG_LIST):
	 if(use_stat_for_list)
	 {
	    real_pos=0;
	    command="STAT";
	    conn->data_iobuf=new IOBuffer(IOBuffer::GET);
	    rate_limit=new RateLimit(hostname);
	    want_type=conn->type;
	 }
	 else
	 {
	    want_type='A';
	    if(!rest_list)
	       real_pos=0;	// some ftp servers do not do REST/LIST.
	    command="LIST";
	 }
	 if(list_options && list_options[0])
	    command=xstring::cat(command," ",list_options.get(),NULL);
	 if(file && file[0])
	    append_file=true;
	 if(use_stat_for_list && !append_file && !strchr(command,' '))
	    command="STAT .";
         break;
      case(MP_LIST):
         want_type='A';
         real_pos=0; // REST doesn't work for MLSD
	 command="MLSD";
	 if(file && file[0])
	    append_file=true;
         break;
      case(LIST):
         want_type='A';
         real_pos=0; // REST doesn't work for NLST
	 command="NLST";
	 if(file && file[0])
            append_file=true;
         break;
      case(CHANGE_DIR):
	 if(!xstrcmp(real_cwd,file))
	    cwd.Set(real_cwd,false,0,device_prefix_len(real_cwd));
	 else
	 {
	    int len=real_cwd.length();
	    const char *path_to_use=file;
	    if(!conn->vms_path && !AbsolutePath(file) && real_cwd
	    && !strncmp(file,real_cwd,len) && file[len]=='/')
	       path_to_use=file+len+1;
	    SendCWD(path_to_use,Expect::CWD,file);
	 }
	 goto pre_WAITING_STATE;
      case(MAKE_DIR):
	 command="MKD";
	 append_file=true;
	 want_type=conn->type;
	 break;
      case(REMOVE_DIR):
	 command="RMD";
	 append_file=true;
	 want_type=conn->type;
	 break;
      case(REMOVE):
	 command="DELE";
	 append_file=true;
	 want_type=conn->type;
	 break;
      case(QUOTE_CMD):
	 real_pos=0;
	 command="";
	 append_file=true;
	 conn->data_iobuf=new IOBuffer(IOBuffer::GET);
	 rate_limit=new RateLimit(hostname);
	 break;
      case(RENAME):
	 command="RNFR";
	 append_file=true;
	 want_type=conn->type;
	 break;
      case(ARRAY_INFO):
	 break;
      case(CHANGE_MODE):
	 {
	    char *c=string_alloca(11+30);
	    sprintf(c,"SITE CHMOD %03o",chmod_mode);
	    command=c;
	    append_file=true;
	    want_type=conn->type;
	    break;
	 }
      case(CONNECT_VERIFY):
      case(CLOSED):
	 state=EOF_STATE;
	 return MOVED;
      }
      if(want_type!=conn->type)
      {
	 conn->SendCmdF("TYPE %c",want_type);
	 expect->Push(new Expect(Expect::TYPE,want_type));
      }

      if(opt_size && conn->size_supported && file[0] && use_size)
      {
	 conn->SendCmd2("SIZE",file,url::path_ptr(file_url),home);
	 expect->Push(Expect::SIZE_OPT);
      }
      if(opt_date && conn->mdtm_supported && file[0] && use_mdtm)
      {
	 conn->SendCmd2("MDTM",file,url::path_ptr(file_url),home);
	 expect->Push(Expect::MDTM_OPT);
      }

      if(mode==ARRAY_INFO)
      {
	 SendArrayInfoRequests();
	 goto pre_WAITING_STATE;
      }

      if(mode==QUOTE_CMD || mode==CHANGE_MODE || (mode==LONG_LIST && use_stat_for_list)
      || mode==REMOVE || mode==REMOVE_DIR || mode==MAKE_DIR || mode==RENAME)
      {
	 if(mode==MAKE_DIR && mkdir_p)
	 {
	    const char *sl=strchr(file,'/');
	    while(sl)
	    {
	       if(sl>file)
	       {
		  xstring& tmp=xstring::get_tmp(file,sl-file);
		  if(tmp.ne(".") && tmp.ne(".."))
		  {
		     conn->SendCmd2("MKD",tmp);
		     expect->Push(Expect::IGNORE);
		  }
	       }
	       sl=strchr(sl+1,'/');
	    }
	 }

	 if(append_file)
	    conn->SendCmd2(command,file,url::path_ptr(file_url),home);
	 else
	    conn->SendCmd(command);

	 if(mode==QUOTE_CMD)
	 {
	    expect->Push(Expect::QUOTED);
	    if(!strncasecmp(file,"CWD",3)
	    || !strncasecmp(file,"CDUP",4)
	    || !strncasecmp(file,"XCWD",4)
	    || !strncasecmp(file,"XCUP",4))
	    {
	       LogNote(9,"Resetting cwd");
	       set_real_cwd(0);  // we do not know the path now.
	    }
	 }
	 else if(mode==LONG_LIST)
	    expect->Push(Expect::QUOTED);
	 else if(mode==RENAME)
	    expect->Push(Expect::RNFR);
	 else
	    expect->Push(Expect::FILE_ACCESS);

	 goto pre_WAITING_STATE;
      }

      if((copy_mode==COPY_NONE && GetFlag(PASSIVE_MODE))
      || (copy_mode!=COPY_NONE && copy_passive))
      {
	 if(use_pret && conn->pret_supported)
	 {
	    conn->SendCmd(xstring::cat("PRET ",command," ",file.get(),NULL));
	    expect->Push(Expect::PRET);
	 }
	 bool can_do_pasv=(conn->peer_sa.sa.sa_family==AF_INET);
#if INET6
	 can_do_pasv|=(conn->peer_sa.sa.sa_family==AF_INET6
			&& IN6_IS_ADDR_V4MAPPED(&conn->peer_sa.in6.sin6_addr));
#endif
	 if(can_do_pasv && !(conn->epsv_supported && QueryBool("prefer-epsv",hostname)))
	 {
#if USE_SSL
	    if(copy_mode!=COPY_NONE && conn->prot=='P' && !conn->sscn_on && copy_ssl_connect)
	       conn->SendCmd("CPSV"); // same as PASV, but server does SSL_connect
	    else
#endif // note the following statement
	       conn->SendCmd("PASV");
	    expect->Push(Expect::PASV);
	    pasv_state=PASV_NO_ADDRESS_YET;
	 }
	 else
	 {
	    conn->SendCmd("EPSV");
	    expect->Push(Expect::EPSV);
	    pasv_state=PASV_NO_ADDRESS_YET;
	 }
      }
      else // !PASSIVE
      {
	 if(copy_mode!=COPY_NONE)
	    conn->data_sa=copy_addr;
	 if(conn->data_sa.sa.sa_family==AF_INET)
	 {
	    a=(const unsigned char*)&conn->data_sa.in.sin_addr;
	    p=(const unsigned char*)&conn->data_sa.in.sin_port;
	    sockaddr_u control_sa;
	    // check if data socket address is unbound
	    if((a[0]|a[1]|a[2]|a[3])==0)
	    {
	       socklen_t addr_len=sizeof(control_sa);
	       getsockname(conn->control_sock,&control_sa.sa,&addr_len);
	       a=(const unsigned char*)&control_sa.in.sin_addr;
	    }
#if INET6
	 ipv4_port:
#endif
	    if(copy_mode==COPY_NONE)
	    {
	       const char *port_ipv4=Query("port-ipv4",hostname);
	       struct in_addr fake_ip;
	       if(port_ipv4 && port_ipv4[0])
	       {
		  if(inet_pton(AF_INET,port_ipv4,&fake_ip))
		     a=(const unsigned char*)&fake_ip;
	       }
	    }
	    conn->SendCmdF("PORT %d,%d,%d,%d,%d,%d",a[0],a[1],a[2],a[3],p[0],p[1]);
	    expect->Push(Expect::PORT);
	 }
	 else
	 {
#if INET6
	    if(conn->data_sa.sa.sa_family==AF_INET6
	       && IN6_IS_ADDR_V4MAPPED(&conn->data_sa.in6.sin6_addr))
	    {
	       a=((unsigned char*)&conn->data_sa.in6.sin6_addr)+12;
	       p=(unsigned char*)&conn->data_sa.in6.sin6_port;
	       goto ipv4_port;
	    }
	    conn->SendCmd2("EPRT",encode_eprt(&conn->data_sa));
	    expect->Push(Expect::PORT);
#else
	    Fatal(_("unsupported network protocol"));
	    return MOVED;
#endif
	 }
      }
      if(mode==STORE && entity_size!=NO_SIZE && QueryBool("use-allo",hostname))
      {
	 // ALLO is usually ignored by servers, but send it anyway.
	 conn->SendCmdF("ALLO %lld",(long long)entity_size);
	 expect->Push(Expect::ALLO);
      }
      // some broken servers don't reset REST after a transfer,
      // so check if last_rest was different.
      if(real_pos==-1 || conn->last_rest!=real_pos)
      {
         conn->rest_pos=(real_pos!=-1?real_pos:pos);
	 conn->SendCmdF("REST %lld",(long long)conn->rest_pos);
	 expect->Push(Expect::REST);
	 real_pos=-1;
      }
      if(copy_mode!=COPY_DEST || copy_allow_store)
      {
	 if(append_file)
	    conn->SendCmd2(command,file,url::path_ptr(file_url),home);
	 else
	    conn->SendCmd(command);
	 expect->Push(Expect::TRANSFER);
      }
      m=MOVED;
      if(copy_mode!=COPY_NONE && !copy_passive)
	 goto pre_WAITING_STATE;
      if((copy_mode==COPY_NONE && GetFlag(PASSIVE_MODE))
      || (copy_mode!=COPY_NONE && copy_passive))
      {
	 state=DATASOCKET_CONNECTING_STATE;
      	 goto datasocket_connecting_state;
      }
      state=ACCEPTING_STATE;
   }
   /* fallthrough */
   case(ACCEPTING_STATE):
      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=ACCEPTING_STATE || Error())
         return MOVED;

      res=Poll(conn->data_sock,POLLIN);
      if(res==-1)
      {
	 Disconnect();
         return MOVED;
      }

      if(!(res&POLLIN))
	 goto usual_return;

      addr_len=sizeof(struct sockaddr);
      res=accept(conn->data_sock,(struct sockaddr *)&conn->data_sa,&addr_len);
      if(res==-1)
      {
	 if(errno==EWOULDBLOCK)
	    goto usual_return;
	 if(NotSerious(errno))
	 {
	    LogError(0,"%s",strerror(errno));
	    Disconnect();
	    return MOVED;
	 }
	 goto system_error;
      }

      close(conn->data_sock);
      conn->data_sock=res;
      NonBlock(conn->data_sock);
      CloseOnExec(conn->data_sock);
      KeepAlive(conn->data_sock);
      MaximizeThroughput(conn->data_sock);
      SetSocketBuffer(conn->data_sock);
      SetSocketMaxseg(conn->data_sock);

      LogNote(5,_("Accepted data connection from (%s) port %u"),
	 SocketNumericAddress(&conn->data_sa),SocketPort(&conn->data_sa));
      if(!conn->data_address_ok(0,verify_data_address,verify_data_port))
      {
	 Disconnect();
	 return MOVED;
      }

      goto pre_waiting_150;

   case(DATASOCKET_CONNECTING_STATE):
   datasocket_connecting_state:
      if(pasv_state!=PASV_DATASOCKET_CONNECTING)
	 m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=DATASOCKET_CONNECTING_STATE || Error())
         return MOVED;

      switch(pasv_state)
      {
      case PASV_NO_ADDRESS_YET:
	 goto usual_return;

      case PASV_HAVE_ADDRESS:
	 if(copy_mode==COPY_NONE
	 && !conn->data_address_ok(&conn->data_sa,verify_data_address,/*port_verify*/false))
	 {
	    Disconnect();
	    return MOVED;
	 }

	 pasv_state=PASV_DATASOCKET_CONNECTING;
	 if(copy_mode!=COPY_NONE)
	 {
	    memcpy(&copy_addr,&conn->data_sa,sizeof(conn->data_sa));
	    copy_addr_valid=true;
	    goto pre_WAITING_STATE;
	 }

	 if(!conn->proxy_is_http)
	 {
	    LogNote(5,_("Connecting data socket to (%s) port %u"),
	       SocketNumericAddress(&conn->data_sa),SocketPort(&conn->data_sa));
	    res=SocketConnect(conn->data_sock,&conn->data_sa);
	 }
	 else // proxy_is_http
	 {
	    LogNote(5,_("Connecting data socket to proxy %s (%s) port %u"),
	       proxy.get(),SocketNumericAddress(&conn->peer_sa),SocketPort(&conn->peer_sa));
	    res=SocketConnect(conn->data_sock,&conn->peer_sa);
	 }
	 if(res==-1 && errno!=EINPROGRESS)
	 {
	    LogError(0,"connect: %s",strerror(errno));
	    Disconnect();
	    if(NotSerious(errno))
	       return MOVED;
	    goto system_error;
	 }
	 m=MOVED;
      /* fallthrough */
      case PASV_DATASOCKET_CONNECTING:
	 res=Poll(conn->data_sock,POLLOUT);
	 if(res==-1)
	 {
	    if(conn->fixed_pasv && QueryBool("auto-passive-mode",hostname))
	    {
	       LogNote(2,_("Switching passive mode off"));
	       SetFlag(PASSIVE_MODE,0);
	    }
	    Disconnect();
	    return MOVED;
	 }
	 if(!(res&POLLOUT))
	    goto usual_return;
	 LogNote(9,_("Data connection established"));
	 if(!conn->proxy_is_http)
	    goto pre_waiting_150;

	 pasv_state=PASV_HTTP_PROXY_CONNECTED;
	 m=MOVED;
	 conn->data_iobuf=new IOBufferFDStream(new FDStream(conn->data_sock,"data-socket"),IOBuffer::PUT);
	 HttpProxySendConnectData();
	 conn->data_iobuf->Roll();
	 // FIXME, data_iobuf could be not done yet
	 conn->data_iobuf=new IOBufferFDStream(new FDStream(conn->data_sock,"data-socket"),IOBuffer::GET);
      /* fallthrough */
      case PASV_HTTP_PROXY_CONNECTED:
      	 if(HttpProxyReplyCheck(conn->data_iobuf))
	    goto pre_waiting_150;
	 goto usual_return;
      }
   /* fallthrough */
   pre_waiting_150:
      state=WAITING_150_STATE;
      conn->waiting_150_timer.Reset();
      rate_limit=new RateLimit(hostname);
      m=MOVED;
   case WAITING_150_STATE:
      m|=FlushSendQueue();
      m|=ReceiveResp();
      if(state!=WAITING_150_STATE || Error())
         return MOVED;
      if(!conn->received_150 && !expect->IsEmpty() && !conn->waiting_150_timer.Stopped())
	 goto usual_return;

      // now init data connection properly and start data exchange
      state=DATA_OPEN_STATE;
      m=MOVED;

#if USE_SSL
      if(conn->prot=='P')
      {
	 Ref<lftp_ssl> ssl(new lftp_ssl(conn->data_sock,lftp_ssl::CLIENT,hostname));
	 if(QueryBool("ssl-data-use-keys",hostname) || !conn->control_ssl)
	    ssl->load_keys();
	 // share session id between control and data connections.
	 if(conn->control_ssl)
	    ssl->copy_sid(conn->control_ssl);

	 IOBuffer::dir_t dir=(mode==STORE?IOBuffer::PUT:IOBuffer::GET);
	 IOBufferSSL *ssl_buf=new IOBufferSSL(ssl.borrow(),dir);
	 conn->data_iobuf=ssl_buf;
      }
      else  // note the following block
#endif
      {
	 IOBuffer::dir_t dir=(mode==STORE?IOBuffer::PUT:IOBuffer::GET);
	 if(!conn->data_iobuf || conn->data_iobuf->GetDirection()!=dir)
	    conn->data_iobuf=new IOBufferFDStream(new FDStream(conn->data_sock,"data-socket"),dir);
      }
      if(mode==LIST || mode==LONG_LIST || mode==MP_LIST)
      {
	 if(conn->utf8_activated)
	    conn->data_iobuf->SetTranslation("UTF-8",true);
	 else if(charset && *charset)
	    conn->data_iobuf->SetTranslation(charset,true);
      }
   /* fallthrough */
   case(DATA_OPEN_STATE):
   {
      if(expect->IsEmpty() && conn->data_sock!=-1)
      {
	 // When ftp server has sent "Transfer complete" it is idle,
	 // but the data can be still unsent in server side kernel buffer.
	 // So the ftp server can decide the connection is idle for too long
	 // time and disconnect. This hack is to prevent the above.
	 if(now.UnixTime() >= conn->nop_time+nop_interval)
	 {
	    // prevent infinite NOOP's
	    if(conn->nop_offset==pos
	    && timeout_timer.GetLastSetting()<conn->nop_count*nop_interval)
	    {
	       LogError(1,"NOOP timeout");
	       HandleTimeout();
	       return MOVED;
	    }
	    if(conn->nop_time!=0)
	    {
	       conn->nop_count++;
	       conn->SendCmd("NOOP");
	       expect->Push(Expect::IGNORE);
	    }
	    conn->nop_time=now;
	    if(conn->nop_offset!=pos)
	       conn->nop_count=0;
	    conn->nop_offset=pos;
	 }
	 TimeoutS(nop_interval-(time_t(now)-conn->nop_time));
      }

      oldstate=state;

      m|=FlushSendQueue();
      m|=ReceiveResp();

      if(state!=oldstate || Error())
         return MOVED;

      timeout_timer.Reset(conn->data_iobuf->EventTime());
      if(conn->data_iobuf->Error() && conn->data_sock!=-1)
      {
	 LogError(0,"%s",conn->data_iobuf->ErrorText());
	 conn->CloseDataSocket();
	 // workaround for proftpd bug - it resets data connection when no files found.
	 if(mode==LIST && expect->IsEmpty() && !conn->received_150 && conn->data_iobuf->GetPos()==0)
	 {
	    DataClose();
	    state=EOF_STATE;
	    eof=true;
	    return MOVED;
	 }
      }
      // handle errors on data connection only when storing or got all replies
      // and read all data.
      if(conn->data_iobuf->Error()
      && (mode==STORE || (expect->IsEmpty() && conn->data_iobuf->Size()==0)))
      {
	 if(conn->data_iobuf->ErrorFatal())
	    SetError(FATAL,conn->data_iobuf->ErrorText());
	 if(!expect->IsEmpty())
	    DisconnectNow();
	 else
	 {
	    DataClose();
	    state=EOF_STATE;
	    if(mode==STORE && GetFlag(IO_FLAG))
	       SetError(STORE_FAILED,0);
	    else if(NextTry())
	       retry_timer.Set(2); // retry after 2 seconds
	 }
	 return MOVED;
      }
      if(mode!=STORE)
      {
	 if(conn->data_iobuf->Size()>=rate_limit->BytesAllowedToGet())
	 {
	    conn->data_iobuf->Suspend();
	    TimeoutS(1);
	 }
	 else if(conn->data_iobuf->Size()>=max_buf)
	 {
	    conn->data_iobuf->Suspend();
	    m=MOVED;
	 }
	 else if(conn->data_iobuf->IsSuspended())
	 {
	    conn->data_iobuf->Resume();
	    if(conn->data_iobuf->Size()>0)
	       m=MOVED;
	 }
	 if(conn->data_iobuf->Size()==0 && conn->data_iobuf->Eof())
	 {
	    LogNote(9,"Got EOF on data connection");
	    DataClose();
	    if(expect->IsEmpty())
	    {
	       eof=true;
	       m=MOVED;
	    }
	    state=WAITING_STATE;
	 }
      }

      if(state!=oldstate || Error())
         return MOVED;

      CheckTimeout();

      if(state!=oldstate)
         return MOVED;

      goto usual_return;
   }

   pre_WAITING_STATE:
      if(copy_mode!=COPY_NONE)
	 TrySuccess();	// it is enough to get here in copying.
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
      if(expect->IsEmpty() && mode==ARRAY_INFO && array_ptr<array_cnt)
      {
	 SendArrayInfoRequests();
	 return MOVED;
      }

      if(conn->data_iobuf)
      {
	 if(expect->IsEmpty() && conn->data_sock==-1 && !conn->data_iobuf->Eof())
	 {
	    conn->data_iobuf->PutEOF();
	    m=MOVED;
	 }
	 timeout_timer.Reset(conn->data_iobuf->EventTime());
	 if(conn->data_iobuf->Eof() && conn->data_iobuf->Size()==0)
	 {
	    state=EOF_STATE;
	    DataAbort();
	    DataClose();
	    idle_timer.Reset();
	    eof=true;
	    return MOVED;
	 }
      }

      if(copy_mode==COPY_DEST && !copy_allow_store)
	 goto notimeout_return;

      if(copy_mode==COPY_DEST && !copy_done && copy_connection_open
      && expect->Count()==1 && use_stat
      && !conn->ssl_is_activated() && !conn->proxy_is_http)
      {
	 if(conn->stat_timer.Stopped())
	 {
	    // send STAT to know current position.
	    SendUrgentCmd("STAT");
	    expect->Push(Expect::TRANSFER);
	    FlushSendQueue(true);
	    m=MOVED;
	 }
      }

      // FXP is special - no data connection at all.
      if(copy_mode!=COPY_NONE)
	 goto notimeout_return;

      goto usual_return;
   }
   case WAITING_CCC_SHUTDOWN:
      if(conn->control_recv->Error())
      {
	 if(conn->control_recv->ErrorFatal())
	    SetError(FATAL,conn->control_recv->ErrorText());
	 Disconnect();
	 return MOVED;
      }
      if(conn->control_recv->Eof()
      || conn->waiting_ssl_timer.Stopped())
      {
	 conn->MakeBuffers();
	 goto pre_EOF_STATE;
      }
      break;
   } /* end of switch */
usual_return:
   if(m==MOVED)
      return MOVED;
   if(CheckTimeout())
      return MOVED;
notimeout_return:
   if(m==MOVED)
      return MOVED;
   if(conn && conn->data_sock!=-1)
   {
      if(state==ACCEPTING_STATE)
	 Block(conn->data_sock,POLLIN);
      else if(state==DATASOCKET_CONNECTING_STATE)
      {
	 if(pasv_state==PASV_DATASOCKET_CONNECTING)
	    Block(conn->data_sock,POLLOUT);
      }
   }
   if(conn && conn->control_sock!=-1)
   {
      if(state==CONNECTING_STATE)
	 Block(conn->control_sock,POLLOUT);
   }
   return m;

system_error:
   if(NonFatalError(errno))
   {
      TimeoutS(1);
      return m;
   }
   DisconnectNow();
   SetError(SEE_ERRNO,0);
   return MOVED;
}

#if USE_SSL
void Ftp::SendAuth(const char *auth)
{
   if(conn->auth_sent || conn->ssl_is_activated())
      return;
   if(!conn->auth_supported)
   {
      if(QueryBool("ssl-force",hostname))
	 SetError(LOGIN_FAILED,_("ftp:ssl-force is set and server does not support or allow SSL"));
      return;
   }

   if(conn->auth_args_supported)
   {
      char *a=alloca_strdup(conn->auth_args_supported);
      bool saw_ssl=false;
      bool saw_tls=false;
      for(a=strtok(a,";"); a; a=strtok(0,";"))
      {
	 if(!strcasecmp(a,auth))
	    break;
	 if(!strcasecmp(a,"SSL"))
	    saw_ssl=true;
	 else if(!strcasecmp(a,"TLS"))
	    saw_tls=true;
      }
      if(!a)
      {
	 const char *old_auth=auth;
	 if(saw_tls)
	    auth="TLS";
	 else if(saw_ssl)
	    auth="SSL";
	 LogError(1,"AUTH %s is not supported, using AUTH %s instead",old_auth,auth);
      }
   }
   conn->SendCmd2("AUTH",auth);
   expect->Push(Expect::AUTH_TLS);
   conn->auth_sent=true;
   if(!strcmp(auth,"TLS")
   || !strcmp(auth,"TLS-C"))
      conn->prot='C';
   else
      conn->prot='P';
}
void Ftp::SendPROT(char want_prot)
{
   if(want_prot==conn->prot || !conn->auth_supported)
      return;
   conn->SendCmdF("PROT %c",want_prot);
   expect->Push(new Expect(Expect::PROT,want_prot));
}
#endif // USE_SSL

void Ftp::SendSiteIdle()
{
   if(!QueryBool("use-site-idle"))
      return;
   conn->SendCmd2("SITE IDLE",idle_timer.GetLastSetting().Seconds());
   expect->Push(Expect::IGNORE);
}
void Ftp::SendUTimeRequest()
{
   if(entity_date==NO_DATE || !file)
      return;

   char d[15];
   time_t n=entity_date;
   strftime(d,sizeof(d),"%Y%m%d%H%M%S",gmtime(&n));
   d[sizeof(d)-1]=0;

   if(conn->mfmt_supported)
   {
      conn->SendCmd2(xstring::format("MFMT %s",d),file,url::path_ptr(file_url),home);
      expect->Push(Expect::IGNORE);
   }
   else if(QueryBool("use-site-utime2") && conn->site_utime2_supported)
   {
      conn->SendCmd2(xstring::format("SITE UTIME %s",d),file,url::path_ptr(file_url),home);
      expect->Push(Expect::SITE_UTIME2);
   }
   else if(QueryBool("use-site-utime") && conn->site_utime_supported)
   {
      conn->SendCmd(xstring::format("SITE UTIME %s %s %s %s UTC",file.get(),d,d,d));
      expect->Push(Expect::SITE_UTIME);
   }
   else if(QueryBool("use-mdtm-overloaded"))
   {
      conn->SendCmd2(xstring::format("MDTM %s",d),file,url::path_ptr(file_url),home);
      expect->Push(Expect::IGNORE);
   }
}
const char *Ftp::QueryStringWithUserAtHost(const char *var)
{
   const char *u=user?user.get():"anonymous";
   const char *h=hostname?hostname.get():"";
   const char *closure=xstring::cat(u,"@",h,NULL);
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
   conn->SendCmd2("ACCT",acct);
   expect->Push(Expect::IGNORE);
}
void Ftp::SendSiteGroup()
{
   const char *group=QueryStringWithUserAtHost("site-group");
   if(!group)
      return;
   conn->SendCmd2("SITE GROUP",group);
   expect->Push(Expect::IGNORE);
}

void Ftp::SendArrayInfoRequests()
{
   for(int i=array_ptr; i<array_cnt; i++)
   {
      bool sent=false;
      if(array_for_info[i].get_time && conn->mdtm_supported && use_mdtm)
      {
	 conn->SendCmd2("MDTM",ExpandTildeStatic(array_for_info[i].file));
	 expect->Push(Expect::MDTM);
	 sent=true;
      }
      else
      {
	 array_for_info[i].time=NO_DATE;
      }
      if(array_for_info[i].get_size && conn->size_supported && use_size)
      {
	 conn->SendCmd2("SIZE",ExpandTildeStatic(array_for_info[i].file));
	 expect->Push(Expect::SIZE);
	 sent=true;
      }
      else
      {
	 array_for_info[i].size=NO_SIZE;
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
	 if(GetFlag(SYNC_MODE))
	    break;	   // don't flood the queues.
      }
   }
}

int Ftp::ReplyLogPriority(int code)
{
   // Greeting messages
   if(code==220 || code==230)
      return 3;
   if(code==250 && mode==CHANGE_DIR)
      return 3;
   if(code==451 && mode==CLOSED)
      return 4;
   /* Most 5XXs go to level 4, as it's the job's responsibility to
    * print fatal errors. Some 5XXs are treated as 4XX's; send those
    * to level 0. (Maybe they should go to 1; we're going to retry them,
    * after all. */
   if(is5XX(code))
      return Transient5XX(code)? 0:4;

   if(is4XX(code))
      return 0;

   // 221 is the reply to QUIT, but we don't expect it.
   if(code==221 && !conn->quit_sent)
      return 0;

   return 4;
}

int Ftp::ReceiveOneLine()
{
   const char *resp;
   int resp_size;
   conn->control_recv->Get(&resp,&resp_size);
   if(resp==0) // eof
   {
      LogError(0,_("Peer closed connection"));
      DisconnectNow();
      return -1;
   }
   if(resp_size==0)
      return 0;
   int line_len=0;
   int skip_len=0;
   // find <CR><NL> pair
   const char *nl=find_char(resp,resp_size,'\n');
   for(;;)
   {
      if(!nl)
      {
	 if(conn->control_recv->Eof())
	 {
	    skip_len=line_len=resp_size;
	    break;
	 }
	 return 0;
      }
      if(nl>resp && nl[-1]=='\r')
      {
	 line_len=nl-resp-1;
	 skip_len=nl-resp+1;
	 break;
      }
      if(nl==resp+resp_size-1 && now-conn->control_recv->EventTime()>5)
      {
	 LogError(1,"server bug: single <NL>");
	 line_len=nl-resp;
	 skip_len=nl-resp+1;
	 break;
      }
      nl=find_char(nl+1,resp_size-(nl+1-resp),'\n');
   }

   line.nset(resp,line_len);
   conn->control_recv->Skip(skip_len);

   // Change <CR><NUL> to <CR> according to RFC2640.
   // Other occurencies of <NUL> are changed to '!'.
   char *w=line.get_non_const();
   const char *r=w;
   for(int i=line.length(); i>0; i--,r++)
   {
      if(*r)
	 *w++=*r;
      else if(r==line || r[-1]!='\r')
	 *w++='!';
   }
   line.truncate(line.length()-(r-w));
   return line.length();
}

int  Ftp::ReceiveResp()
{
   int m=STALL;

   if(!conn || !conn->control_recv)
      return m;

   timeout_timer.Reset(conn->control_recv->EventTime());
   if(conn->control_recv->Error())
   {
      LogError(0,"%s",conn->control_recv->ErrorText());
      if(conn->control_recv->ErrorFatal())
	 SetError(FATAL,conn->control_recv->ErrorText());
      DisconnectNow();
      return MOVED;
   }

   for(;;)  // handle all lines in buffer, one line per loop
   {
      if(!conn || !conn->control_recv)
	 return m;

      int res=ReceiveOneLine();
      if(res==-1)
	 return MOVED;
      if(res==0)
	 return m;

      int code=0;
      if(line.length()>=3 && is_ascii_digit(line[0])
      && is_ascii_digit(line[1]) && is_ascii_digit(line[2]))
	 sscanf(line,"%3d",&code);

      if(conn->multiline_code && conn->multiline_code!=code)
	 code=0;  // reply can only terminate with the same code

      int log_prio=ReplyLogPriority(conn->multiline_code?conn->multiline_code:code);

      bool is_first_line=(line[3]=='-' && conn->multiline_code==0);
      bool is_last_line=(line[3]!='-' && code!=0);

      bool is_data=(!expect->IsEmpty() && expect->FirstIs(Expect::QUOTED) && conn->data_iobuf);
      int data_offset=0;
      if(is_data && mode==LONG_LIST)
      {
	 if(code && !is2XX(code))
	    is_data=false;
	 if(code && line.length()>4)
	 {
	    data_offset=4;
	    if(is_first_line && strstr(line+data_offset,"FTP server status"))
	    {
	       TurnOffStatForList();
	       is_data=false;
	    }
	    if((is_first_line && !strncasecmp(line+data_offset,"Stat",4))
	    || (is_last_line  && !strncasecmp(line+data_offset,"End",3)))
	       is_data=false;
	 }
      }
      if(is_data && conn->data_iobuf)
      {
	 if(line[data_offset]==' ')
	    data_offset++;
	 conn->data_iobuf->Put(line+data_offset,line.length()-data_offset);
	 conn->data_iobuf->Put("\n");
	 log_prio=10;
      }
      LogRecv(log_prio,line);

      if(conn->multiline_code==0 || all_lines.length()==0)
	 all_lines.set(line); // not continuation
      else if(all_lines.length()<0x4000)
	 all_lines.vappend("\n",line.get(),NULL);

      if(code==0)
	 continue;

      if(line[3]=='-')
      {
	 if(conn->multiline_code==0)
	    conn->multiline_code=code;
	 continue;
      }
      if(conn->multiline_code && line[3]!=' ')
	 continue; // The space is required to terminate multiline reply
      conn->multiline_code=0;

      if(conn->sync_wait>0 && !is1XX(code))
	 conn->sync_wait--; // clear the flag to send next command

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
	    LogNote(4,_("Persist and retry"));
	    return m;
	 }
      }
   }
   return m;
}

void Ftp::HttpProxySendAuth(const SMTaskRef<IOBuffer>& buf)
{
   if(!proxy_user || !proxy_pass)
      return;
   xstring& auth=xstring::cat(proxy_user.get(),":",proxy_pass.get(),NULL);
   int auth_len=auth.length();
   char *buf64=string_alloca(base64_length(auth_len)+1);
   base64_encode(auth,buf64,auth_len);
   buf->Format("Proxy-Authorization: Basic %s\r\n",buf64);
   Log::global->Format(4,"+--> Proxy-Authorization: Basic %s\r\n",buf64);
}
void Ftp::HttpProxySendConnect()
{
   const char *the_port=portname?portname.get():ftps?FTPS_DEFAULT_PORT:FTP_DEFAULT_PORT;
   conn->control_send->Format("CONNECT %s:%s HTTP/1.0\r\n",hostname.get(),the_port);
   Log::global->Format(4,"+--> CONNECT %s:%s HTTP/1.0\n",hostname.get(),the_port);
   HttpProxySendAuth(conn->control_send);
   conn->control_send->Put("\r\n");
   http_proxy_status_code=0;
}
void Ftp::HttpProxySendConnectData()
{
   const char *the_host=SocketNumericAddress(&conn->data_sa);
   int the_port=SocketPort(&conn->data_sa);
   conn->data_iobuf->Format("CONNECT %s:%d HTTP/1.0\r\n",the_host,the_port);
   Log::global->Format(4,"+--> CONNECT %s:%d HTTP/1.0\n",the_host,the_port);
   HttpProxySendAuth(conn->data_iobuf);
   conn->data_iobuf->Put("\r\n");
   http_proxy_status_code=0;
}
// Check reply and return true when the reply is received and is ok.
bool Ftp::HttpProxyReplyCheck(const SMTaskRef<IOBuffer>& buf)
{
   const char *b;
   int s;
   buf->Get(&b,&s);
   const char *nl=b?(const char*)memchr(b,'\n',s):0;
   if(!nl)
   {
      if(buf->Error())
      {
	 LogError(0,"%s",buf->ErrorText());
	 if(buf->ErrorFatal())
	    SetError(FATAL,buf->ErrorText());
      }
      else if(buf->Eof())
	 LogError(0,_("Peer closed connection"));
      if(conn && (buf->Eof() || buf->Error()))
	 DisconnectNow();
      return false;
   }

   char *line=string_alloca(nl-b);
   memcpy(line,b,nl-b-1);	 // don't copy \r
   line[nl-b-1]=0;
   buf->Skip(nl-b+1);	 // skip \r\n too.

   Log::global->Format(4,"<--+ %s\n",line);

   if(!http_proxy_status_code)
   {
      if(1!=sscanf(line,"HTTP/%*d.%*d %d",&http_proxy_status_code)
      || !is2XX(http_proxy_status_code))
      {
	 // check for retriable codes
	 if(http_proxy_status_code==408 // Request Timeout
	 || http_proxy_status_code==502 // Bad Gateway
	 || http_proxy_status_code==503 // Service Unavailable
	 || http_proxy_status_code==504)// Gateway Timeout
	 {
	    DisconnectNow();
	    return false;
	 }
	 SetError(FATAL,line);
	 return false;
      }
   }
   if(!*line)
      return true;
   return false;
}

void Ftp::SendUrgentCmd(const char *cmd)
{
   if(!use_telnet_iac || !conn->telnet_layer_send)
   {
      conn->SendCmd(cmd);
      return;
   }

   static const char pre_cmd[]={TELNET_IAC,TELNET_IP,TELNET_IAC,TELNET_DM};

#if USE_SSL
   if(conn->ssl_is_activated())
   {
      // no way to send urgent data over ssl, send normally.
      conn->telnet_layer_send->Buffer::Put(pre_cmd,4);
   }
   else // note the following block
#endif
   {
      int fl=fcntl(conn->control_sock,F_GETFL);
      fcntl(conn->control_sock,F_SETFL,fl&~O_NONBLOCK);
      FlushSendQueue(/*all=*/true);
      if(!conn || !conn->control_send)
	 return;
      if(conn->control_send->Size()>0)
	 conn->control_send->Roll();
      // only DM byte is to be sent in urgent mode
      send(conn->control_sock,pre_cmd,3,0);
      send(conn->control_sock,pre_cmd+3,1,MSG_OOB);
      fcntl(conn->control_sock,F_SETFL,fl);
   }
   conn->SendCmd(cmd);
}

void  Ftp::DataAbort()
{
   if(!conn || state==CONNECTING_STATE || conn->quit_sent)
      return;

   if(conn->data_sock==-1 && copy_mode==COPY_NONE)
      return; // nothing to abort

   if(copy_mode!=COPY_NONE)
   {
      if(expect->IsEmpty())
	 return; // the transfer seems to be finished
      if(!copy_addr_valid)
	 return; // data connection cannot be established at this time
      if(!copy_connection_open && expect->FirstIs(Expect::TRANSFER))
      {
	 // wu-ftpd-2.6.0 cannot interrupt accept() or connect().
	 DisconnectNow();
	 return;
      }
   }
   copy_connection_open=false;

   // if transfer has been completed then ABOR is not needed
   if(conn->data_sock!=-1 && expect->IsEmpty())
      return;

   expect->Close();

   if(!QueryBool("use-abor",hostname)
   || expect->Count()>1 || conn->proxy_is_http)
   {
      // check that we have a data socket to close, and the server is not
      // in uninterruptible accept() state.
      if(copy_mode==COPY_NONE
      && !(GetFlag(PASSIVE_MODE) && state==DATASOCKET_CONNECTING_STATE
           && (pasv_state==PASV_NO_ADDRESS_YET || pasv_state==PASV_HAVE_ADDRESS)))
	 DataClose();	// just close data connection
      else
      {
	 // otherwise, just close control connection.
	 DisconnectNow();
      }
      return;
   }

   if(conn->aborted_data_sock!=-1)  // don't allow double ABOR.
   {
      DisconnectNow();
      return;
   }

   SendUrgentCmd("ABOR");
   expect->Push(Expect::ABOR);
   FlushSendQueue(true);
   conn->abor_close_timer.Reset();

   // don't close it now, wait for ABOR result
   conn->AbortDataConnection();

   // ABOR over SSL connection does not always work,
   // closing data socket should help it.
   if(conn->ssl_is_activated())
      conn->CloseAbortedDataConnection();

   if(QueryBool("web-mode"))
      Disconnect();
}

void Ftp::ControlClose()
{
   conn=0;
   expect=0;
}

void  Ftp::DisconnectNow()
{
   DataClose();
   ControlClose();
   state=INITIAL_STATE;
   http_proxy_status_code=0;

   if(copy_mode!=COPY_NONE)
   {
      if(copy_addr_valid)
	 copy_failed=true;
   }
   else
   {
      if(mode==STORE && GetFlag(IO_FLAG))
	 SetError(STORE_FAILED,0);
   }
   copy_addr_valid=false;
}

void  Ftp::Disconnect()
{
   if(!conn)
      return;

   if(conn->quit_sent)
   {
      DisconnectNow();
      return;
   }

   /* protect against re-entering from FlushSendQueue */
   static bool disconnect_in_progress=false;
   if(disconnect_in_progress)
      return;
   disconnect_in_progress=true;

   bool no_greeting=(!expect->IsEmpty() && expect->FirstIs(Expect::READY));

   expect->Close();
   DataAbort();
   DataClose();
   if(conn && state!=CONNECTING_STATE && state!=HTTP_PROXY_CONNECTED
   && expect->Count()<2 && QueryBool("use-quit",hostname))
   {
      conn->SendCmd("QUIT");
      expect->Push(Expect::IGNORE);
      conn->quit_sent=true;
      goto out;
   }
   ControlClose();

   if(state==CONNECTING_STATE || no_greeting)
      NextPeer();

   DisconnectNow();

out:
   disconnect_on_close=false;
   Timeout(0);

   disconnect_in_progress=false;
}

void Ftp::Connection::CloseDataSocket()
{
   if(data_sock==-1)
      return;
   LogNote(7,_("Closing data socket"));
   close(data_sock);
   data_sock=-1;
}

void Ftp::Connection::CloseDataConnection()
{
   data_iobuf=0;
   fixed_pasv=false;
   CloseDataSocket();
}
void Ftp::Connection::AbortDataConnection()
{
   CloseAbortedDataConnection();
   aborted_data_sock=data_sock;
   data_sock=-1;
   CloseDataConnection(); // clean up all other members.
}
void Ftp::Connection::CloseAbortedDataConnection()
{
   if(aborted_data_sock!=-1)
   {
      LogNote(9,_("Closing aborted data socket"));
      close(aborted_data_sock);
      aborted_data_sock=-1;
   }
}

void  Ftp::DataClose()
{
   rate_limit=0;
   if(!conn)
      return;
   conn->nop_time=0;
   conn->nop_offset=0;
   conn->nop_count=0;
   if(conn->data_sock!=-1 && QueryBool("web-mode"))
      disconnect_on_close=true;
   conn->CloseDataConnection();
   if(state==DATA_OPEN_STATE || state==DATASOCKET_CONNECTING_STATE)
      state=WAITING_STATE;
}

int Ftp::Connection::FlushSendQueueOneCmd()
{
   const char *send_cmd_ptr;
   int send_cmd_count;
   send_cmd_buffer.Get(&send_cmd_ptr,&send_cmd_count);

   if(send_cmd_count==0)
      return 0;

   const char *cmd_begin=send_cmd_ptr;
   const char *line_end=(const char*)memchr(send_cmd_ptr,'\n',send_cmd_count);
   if(!line_end)
      return 0;

   int to_write=line_end+1-send_cmd_ptr;
   control_send->Put(send_cmd_ptr,to_write);
   send_cmd_buffer.Skip(to_write);
   sync_wait++;

   int log_level=5;

   if(!may_show_password && !strncasecmp(cmd_begin,"PASS ",5))
      LogSend(log_level,"PASS XXXX");
   else
   {
      xstring log;
      for(const char *s=cmd_begin; s<=line_end; s++)
      {
	 if(*s==0)
	    log.append("<NUL>");
	 else if((unsigned char)*s==TELNET_IAC && telnet_layer_send)
	 {
	    s++;
	    if((unsigned char)*s==TELNET_IAC)
	       log.append('\377');
	    else if((unsigned char)*s==TELNET_IP)
	       log.append("<IP>");
	    else if((unsigned char)*s==TELNET_DM)
	       log.append("<DM>");
	 }
	 else
	   log.append(*s?*s:'!');
      }
      LogSend(log_level,log);
   }
   return 1;
}

int  Ftp::FlushSendQueue(bool all)
{
   int m=STALL;

   if(!conn || !conn->control_send)
      return m;

   if(conn->control_send->Error())
   {
      LogError(0,"%s",conn->control_send->ErrorText());
      if(conn->control_send->ErrorFatal())
      {
	 if(conn->ssl_is_activated() && !ftps && !QueryBool("ssl-force",hostname))
	    ResMgr::Set("ftp:ssl-allow",hostname,"no");
	 else
	    SetError(FATAL,conn->control_send->ErrorText());
      }
      DisconnectNow();
      return MOVED;
   }

   if(conn->send_cmd_buffer.Size()==0)
      return m;

   while(conn->sync_wait<=0 || all || !GetFlag(SYNC_MODE))
   {
      int res=conn->FlushSendQueueOneCmd();
      if(!res)
	 break;
      m|=MOVED;
   }

   if(m==MOVED)
      conn->control_send->Roll();
   timeout_timer.Reset(conn->control_send->EventTime());

   return m;
}

void  Ftp::Connection::Send(const char *buf)
{
   while(*buf)
   {
      char ch=*buf++;
      send_cmd_buffer.Put(&ch,1);
      if(ch=='\r')
	 send_cmd_buffer.PutRaw("",1); // RFC2640
   }
}
void  Ftp::Connection::SendEncoded(const char *buf)
{
   while(*buf)
   {
      char ch=*buf++;
      if(ch=='%' && isxdigit((unsigned char)buf[0]) && isxdigit((unsigned char)buf[1]))
      {
	 int n=0;
	 if(sscanf(buf,"%2x",&n)==1)
	 {
	    buf+=2;
	    ch=n;
	    // don't translate encoded bytes
	    send_cmd_buffer.PutRaw(&ch,1);
	    send_cmd_buffer.ResetTranslation();
	    goto next;
	 }
      }
      send_cmd_buffer.Put(&ch,1);
next: if(ch=='\r')
	 send_cmd_buffer.PutRaw("",1); // RFC2640
   }
}

void  Ftp::Connection::SendCmd(const char *cmd)
{
   Send(cmd);
   send_cmd_buffer.PutRaw("\r\n",2);
   send_cmd_buffer.ResetTranslation();
}

void Ftp::Connection::SendCmd2(const char *cmd,const char *f,const char *u,const char *home)
{
   if(cmd && cmd[0])
   {
      Send(cmd);
      send_cmd_buffer.Put(" ",1);
   }
   if(u)
   {
      if(u[0]=='/' && u[1]=='~')
	 u++;
      else if(!strncasecmp(u,"/%2F",4))
      {
	 Send("/");
	 u+=4;
      }
      else if(home && strcmp(home,"/"))
	 Send(home);
      SendEncoded(u);
   }
   else
      Send(f);
   send_cmd_buffer.PutRaw("\r\n",2);
   send_cmd_buffer.ResetTranslation();
}

void Ftp::Connection::SendCmd2(const char *cmd,int v)
{
   char buf[32];
   sprintf(buf,"%d",v);
   SendCmd2(cmd,buf);
}

void Ftp::Connection::SendCmdF(const char *f,...)
{
   va_list v;
   va_start(v,f);
   xstring_ca s(xvasprintf(f,v));
   va_end(v);
   SendCmd(s);
}

int   Ftp::SendEOT()
{
   if(mode!=STORE)
      return(OK); /* nothing to do */

   if(state!=DATA_OPEN_STATE)
      return(DO_AGAIN);

   if(!conn->data_iobuf->Eof())
      conn->data_iobuf->PutEOF();

   if(!conn->data_iobuf->Done())
      return(DO_AGAIN);

   DataClose();
   state=WAITING_STATE;
   return(OK);
}

void  Ftp::Close()
{
   if(mode!=CLOSED)
      idle_timer.Reset();

   flags&=~NOREST_MODE;	// can depend on a particular file
   eof=false;

   Resume();
   ExpandTildeInCWD();
   DataAbort();
   DataClose();
   if(conn)
   {
      expect->Close();
      switch(state)
      {
      case(CONNECTING_STATE):
      case(HTTP_PROXY_CONNECTED):
      case(CONNECTED_STATE):
      case(USER_RESP_WAITING_STATE):
	 Disconnect();
	 break;
      case(ACCEPTING_STATE):
      case(DATASOCKET_CONNECTING_STATE):
      case(CWD_CWD_WAITING_STATE):
      case(WAITING_STATE):
      case(DATA_OPEN_STATE):
      case(WAITING_150_STATE):
	 state=EOF_STATE;
	 break;
      case(INITIAL_STATE):
      case(EOF_STATE):
      case(WAITING_CCC_SHUTDOWN):
	 break;
      }
   }
   else
   {
      state=INITIAL_STATE;
   }
   copy_mode=COPY_NONE;
   copy_protect=false;
   copy_ssl_connect=false;
   copy_addr_valid=false;
   copy_done=false;
   copy_connection_open=false;
   copy_allow_store=false;
   copy_failed=false;
   super::Close();
   if(disconnect_on_close)
      Disconnect();
}

Ftp::ExpectQueue::ExpectQueue()
{
   first=0;
   last=&first;
   count=0;
}
Ftp::ExpectQueue::~ExpectQueue()
{
   while(first)
      delete Pop();
}
void Ftp::ExpectQueue::Push(Expect *e)
{
   *last=e;
   last=&e->next;
   e->next=0;
   count++;
}
void Ftp::ExpectQueue::Push(Expect::expect_t e)
{
   Push(new Expect(e));
}
Ftp::Expect *Ftp::ExpectQueue::Pop()
{
   if(!first)
      return 0;
   Expect *res=first;
   first=first->next;
   if(last==&res->next)
      last=&first;
   res->next=0;
   count--;
   return res;
}
bool Ftp::ExpectQueue::Has(Expect::expect_t cc)
{
   for(Expect *scan=first; scan; scan=scan->next)
      if(cc==scan->check_case)
	 return true;
   return false;
}
bool Ftp::ExpectQueue::FirstIs(Expect::expect_t cc)
{
   if(first && first->check_case==cc)
      return true;
   return false;
}
void Ftp::ExpectQueue::Close()
{
   for(Expect *scan=first; scan; scan=scan->next)
   {
      switch(scan->check_case)
      {
      case(Expect::IGNORE):
      case(Expect::PWD):
      case(Expect::USER):
      case(Expect::USER_PROXY):
      case(Expect::PASS):
      case(Expect::PASS_PROXY):
      case(Expect::OPEN_PROXY):
      case(Expect::ACCT_PROXY):
      case(Expect::READY):
      case(Expect::ABOR):
      case(Expect::CWD_STALE):
      case(Expect::PRET):
      case(Expect::PASV):
      case(Expect::EPSV):
      case(Expect::TRANSFER_CLOSED):
      case(Expect::FEAT):
      case(Expect::SITE_UTIME):
      case(Expect::SITE_UTIME2):
      case(Expect::TYPE):
      case(Expect::LANG):
      case(Expect::OPTS_UTF8):
      case(Expect::ALLO):
#if USE_SSL
      case(Expect::AUTH_TLS):
      case(Expect::PROT):
      case(Expect::SSCN):
      case(Expect::CCC):
#endif
	 break;
      case(Expect::CWD_CURR):
      case(Expect::CWD):
	 scan->check_case=Expect::CWD_STALE;
	 break;
      case(Expect::NONE):
      case(Expect::REST):
      case(Expect::SIZE):
      case(Expect::SIZE_OPT):
      case(Expect::MDTM):
      case(Expect::MDTM_OPT):
      case(Expect::PORT):
      case(Expect::FILE_ACCESS):
      case(Expect::RNFR):
      case(Expect::QUOTED):
	 scan->check_case=Expect::IGNORE;
	 break;
      case(Expect::TRANSFER):
	 scan->check_case=Expect::TRANSFER_CLOSED;
	 break;
      }
   }
}
Ftp::Expect *Ftp::ExpectQueue::FindLastCWD()
{
   for(Expect *scan=first; scan; scan=scan->next)
   {
      switch(scan->check_case)
      {
      case(Expect::CWD_CURR):
      case(Expect::CWD_STALE):
      case(Expect::CWD):
	 return scan;
      default:
	 ;
      }
   }
   return 0;
}

bool  Ftp::IOReady()
{
   if(copy_mode!=COPY_NONE && !copy_passive && !copy_addr_valid)
      return true;   // simulate to be ready as other fxp peer has to go
   return (state==DATA_OPEN_STATE || state==WAITING_STATE)
      && real_pos!=-1 && IsOpen();
}

int   Ftp::Read(void *buf,int size)
{
   int shift;

   Resume();
   if(Error())
      return(error_code);

   if(mode==CLOSED || eof)
      return(0);

   if(!conn || !conn->data_iobuf)
      return DO_AGAIN;

   if(expect->Has(Expect::REST) && real_pos==-1)
      return DO_AGAIN;

   if(state==DATASOCKET_CONNECTING_STATE)
      return DO_AGAIN;

   if(state==DATA_OPEN_STATE)
   {
      assert(rate_limit!=0);
      int allowed=rate_limit->BytesAllowedToGet();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(norest_manual && real_pos==0 && pos>0)
      return DO_AGAIN;

   const char *b;
   int s;
   conn->data_iobuf->Get(&b,&s);
   if(s==0)
      return DO_AGAIN;
   if(size>s)
      size=s;
   memcpy(buf,b,size);
   conn->data_iobuf->Skip(size);

   TrySuccess();
   assert(rate_limit!=0);
   rate_limit->BytesGot(size);
   real_pos+=size;
   if(real_pos<=pos)
      return DO_AGAIN;
   flags|=IO_FLAG;
   if((shift=pos+size-real_pos)>0)
   {
      memmove(buf,(char*)buf+shift,size-shift);
      size-=shift;
   }
   pos+=size;
   return(size);
}

/*
   Write - send data to ftp server

   * Uploading is not reliable in this realization *
   Well, not less reliable than in any usual ftp client.

   The reason for this is uncheckable receiving of data on the remote end.
   Since that, we have to leave re-putting up to caller.
   Fortunately, class FileCopy does it.
*/
int   Ftp::Write(const void *buf,int size)
{
   if(mode!=STORE)
      return(0);

   Resume();
   if(Error())
      return(error_code);

   if(!conn || state!=DATA_OPEN_STATE || (expect->Has(Expect::REST) && real_pos==-1))
      return DO_AGAIN;

   if(!conn->data_iobuf)
      return DO_AGAIN;

   {
      assert(rate_limit!=0);
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size>allowed)
	 size=allowed;
   }
   if(size+conn->data_iobuf->Size()>=max_buf)
      size=max_buf-conn->data_iobuf->Size();
   if(size<=0)
      return 0;

   conn->data_iobuf->Put((const char*)buf,size);

   if(retries+persist_retries>0
   && conn->data_iobuf->GetPos()-conn->data_iobuf->Size()>Buffered()+0x10000)
   {
      // reset retry count if some data were actually written to server.
      TrySuccess();
   }

   assert(rate_limit!=0);
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   flags|=IO_FLAG;
   return(size);
}

int   Ftp::StoreStatus()
{
   if(Error())
      return(error_code);

   if(mode!=STORE)
      return(OK);

   if(state==DATA_OPEN_STATE)
   {
      // have not send EOT by SendEOT, do it now
      SendEOT();
   }

   if(state==WAITING_STATE && expect->IsEmpty())
   {
      eof=true;
      return(OK);
   }

   return(IN_PROGRESS);
}

void  Ftp::MoveConnectionHere(Ftp *o)
{
   expect=o->expect.borrow();
   expect->Close(); // we need not handle other session's replies.

   assert(o->conn->data_iobuf==0);

   conn=o->conn.borrow();
   o->state=INITIAL_STATE;

   if(peer_curr>=peer.count())
      peer_curr=0;
   timeout_timer.Reset(o->timeout_timer);

   if(!home)
      set_home(home_auto);

   set_real_cwd(o->real_cwd);
   o->Disconnect();
   state=EOF_STATE;
}

void Ftp::SendOPTS_MLST()
{
   char *facts=alloca_strdup(conn->mlst_attr_supported);
   char *store=facts;
   bool differs=false;
   for(char *tok=strtok(facts,";"); tok; tok=strtok(0,";"))
   {
      bool was_enabled=false;
      bool want_enable=false;
      int len=strlen(tok);
      if(len>0 && tok[len-1]=='*')
      {
	 was_enabled=true;
	 tok[--len]=0;
      }
      // "unique" not needed yet.
      static const char *const needed[]={
	 "type","size","modify","perm",
	 "UNIX.mode","UNIX.owner","UNIX.uid","UNIX.group","UNIX.gid",
	 0};
      for(const char *const *scan=needed; *scan; scan++)
      {
	 if(!strcasecmp(tok,*scan))
	 {
	    memmove(store,tok,len);
	    store+=len;
	    *store++=';';
	    want_enable=true;
	    break;
	 }
      }
      differs|=(was_enabled^want_enable);
   }
   if(!differs || store==facts)
      return;
   *store=0;
   conn->SendCmd2("OPTS MLST",facts);
   expect->Push(Expect::IGNORE);
}

void Ftp::TuneConnectionAfterFEAT()
{
   if(conn->clnt_supported)
   {
      const char *client=Query("client",hostname);
      if(client && client[0])
      {
	 conn->SendCmd2("CLNT",client);
	 expect->Push(Expect::IGNORE);
      }
   }
   if(conn->lang_supported)
   {
      const char *lang_to_use=Query("lang",hostname);
      if(lang_to_use && lang_to_use[0])
	 conn->SendCmd2("LANG",lang_to_use);
      else
	 conn->SendCmd("LANG");
      expect->Push(Expect::LANG);
   }
   if(conn->utf8_supported)
   {
      // some non-RFC2640 servers require this command.
      conn->SendCmd("OPTS UTF8 ON");
      expect->Push(Expect::OPTS_UTF8);
   }
   if(conn->host_supported)
   {
      conn->SendCmd2("HOST",hostname);
      expect->Push(Expect::IGNORE);
   }
   if(conn->mlst_attr_supported)
      SendOPTS_MLST();
}

void Ftp::CheckFEAT(char *reply)
{
   if(QueryBool("trust-feat",hostname)) {
      // turn off these pre-FEAT extensions only when trusting FEAT reply,
      // as some servers forget to advertise them.
      conn->mdtm_supported=false;
      conn->size_supported=false;
      conn->rest_supported=false;
   }
#if USE_SSL
   conn->auth_supported=false;
   conn->auth_args_supported.set(0);
   conn->cpsv_supported=false;
   conn->sscn_supported=false;
#endif
   conn->pret_supported=false;
   conn->epsv_supported=false;

   char *scan=strchr(reply,'\n');
   if(scan)
      scan++;
   if(!scan || !*scan)
      return;

   for(char *f=strtok(scan,"\r\n"); f; f=strtok(0,"\r\n"))
   {
      if(!strncmp(f,line,3))
      {
	 if(f[3]==' ')
	    break;   // last line
	 if(f[3]=='-')
	    f+=4;    // workaround for broken servers, RFC2389 does not allow it.
      }
      while(*f==' ')
	 f++;

      if(!strcasecmp(f,"UTF8"))
	 conn->utf8_supported=true;
      else if(!strncasecmp(f,"LANG ",5))
	 conn->lang_supported=true;
      else if(!strcasecmp(f,"PRET"))
	 conn->pret_supported=true;
      else if(!strcasecmp(f,"MDTM"))
	 conn->mdtm_supported=true;
      else if(!strcasecmp(f,"SIZE"))
	 conn->size_supported=true;
      else if(!strcasecmp(f,"CLNT") || !strncasecmp(f,"CLNT ",5))
	 conn->clnt_supported=true;
      else if(!strcasecmp(f,"HOST"))
	 conn->host_supported=true;
      else if(!strcasecmp(f,"MFMT"))
	 conn->mfmt_supported=true;
      else if(!strncasecmp(f,"REST ",5)) // FIXME: actually REST STREAM
	 conn->rest_supported=true;
      else if(!strcasecmp(f,"REST"))
	 conn->rest_supported=true;
      else if(!strncasecmp(f,"MLST ",5))
      {
	 conn->mlst_supported=true;
	 conn->mlst_attr_supported.set(f+5);
      }
      else if(!strcasecmp(f,"EPSV"))
	 conn->epsv_supported=true;
#if USE_SSL
      else if(!strncasecmp(f,"AUTH ",5))
      {
	 conn->auth_supported=true;
	 if(conn->auth_args_supported)
	    conn->auth_args_supported.vappend(";",f+5,NULL);
	 else
	    conn->auth_args_supported.append(f+5);
      }
      else if(!strcasecmp(f,"AUTH"))
	 conn->auth_supported=true;
      else if(!strcasecmp(f,"CPSV"))
	 conn->cpsv_supported=true;
      else if(!strcasecmp(f,"SSCN"))
	 conn->sscn_supported=true;
#endif // USE_SSL
   }
   conn->have_feat_info=true;
}

void Ftp::TurnOffStatForList()
{
   DataClose();
   expect->Close();
   state=EOF_STATE;
   LogNote(2,"Setting ftp:use-stat-for-list to off");
   ResMgr::Set("ftp:use-stat-for-list",hostname,"off");
   use_stat_for_list=false;
}

void Ftp::CheckResp(int act)
{
   // close aborted accepting data socket when the connection is established
   if(is1XX(act) && GetFlag(PASSIVE_MODE) && conn->aborted_data_sock!=-1)
      conn->CloseAbortedDataConnection();

   if(is1XX(act) && expect->FirstIs(Expect::TRANSFER))
   {
      // allow data transfer
      conn->received_150=true;

      if(state==WAITING_STATE)
      {
	 // set the FXP flag
	 copy_connection_open=true;
	 conn->stat_timer.ResetDelayed(2);
      }

      if(mode==RETRIEVE && entity_size<0)
      {
	 // try to catch size
	 const char *s=strrchr(line,'(');
	 if(s && is_ascii_digit(s[1]))
	 {
	    long long size_ll;
	    if(1==sscanf(s+1,"%lld",&size_ll))
	    {
	       entity_size=size_ll;
	       if(opt_size)
		  *opt_size=entity_size;
	       LogNote(7,_("saw file size in response"));
	    }
	 }
      }
   }

   if(is1XX(act)) // intermediate responses are ignored
      return;

   if(act==421)	  // server is going to disconnect, don't try sending QUIT.
      conn->quit_sent=true;

   Expect *exp=expect->Pop();
   if(!exp)
   {
      if(act!=421)
	 LogError(3,_("extra server response"));
      if(is2XX(act)) // some buggy servers send several 226 replies
	 return;
      Disconnect();
      return;
   }

   Expect::expect_t cc=exp->check_case;
   const char *arg=exp->arg;

   // some servers mess all up
   if(act==331 && cc==Expect::READY && !GetFlag(SYNC_MODE) && expect->Count()>1)
   {
      delete expect->Pop();
      LogNote(2,_("Turning on sync-mode"));
      ResMgr::Set("ftp:sync-mode",hostname,"on");
      Disconnect();
      try_time=0; // retry immediately
      goto leave;
   }

   switch(cc)
   {
   case Expect::NONE:
      if(is2XX(act)) // default rule.
	 break;
      Disconnect();
      break;

   case Expect::QUOTED:
      if(mode==LONG_LIST && !is2XX(act) && use_stat_for_list)
	 TurnOffStatForList();
      break;
   case Expect::IGNORE:
   ignore:
      break;

   case Expect::READY:
   case Expect::OPEN_PROXY:
      if(!GetFlag(SYNC_MODE) && re_match(all_lines,Query("auto-sync-mode",hostname)))
      {
	 LogNote(2,_("Turning on sync-mode"));
	 ResMgr::Set("ftp:sync-mode",hostname,"on");
	 assert(GetFlag(SYNC_MODE));
	 Disconnect();
	 try_time=0; // retry immediately
      }
      if(!is2XX(act))
      {
	 Disconnect();
	 if(cc==Expect::OPEN_PROXY && act==403)
	 {
	    SetError(LOGIN_FAILED,all_lines);
	    break;
	 }
	 NextPeer();
	 if(peer_curr==0)
	    try_time=now;  // count the reconnect-interval from this moment
	 last_connection_failed=true;
      }
      break;

   case Expect::REST:
      RestCheck(act);
      break;

   case Expect::CWD:
   case Expect::CWD_CURR:
      if(is2XX(act))
      {
	 if(cc==Expect::CWD)
	    cwd.Set(arg,false,0,device_prefix_len(arg));
	 set_real_cwd(arg);
	 cache->SetDirectory(this, arg, true);
	 break;
      }
      if(is5XX(act))
      {
	 SetError(NO_FILE,all_lines);
	 cache->SetDirectory(this, arg, false);
	 break;
      }
      Disconnect();
      break;

   case Expect::CWD_STALE:
      if(is2XX(act))
	 set_real_cwd(arg);
      goto ignore;

   case Expect::ABOR:
      conn->CloseAbortedDataConnection();
      goto ignore;

   case Expect::SIZE:
      CatchSIZE(act);
      break;
   case Expect::SIZE_OPT:
      CatchSIZE_opt(act);
      break;
   case Expect::MDTM:
      CatchDATE(act);
      break;
   case Expect::MDTM_OPT:
      CatchDATE_opt(act);
      break;

   case Expect::FILE_ACCESS:
   file_access:
      if(mode==CHANGE_MODE && site_cmd_unsupported(act))
      {
	 conn->site_chmod_supported=false;
	 SetError(NO_FILE,all_lines);
	 break;
      }
      NoFileCheck(act);
      break;

   case Expect::PRET:
      if(cmd_unsupported(act))
      {
	 conn->pret_supported=false;
	 break;
      }
      if(is5XX(act))
	 SetError(NO_FILE,all_lines);
      break;

   case Expect::ALLO:
      if(cmd_unsupported(act) || act==202)
	 ResMgr::Set("ftp:use-allo",hostname,"no");
      break;

   case Expect::PASV:
   case Expect::EPSV:
      if(is2XX(act))
      {
	 if(line.length()<=4)
	    goto passive_off;

	 memset(&conn->data_sa,0,sizeof(conn->data_sa));

	 if(cc==Expect::PASV)
	    pasv_state=Handle_PASV();
	 else // cc==Expect::EPSV
	    pasv_state=Handle_EPSV();

	 if(pasv_state==PASV_NO_ADDRESS_YET)
	    goto passive_off;

	 if(conn->aborted_data_sock!=-1)
	    SocketConnect(conn->aborted_data_sock,&conn->data_sa);

      	 break;
      }
      if(copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 copy_failed=true;
	 break;
      }
      if(is5XX(act))
      {
      passive_off:
	 if(QueryBool("auto-passive-mode",hostname))
	 {
	    LogNote(2,_("Switching passive mode off"));
	    SetFlag(PASSIVE_MODE,0);
	 }
      }
      Disconnect();
      break;

   case Expect::PORT:
      if(is2XX(act))
	 break;
      if(copy_mode!=COPY_NONE)
      {
	 copy_passive=!copy_passive;
	 copy_failed=true;
	 break;
      }
      if(is5XX(act))
      {
	 if(QueryBool("auto-passive-mode",hostname))
	 {
	    LogNote(2,_("Switching passive mode on"));
	    SetFlag(PASSIVE_MODE,1);
	 }
      }
      Disconnect();
      break;

   case Expect::PWD:
      if(is2XX(act))
      {
	 if(!home_auto)
	 {
	    home_auto.set_allocated(ExtractPWD());
	    PropagateHomeAuto();
	 }
	 if(!home)
	    set_home(home_auto);
	 cache->SetDirectory(this, home, true);
	 break;
      }
      break;

   case Expect::RNFR:
      if(is3XX(act))
      {
	 conn->SendCmd2("RNTO",file1);
	 expect->Push(Expect::FILE_ACCESS);
      	 break;
      }
      goto file_access;

   case Expect::USER_PROXY:
      proxy_NoPassReqCheck(act);
      break;
   case Expect::USER:
      NoPassReqCheck(act);
      break;
   case Expect::PASS_PROXY:
   case Expect::ACCT_PROXY:
      proxy_LoginCheck(act);
      break;
   case Expect::PASS:
      LoginCheck(act);
      break;

   case Expect::TRANSFER:
      TransferCheck(act);
      if(mode==STORE && is2XX(act)
      && entity_size==real_pos)
	 SendUTimeRequest();
      break;

   case Expect::TRANSFER_CLOSED:
      if(strstr(line,"ABOR")
      && expect->Count()>=2 && expect->FirstIs(Expect::ABOR))
      {
	 LogError(1,"server bug: 426 reply missed");
	 delete expect->Pop();
	 conn->CloseAbortedDataConnection();
      }
      break;
   case Expect::FEAT:
      if(is2XX(act))
      {
	 CheckFEAT(all_lines.get_non_const());
	 if(conn->try_feat_after_login && conn->have_feat_info)
	    TuneConnectionAfterFEAT();
      }
      else if(act==530 || act==503)
	 conn->try_feat_after_login=true;
      break;

   case Expect::SITE_UTIME:
      if(site_cmd_unsupported(act))
      {
	 conn->site_utime_supported=false;
	 SendUTimeRequest();  // try another method.
      }
      break;
   case Expect::SITE_UTIME2:
      if(site_cmd_unsupported(act))
      {
	 conn->site_utime2_supported=false;
	 SendUTimeRequest();  // try another method.
      }
      break;
   case Expect::TYPE:
      if(is2XX(act))
	 conn->type=arg[0];
      break;
   case Expect::OPTS_UTF8:
   case Expect::LANG:
      if(is2XX(act))
      {
	 conn->utf8_activated=true;
	 conn->SetControlConnectionTranslation("UTF-8");
      }
      else if(act==530)
	 conn->tune_after_login=true;
      break;

#if USE_SSL
   case Expect::AUTH_TLS:
      if(is2XX(act) || is3XX(act))
      {
	 conn->MakeSSLBuffers(hostname);
      }
      else
      {
	 if(QueryBool("ssl-force",hostname))
	    SetError(LOGIN_FAILED,_("ftp:ssl-force is set and server does not support or allow SSL"));
	 conn->prot='C';
	 conn->auth_supported=false;
      }
      break;
   case Expect::PROT:
      if(is2XX(act))
	 conn->prot=arg[0];
      else if(!conn->prot)
	 conn->prot=(ftps?'P':'C');
      break;
   case Expect::SSCN:
      if(is2XX(act))
	 conn->sscn_on=(arg[0]=='Y');
      else if(cmd_unsupported(act))
	 conn->sscn_supported=false;
      break;
   case Expect::CCC:
      if(is2XX(act))
      {
	 state=WAITING_CCC_SHUTDOWN;
	 conn->waiting_ssl_timer.Reset();
      }
      break;
#endif // USE_SSL

   } /* end switch */
leave:
   delete exp;
}

const char *Ftp::CurrentStatus()
{
   if(Error())
      return StrError(error_code);
   if(expect && expect->Has(Expect::FEAT))
      return _("FEAT negotiation...");
   switch(state)
   {
   case(EOF_STATE):
      if(conn && conn->control_sock!=-1)
      {
	 if(conn->send_cmd_buffer.Size()>0)
	    return(_("Sending commands..."));
	 if(!expect->IsEmpty())
	    return(_("Waiting for response..."));
	 if(!retry_timer.Stopped())
	    return _("Delaying before retry");
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
   case(HTTP_PROXY_CONNECTED):
      return(_("Connecting..."));
   case(CONNECTED_STATE):
#if USE_SSL
      if(conn->auth_sent)
	 return _("TLS negotiation...");
#endif
      return _("Connected");
   case(USER_RESP_WAITING_STATE):
      return(_("Logging in..."));
   case(DATASOCKET_CONNECTING_STATE):
      if(pasv_state==PASV_NO_ADDRESS_YET)
	 return(_("Waiting for response..."));
      return(_("Making data connection..."));
   case(CWD_CWD_WAITING_STATE):
      if(expect->FindLastCWD())
	 return(_("Changing remote directory..."));
      /*fallthrough*/
   case(WAITING_STATE):
      if(copy_mode==COPY_SOURCE)
	 return "";
      if(copy_mode==COPY_DEST && expect->IsEmpty())
	 return _("Waiting for other copy peer...");
      if(mode==STORE)
	 return(_("Waiting for transfer to complete"));
   case(WAITING_150_STATE):
      return(_("Waiting for response..."));
   case(WAITING_CCC_SHUTDOWN):
      return(_("Waiting for TLS shutdown..."));
   case(ACCEPTING_STATE):
      return(_("Waiting for data connection..."));
   case(DATA_OPEN_STATE):
#if USE_SSL
      if(conn->prot=='P')
      {
	 if(mode==STORE)
	    return(_("Sending data/TLS"));
         else
	    return(_("Receiving data/TLS"));
      }
#endif
      if(conn->data_sock!=-1)
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

bool  Ftp::SameSiteAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   Ftp *o=(Ftp*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && ftps==o->ftps);
}

bool  Ftp::SameConnection(const Ftp *o) const
{
   if(!strcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass)
   && (user || !xstrcmp(anon_user,o->anon_user))
   && (pass || !xstrcmp(anon_pass,o->anon_pass))
   && ftps==o->ftps)
      return true;
   return false;
}

bool  Ftp::SameLocationAs(const FileAccess *fa) const
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
      if(state==WAITING_STATE && expect->IsEmpty() && array_ptr==array_cnt)
	 return(OK);
      return(IN_PROGRESS);
   }

   if(copy_mode==COPY_DEST && !copy_allow_store)
      return(IN_PROGRESS);

   if(mode==CHANGE_DIR || mode==RENAME
   || mode==MAKE_DIR || mode==REMOVE_DIR || mode==REMOVE || mode==CHANGE_MODE
   || copy_mode!=COPY_NONE)
   {
      if(state==WAITING_STATE && expect->IsEmpty())
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
   home_auto.set(FindHomeAuto());
   Reconfig();
   state=INITIAL_STATE;
}

bool Ftp::AnonymousQuietMode()
{
   if(user && user.ne("anonymous") && user.ne("ftp"))
      return false;
   const char *pass_to_use=(pass?pass:anon_pass);
   return pass_to_use && *pass_to_use=='-';  // minus sign in password means quiet mode
}

void Ftp::Reconfig(const char *name)
{
   closure.set(hostname);
   super::Reconfig(name);

   if(!xstrcmp(name,"net:idle") || !xstrcmp(name,"ftp:use-site-idle"))
   {
      if(conn && conn->data_sock==-1 && state==EOF_STATE && !conn->quit_sent)
	 SendSiteIdle();
      return;
   }

   SetFlag(SYNC_MODE,	QueryBool("sync-mode"));
   if(!conn || !conn->proxy_is_http)
      SetFlag(PASSIVE_MODE,QueryBool("passive-mode"));
   rest_list = QueryBool("rest-list");

   nop_interval = Query("nop-interval").to_number(1,30);

   allow_skey = QueryBool("skey-allow");
   force_skey = QueryBool("skey-force");
   verify_data_address = QueryBool("verify-address");
   verify_data_port = QueryBool("verify-port");

   use_stat = QueryBool("use-stat");
   use_stat_for_list=QueryBool("use-stat-for-list") && !AnonymousQuietMode();
   use_mdtm = QueryBool("use-mdtm");
   use_size = QueryBool("use-size");
   use_pret = QueryBool("use-pret");
   use_feat = QueryBool("use-feat");
   use_mlsd = QueryBool("use-mlsd");

   use_telnet_iac = QueryBool("use-telnet-iac");

   anon_user.set(Query("anon-user"));
   anon_pass.set(Query("anon-pass"));

   if(!name || !xstrcmp(name,"ftp:list-options"))
   {
      if(name && !IsSuspended())
	 cache->TreeChanged(this,"/");
      list_options.set(Query("list-options"));
   }

   if(!name || !xstrcmp(name,"ftp:charset"))
   {
      if(name && !IsSuspended())
	 cache->TreeChanged(this,"/");
      charset.set(Query("charset"));
      if(conn && conn->have_feat_info && !conn->utf8_activated
      && !(expect->Has(Expect::LANG) || expect->Has(Expect::OPTS_UTF8))
      && charset && *charset)
	 conn->SetControlConnectionTranslation(charset);
   }

   const char *h=QueryStringWithUserAtHost("home");
   if(h && h[0] && AbsolutePath(h))
      set_home(h);
   else
      set_home(home_auto);

   if(NoProxy(hostname))
      SetProxy(0);
   else
      SetProxy(Query("proxy"));

   if(proxy && proxy_port==0)
   {
      if(ProxyIsHttp())
	 proxy_port.set(HTTP_DEFAULT_PROXY_PORT);
      else
	 proxy_port.set(FTP_DEFAULT_PORT);
   }

   if(conn && conn->control_sock!=-1)
      SetSocketBuffer(conn->control_sock);
   if(conn && conn->data_sock!=-1)
      SetSocketBuffer(conn->data_sock);
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
   if(!conn || mode!=CLOSED)
      return;
   Disconnect();
}

void Ftp::SetError(int ec,const char *e)
{
   // join multiline error message into single line, removing `550-' prefix.
   if(e && strchr(e,'\n'))
   {
      char *joined=string_alloca(strlen(e)+1);
      const char *prefix=e;
      char *store=joined;
      while(*e)
      {
	 if(*e=='\n')
	 {
	    if(e[1])
	       *store++=' ';
	    e++;
	    if(!strncmp(e,prefix,3) && (e[3]=='-' || e[3]==' '))
	       e+=4;
	 }
	 else
	 {
	    *store++=*e++;
	 }
      }
      *store=0;
      e=joined;
   }
   super::SetError(ec,e);

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
}

Ftp::ConnectLevel Ftp::GetConnectLevel() const
{
   if(!conn)
      return CL_NOT_CONNECTED;
   if(state==CONNECTING_STATE || state==HTTP_PROXY_CONNECTED)
      return CL_CONNECTING;
   if(state==CONNECTED_STATE)
      return CL_JUST_CONNECTED;
   if(state==USER_RESP_WAITING_STATE)
      return CL_NOT_LOGGED_IN;
   if(conn->quit_sent)
      return CL_JUST_BEFORE_DISCONNECT;
   return CL_LOGGED_IN;
}

ListInfo *Ftp::MakeListInfo(const char *path)
{
   return new FtpListInfo(this,path);
}
Glob *Ftp::MakeGlob(const char *pattern)
{
   return new GenericGlob(this,pattern);
}
DirList *Ftp::MakeDirList(ArgV *args)
{
   return new FtpDirList(this,args);
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
      cp=strstr(all_lines,skey_head[i]);
      if(cp)
      {
	 cp+=strlen(skey_head[i]);
	 break;
      }
   }

   LogNote(9,"found s/key substring");

   int skey_sequence=0;
   char *buf=(char*)alloca(strlen(cp));

   if(sscanf(cp,"%d %s",&skey_sequence,buf)!=2 || skey_sequence<1)
      return 0;

   return calculate_skey_response(skey_sequence,buf,pass);
}

int Ftp::Buffered()
{
   if(!conn || !conn->data_iobuf)
      return 0;
   if(state!=DATA_OPEN_STATE || conn->data_sock==-1 || mode!=STORE)
      return 0;
   return conn->data_iobuf->Size()+SocketBuffered(conn->data_sock);
}

const char *Ftp::ProtocolSubstitution(const char *host)
{
   if(NoProxy(host))
      return 0;
   const char *proxy=ResMgr::Query("ftp:proxy",host);
   if(proxy && QueryBool("use-hftp",host)
   && (!strncmp(proxy,"http://",7) || !strncmp(proxy,"https://",8)))
      return "hftp";
   return 0;
}


#if USE_SSL
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

   Reconfig();
}
FileAccess *FtpS::New() { return new FtpS(); }

void Ftp::Connection::MakeSSLBuffers(const char *hostname)
{
   control_ssl=new lftp_ssl(control_sock,lftp_ssl::CLIENT,hostname);
   control_ssl->load_keys();
   IOBufferSSL *send_ssl=new IOBufferSSL(control_ssl,IOBufferSSL::PUT);
   IOBufferSSL *recv_ssl=new IOBufferSSL(control_ssl,IOBufferSSL::GET);

   control_send=send_ssl;
   control_recv=recv_ssl;
   telnet_layer_send=0;
}
#endif

void TelnetEncode::PutTranslated(Buffer *target,const char *put_buf,int size)
{
   size_t put_size=size;
   const char *iac;
   while(put_size>0)
   {
      iac=(const char*)memchr(put_buf,TELNET_IAC,put_size);
      if(!iac)
      {
	 target->Put(put_buf,put_size);
	 break;
      }
      target->Put(put_buf,iac+1-put_buf);
      put_size-=iac+1-put_buf;
      put_buf=iac+1;
      // double the IAC to send it literally.
      target->Put(iac,1);
   }
}
void TelnetDecode::PutTranslated(Buffer *target,const char *put_buf,int size)
{
   if(Size()>0)
   {
      Put(put_buf,size);
      Get(&put_buf,&size);
   }
   if(size<=0)
      return;
   size_t put_size=size;
   const char *iac;
   while(put_size>0)
   {
      iac=(const char*)memchr(put_buf,TELNET_IAC,put_size);
      if(!iac)
	 break;
      target->Put(put_buf,iac-put_buf);
      Skip(iac-put_buf);
      put_size-=iac-put_buf;
      put_buf=iac;
      if(put_size<2)
      {
	 if(Size()==0)
	    Put(put_buf,put_size); // remember incomplete sequence
	 return;
      }
      switch((unsigned char)iac[1])
      {
      // 3-byte commands
      case TELNET_WILL:
      case TELNET_WONT:
      case TELNET_DO:
      case TELNET_DONT:
	 if(put_size<3)
	 {
	    if(Size()==0)
	       Put(put_buf,put_size); // remember incomplete sequence
	    return;
	 }
	 Skip(3);
	 put_buf+=3;
	 put_size-=3;
	 break;
      // 2-byte commands
      case TELNET_IAC:
	 target->Put(iac,1);
	 /*fallthrough*/
      default:
	 Skip(2);
	 put_buf+=2;
	 put_size-=2;
      }
   }
   if(put_size>0)
   {
      target->Put(put_buf,put_size);
      Skip(put_size);
   }
}

void Ftp::Connection::SetControlConnectionTranslation(const char *cs)
{
   if(translation_activated)
      return;
   if(telnet_layer_send)
   {
      // cannot do two conversions in one DirectedBuffer, stack it.
      control_recv=new IOBufferStacked(control_recv.borrow());
   }
   send_cmd_buffer.SetTranslation(cs,false);
   control_recv->SetTranslation(cs,true);
   translation_activated=true;
}

#include "modconfig.h"
#ifdef MODULE_PROTO_FTP
void module_init()
{
   Ftp::ClassInit();
}
#endif
