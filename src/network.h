/*
 * lftp and utils
 *
 * Copyright (c) 2008 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef NETWORK_H
#define NETWORK_H

#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>

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
   int bind_to(int s) { return bind(s,&sa,addr_len()); }
};

#endif //NETWORK_H