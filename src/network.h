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

/* $Id: network.h,v 1.10 2011/03/25 14:27:14 lav Exp $ */

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
   int operator==(const sockaddr_u &o) const {
      return !memcmp(this,&o,addr_len());
   }
   const char *address() const;
   int port() const;
   int bind_to(int s) const { return bind(s,&sa,addr_len()); }
   sockaddr_u();
   bool is_reserved() const;
   bool is_multicast() const;
   bool is_loopback() const;
   bool is_private() const;
   const xstring& to_string() const;
   operator const char *() const { return to_string(); }
   bool set_defaults(int af,const char *hostname,int port);
};

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
};

#endif //NETWORK_H
