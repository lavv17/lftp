/*
 * lftp and utils
 *
 * Copyright (c) 2008 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "network.h"
#include <stdio.h>
#include <netdb.h>

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
