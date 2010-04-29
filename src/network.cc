/*
 * lftp and utils
 *
 * Copyright (c) 2008-2010 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <arpa/inet.h>
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

sockaddr_u::sockaddr_u()
{
   memset(this,0,sizeof(*this));
}

const char *sockaddr_u::address() const
{
#ifdef HAVE_GETNAMEINFO
   static char buf[NI_MAXHOST];
   if(getnameinfo(&sa,addr_len(),buf,sizeof(buf),0,0,NI_NUMERICHOST)<0)
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

const xstring& sockaddr_u::to_string() const
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
   return false;
}

bool sockaddr_u::is_multicast() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]>=224 && a[0]<240);
   }
   return false;
}

bool sockaddr_u::is_private() const
{
   if(sa.sa_family==AF_INET)
   {
      unsigned char *a=(unsigned char *)&in.sin_addr;
      return (a[0]==10)
	  || (a[0]==172 && a[1]>=16 && a[1]<32)
	  || (a[0]==192 && a[1]==168);
   }
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
void Networker::SocketBindStd(int s,int af,const char *hostname)
{
   const char *b=0;
   sockaddr_u bind_addr;
   memset(&bind_addr,0,sizeof(bind_addr));
   bind_addr.in.sin_family=af;
   if(af==AF_INET)
   {
      b=ResMgr::Query("net:socket-bind-ipv4",hostname);
      if(!(b && b[0] && inet_pton(af,b,&bind_addr.in.sin_addr)))
	 b=0;
   }
#if INET6
   else if(af==AF_INET6)
   {
      b=ResMgr::Query("net:socket-bind-ipv6",hostname);
      if(!(b && b[0] && inet_pton(af,b,&bind_addr.in6.sin6_addr)))
	 b=0;
   }
#endif
   if(b)
   {
      int res=bind_addr.bind_to(s);
      if(res==-1)
	 ProtoLog::LogError(0,"bind(socket, %s): %s",b,strerror(errno));
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
