/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2020 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include <sys/types.h>
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netinet/in.h>
#ifdef HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
#ifdef HAVE_NETINET_IP_H
# include <netinet/ip.h>
#endif
#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#include "SMTask.h"
#include "network.h"
#include "ResMgr.h"
#include "ProtoLog.h"
#include "xstring.h"

const char *sockaddr_u::address() const
{
#ifdef HAVE_GETNAMEINFO
   static char buf[NI_MAXHOST];
   if(getnameinfo(&sa,addr_len(),buf,NI_MAXHOST,0,0,NI_NUMERICHOST)<0)
      return "????";
   return buf;
#else
   static char buf[16];
   if(sa.sa_family!=AF_INET)
      return "????";
   unsigned char *a=(unsigned char *)&in.sin_addr;
   snprintf(buf,16,"%u.%u.%u.%u",a[0],a[1],a[2],a[3]);
   return buf;
#endif
}
int sockaddr_u::port() const
{
   if(sa.sa_family==AF_INET)
      return ntohs(in.sin_port);
#if INET6
   if(sa.sa_family==AF_INET6)
      return ntohs(in6.sin6_port);
#endif
   return 0;
}
void sockaddr_u::set_port(int port)
{
   if(sa.sa_family==AF_INET)
      in.sin_port=htons(port);
#if INET6
   if(sa.sa_family==AF_INET6)
      in6.sin6_port=htons(port);
#endif
}

const xstring& sockaddr_u::to_xstring() const
{
   return xstring::format("[%s]:%d",address(),port());
}

bool sockaddr_u::is_reserved() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]==0)
	  || (a[0]==127 && !is_loopback())
	  || (a[0]>=240);
   }
#if INET6
   if(family()==AF_INET6) {
      return IN6_IS_ADDR_UNSPECIFIED(&in6.sin6_addr)
	  || IN6_IS_ADDR_V4MAPPED(&in6.sin6_addr)
	  || IN6_IS_ADDR_V4COMPAT(&in6.sin6_addr);
   }
#endif
   return false;
}

bool sockaddr_u::is_multicast() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]>=224 && a[0]<240);
   }
#if INET6
   if(family()==AF_INET6)
      return IN6_IS_ADDR_MULTICAST(&in6.sin6_addr);
#endif
   return false;
}

bool sockaddr_u::is_private() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]==10)
	  || (a[0]==172 && a[1]>=16 && a[1]<32)
	  || (a[0]==192 && a[1]==168)
	  || (a[0]==169 && a[1]==254); // self-assigned
   }
#if INET6
   if(family()==AF_INET6) {
      return IN6_IS_ADDR_SITELOCAL(&in6.sin6_addr)
	  || IN6_IS_ADDR_LINKLOCAL(&in6.sin6_addr);
   }
#endif
   return false;
}
bool sockaddr_u::is_loopback() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]==127 && a[1]==0 && a[2]==0 && a[3]==1);
   }
#if INET6
   if(sa.sa_family==AF_INET6)
      return IN6_IS_ADDR_LOOPBACK(&in6.sin6_addr);
#endif
   return false;
}
bool sockaddr_u::is_compatible(const sockaddr_u& o) const
{
   return family()==o.family()
      && !is_multicast() && !o.is_multicast()
      && !is_reserved() && !o.is_reserved()
      && is_private()==o.is_private()
      && is_loopback()==o.is_loopback();
}

bool sockaddr_u::set_compact(const char *c,size_t len)
{
   if(len==4) {
      sa.sa_family=AF_INET;
      memcpy(&in.sin_addr,c,4);
      in.sin_port=0;
      return true;
#if INET6
   } else if(len==16) {
      sa.sa_family=AF_INET6;
      memcpy(&in6.sin6_addr,c,16);
      return true;
#endif
   } else if(len==6) {
      sa.sa_family=AF_INET;
      memcpy(&in.sin_addr,c,4);
      in.sin_port=htons((c[5]&255)|((c[4]&255)<<8));
      return true;
#if INET6
   } else if(len==18) {
      sa.sa_family=AF_INET6;
      memcpy(&in6.sin6_addr,c,16);
      in6.sin6_port=htons((c[17]&255)|((c[16]&255)<<8));
      return true;
#endif
   }
   return false;
}
const sockaddr_compact& sockaddr_u::compact() const
{
   sockaddr_compact& c=compact_addr();
   int p=port();
   if(c.length() && p) {
      c.append(char(p>>8));
      c.append(char(p&255));
   }
   return c;
}
sockaddr_compact& sockaddr_u::compact_addr() const
{
   sockaddr_compact& c=sockaddr_compact::get_tmp();
   if(family()==AF_INET)
      c.append((const char*)&in.sin_addr,4);
#if INET6
   else if(family()==AF_INET6)
      c.append((const char*)&in6.sin6_addr,16);
#endif
   return c;
}

void Networker::NonBlock(int fd)
{
   int fl=fcntl(fd,F_GETFL);
   fcntl(fd,F_SETFL,fl|O_NONBLOCK);
}
void Networker::CloseOnExec(int fd)
{
   fcntl(fd,F_SETFD,FD_CLOEXEC);
}

static int one=1;
void Networker::KeepAlive(int sock)
{
   setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&one,sizeof(one));
}
void Networker::MinimizeLatency(int sock)
{
#ifdef IP_TOS
   int tos = IPTOS_LOWDELAY;
   setsockopt(sock, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int));
#endif
}
void Networker::MaximizeThroughput(int sock)
{
#ifdef IP_TOS
   int tos = IPTOS_THROUGHPUT;
   setsockopt(sock, IPPROTO_IP, IP_TOS, (char *)&tos, sizeof(int));
#endif
}
void Networker::ReuseAddress(int sock)
{
   setsockopt(sock,SOL_SOCKET,SO_REUSEADDR,(char*)&one,sizeof(one));
}
void Networker::SetSocketBuffer(int sock,int socket_buffer)
{
   if(socket_buffer==0)
      return;
   if(-1==setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      ProtoLog::LogError(1,"setsockopt(SO_SNDBUF,%d): %s",socket_buffer,strerror(errno));
   if(-1==setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      ProtoLog::LogError(1,"setsockopt(SO_RCVBUF,%d): %s",socket_buffer,strerror(errno));
}
void Networker::SetSocketMaxseg(int sock,int socket_maxseg)
{
#ifndef SOL_TCP
# define SOL_TCP IPPROTO_TCP
#endif
#ifdef TCP_MAXSEG
   if(socket_maxseg==0)
      return;
   if(-1==setsockopt(sock,SOL_TCP,TCP_MAXSEG,(char*)&socket_maxseg,sizeof(socket_maxseg)))
      ProtoLog::LogError(1,"setsockopt(TCP_MAXSEG,%d): %s",socket_maxseg,strerror(errno));
#endif
}

int Networker::SocketCreateUnbound(int af,int type,int proto,const char *hostname)
{
   int s=socket(af,type,proto);
   if(s<0)
      return s;

   NonBlock(s);
   CloseOnExec(s);
   SetSocketBuffer(s,ResMgr::Query("net:socket-buffer",hostname));
   return s;
}
bool sockaddr_u::set_defaults(int af,const char *hostname,int port)
{
   memset(this,0,sizeof(*this));
   sa.sa_family=af;
   const char *b=0;
   if(af==AF_INET)
   {
      b=ResMgr::Query("net:socket-bind-ipv4",hostname);
      if(!(b && b[0] && inet_pton(af,b,&in.sin_addr)))
	 b=0;
      in.sin_port=htons(port);
   }
#if INET6
   else if(af==AF_INET6)
   {
      b=ResMgr::Query("net:socket-bind-ipv6",hostname);
      if(!(b && b[0] && inet_pton(af,b,&in6.sin6_addr)))
	 b=0;
      in6.sin6_port=htons(port);
   }
#endif
   return b || port;
}
void Networker::SocketBindStd(int s,int af,const char *hostname,int port)
{
   sockaddr_u bind_addr;
   if(bind_addr.set_defaults(af,hostname,port))
   {
      if(bind_addr.bind_to(s)==-1)
	 ProtoLog::LogError(0,"bind(%s): %s",bind_addr.to_string(),strerror(errno));
   }
}
int Networker::SocketCreate(int af,int type,int proto,const char *hostname)
{
   int s=SocketCreateUnbound(af,type,proto,hostname);
   if(s<0)
      return s;
   SocketBindStd(s,af,hostname);
   return s;
}
void Networker::SocketTuneTCP(int s,const char *hostname)
{
   KeepAlive(s);
   SetSocketMaxseg(s,ResMgr::Query("net:socket-maxseg",hostname));
}
int Networker::SocketCreateTCP(int af,const char *hostname)
{
   int s=SocketCreate(af,SOCK_STREAM,IPPROTO_TCP,hostname);
   if(s<0)
      return s;
   SocketTuneTCP(s,hostname);
   return s;
}
int Networker::SocketCreateUnboundTCP(int af,const char *hostname)
{
   int s=SocketCreateUnbound(af,SOCK_STREAM,IPPROTO_TCP,hostname);
   if(s<0)
      return s;
   SocketTuneTCP(s,hostname);
   return s;
}
int Networker::SocketConnect(int fd,const sockaddr_u *u)
{
   // some systems have wrong connect() prototype, so we have to cast off const.
   // in any case, connect does not alter the address.
   int res=connect(fd,(sockaddr*)&u->sa,SocketAddrLen(u));
   if(res!=-1)
      SMTask::UpdateNow(); // if non-blocking doesn't work
   return res;
}
int Networker::SocketAccept(int fd,sockaddr_u *u,const char *hostname)
{
   socklen_t len=sizeof(*u);
   int a=accept(fd,&u->sa,&len);
   if(a<0)
      return a;
   NonBlock(a);
   CloseOnExec(a);
   KeepAlive(a);
   SetSocketBuffer(a,ResMgr::Query("net:socket-buffer",hostname));
   SetSocketMaxseg(a,ResMgr::Query("net:socket-maxseg",hostname));
   return a;
}

void Networker::SocketSinglePF(int s,int pf)
{
#if INET6 && defined(IPV6_V6ONLY)
   if(pf==PF_INET6) {
      int on = 1;
      if(-1==setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&on, sizeof(on)))
	 ProtoLog::LogError(1,"setsockopt(IPV6_V6ONLY): %s",strerror(errno));
   }
#endif
}

#ifdef TIOCOUTQ
static bool TIOCOUTQ_returns_free_space;
static bool TIOCOUTQ_works;
static bool TIOCOUTQ_tested;
static void test_TIOCOUTQ()
{
   int sock=socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
   if(sock==-1)
      return;
   TIOCOUTQ_tested=true;
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
#endif
int Networker::SocketBuffered(int sock)
{
#ifdef TIOCOUTQ
   if(!TIOCOUTQ_tested)
      test_TIOCOUTQ();
   if(!TIOCOUTQ_works)
      return 0;
   int buffer=0;
   if(TIOCOUTQ_returns_free_space)
   {
      socklen_t len=sizeof(buffer);
      if(getsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&buffer,&len)==-1)
	 return 0;
      int avail=buffer;
      if(ioctl(sock,TIOCOUTQ,&avail)==-1)
	 return 0;
      if(avail>buffer)
	 return 0; // something wrong
      buffer-=avail;
      buffer=buffer*3/4; // approx...
   }
   else
   {
      if(ioctl(sock,TIOCOUTQ,&buffer)==-1)
	 return 0;
   }
   return buffer;
#else
   return 0;
#endif
}

#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
const char *Networker::FindGlobalIPv6Address()
{
#if INET6 && defined(HAVE_IFADDRS_H)
   struct ifaddrs *ifaddrs=0;
   getifaddrs(&ifaddrs);
   for(struct ifaddrs *ifa=ifaddrs; ifa; ifa=ifa->ifa_next) {
      if(ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET6) {
	 struct in6_addr *addr=&((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
	 if(!IN6_IS_ADDR_UNSPECIFIED(addr) && !IN6_IS_ADDR_LOOPBACK(addr)
	 && !IN6_IS_ADDR_LINKLOCAL(addr) && !IN6_IS_ADDR_SITELOCAL(addr)
	 && !IN6_IS_ADDR_MULTICAST(addr)) {
	    char *buf=xstring::tmp_buf(INET6_ADDRSTRLEN);
	    inet_ntop(AF_INET6, addr, buf, INET6_ADDRSTRLEN);
	    freeifaddrs(ifaddrs);
	    return buf;
	 }
      }
   }
   freeifaddrs(ifaddrs);
#endif
   return 0;
}

bool Networker::CanCreateIpv6Socket()
{
#if INET6
   bool can=true;
   int s=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
   if(s==-1 && (errno==EINVAL
#ifdef EAFNOSUPPORT
      || errno==EAFNOSUPPORT
#endif
   ))
      can=false;
   if(s!=-1)
      close(s);
   return can;
#else
   return false;
#endif
}
