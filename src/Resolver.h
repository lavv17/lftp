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

#ifndef RESOLVER_H
#define RESOLVER_H

#include "ProcWait.h"
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "buffer.h"

union sockaddr_u
{
	struct sockaddr		sa;
	struct sockaddr_in	in;
#if INET6
	struct sockaddr_in6	in6;
#endif
};

class Resolver : public SMTask
{
   char *hostname;
   char *portname;

   char *service;
   char *proto;
   char *defport;

   int port_number;

   int pipe_to_child[2];
   ProcWait *w;
   FileInputBuffer *buf;
   int timeout;

   time_t start_time;

   int addr_num;
   sockaddr_u *addr;

   void AddAddress(int family,const char *a,int len);

   char *err_msg;
   bool done;

   void  MakeErrMsg(const char *f);
   void	 DoGethostbyname();

   static const char *ParseOrder(const char *s,int *o);

   void LookupOne(const char *name);
   void LookupSRV_RR();
   const char *error;

public:
   int	 Do();
   bool	 Done() { return done; }
   bool	 Error() { return err_msg!=0; }
   const char *ErrorMsg() { return err_msg; }
   sockaddr_u *Result() { return addr; }
   size_t GetResultSize() { return addr_num*sizeof(*addr); }
   int	 GetResultNum() { return addr_num; }
   void  GetResult(void *m) { memcpy(m,addr,GetResultSize()); }

   Resolver(const char *h,const char *p,const char *defp=0,const char *ser=0,
	    const char *pr=0);
   ~Resolver();

   void Reconfig();

   static const char *OrderValidate(char **s);

   static void ClassInit();
};

#endif // RESOLVER_H
