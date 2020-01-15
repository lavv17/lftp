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

#ifndef NETWORK_H
#define NETWORK_H

#include <string.h>
#include "sockets.h"
#include <sys/types.h>
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#elif HAVE_WS2TCPIP_H
# include <ws2tcpip.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>

class sockaddr_compact : public xstring
{
   void operator=(const sockaddr_compact&);   // disable assignment

public:
   int family() const {
      if(length()==16 || length()==18)
	 return AF_INET6;
      if(length()==4 || length()==6)
	 return AF_INET;
      return 0;
   }
   int port() const {
      if(length()==18 || length()==6)
	 return ((buf[length()-2]&255)<<8)|(buf[length()-1]&255);
      return 0;
   }
   void set_port(int p) {
      if(length()==18 || length()==6) {
	 buf[length()-2]=((p>>8)&255);
	 buf[length()-1]=(p&255);
      }
   }
   const char *address() const;
   static sockaddr_compact& get_tmp() {
      return *(sockaddr_compact*)&xstring::get_tmp("",0);
   }
   static sockaddr_compact& cast(xstring& s) { return *(sockaddr_compact*)&s; }
   static const sockaddr_compact& cast(const xstring& s) { return *(const sockaddr_compact*)&s; }
   sockaddr_compact() {}
   sockaddr_compact(const sockaddr_compact& c) : xstring(c.copy()) {}
};

union sockaddr_u
{
   struct sockaddr	sa;
   struct sockaddr_in	in;
#if INET6
   struct sockaddr_in6	in6;
#endif

   socklen_t addr_len() const {
      if(sa.sa_family==AF_INET)
	 return sizeof(in);
#if INET6
      if(sa.sa_family==AF_INET6)
	 return sizeof(in6);
#endif
      return sizeof(*this);
   }
   bool operator==(const sockaddr_u &o) const {
      return !memcmp(this,&o,addr_len());
   }
   bool operator!=(const sockaddr_u &o) const {
      return memcmp(this,&o,addr_len());
   }
   const char *address() const;
   int port() const;
   int bind_to(int s) const { return bind(s,&sa,addr_len()); }
   void clear() { memset(this,0,sizeof(*this)); }
   sockaddr_u() { clear(); }
   sockaddr_u(const sockaddr_compact& c) { clear(); set_compact(c); }
   bool is_reserved() const;
   bool is_multicast() const;
   bool is_loopback() const;
   bool is_private() const;
   bool is_compatible(const sockaddr_u&) const;
   const xstring& to_xstring() const;
   const char *to_string() const { return to_xstring(); }
   operator const char *() const { return to_string(); }
   bool set_defaults(int af,const char *hostname,int port);
   void set_port(int port);
   int family() const { return sa.sa_family; }
   bool set_compact(const char *c,size_t len);
   bool set_compact(const xstring& c) { return set_compact(c,c.length()); }
   const sockaddr_compact& compact() const;
   sockaddr_compact& compact_addr() const;
};

inline const char *sockaddr_compact::address() const
{
   return sockaddr_u(*this).address();
}

class Networker
{
protected:
   static void NonBlock(int fd);
   static void CloseOnExec(int fd);
   static void KeepAlive(int sock);
   static void MinimizeLatency(int sock);
   static void MaximizeThroughput(int sock);
   static void ReuseAddress(int sock);
   static int SocketBuffered(int sock);
   static const char *SocketNumericAddress(const sockaddr_u *u) { return u->address(); }
   static int SocketPort(const sockaddr_u *u) { return u->port(); }
   static socklen_t SocketAddrLen(const sockaddr_u *u) { return u->addr_len(); }
   static int SocketConnect(int fd,const sockaddr_u *u);
   static int SocketAccept(int fd,sockaddr_u *u,const char *hostname=0);
   static void SetSocketBuffer(int sock,int socket_buffer);
   static void SetSocketMaxseg(int sock,int socket_maxseg);
   static void SocketBindStd(int s,int af,const char *hostname,int port=0);
   static int SocketCreate(int af,int type,int proto,const char *hostname);
   static void SocketTuneTCP(int s,const char *hostname);
   static int SocketCreateTCP(int af,const char *hostname);
   static int SocketCreateUnbound(int af,int type,int proto,const char *hostname);
   static int SocketCreateUnboundTCP(int af,const char *hostname);
   static void SocketSinglePF(int sock,int pf);
   static const char *FindGlobalIPv6Address();
   static bool CanCreateIpv6Socket();
};

#endif //NETWORK_H
