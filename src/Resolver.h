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

#ifndef RESOLVER_H
#define RESOLVER_H

#include "ProcWait.h"
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "buffer.h"
#include "xmalloc.h"

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
   IOBuffer *buf;
   int timeout;

   time_t start_time;

   int addr_num;
   sockaddr_u *addr;

   void AddAddress(int family,const char *a,int len);

   char *err_msg;
   bool done;

   void  MakeErrMsg(const char *f);
   void	 DoGethostbyname();

   static int FindAddressFamily(const char *name);
   static void ParseOrder(const char *s,int *o);

   void LookupOne(const char *name);
   void LookupSRV_RR();
   const char *error;

   static class ResolverCache *cache;

   bool no_cache;
   bool use_fork;

protected:
   ~Resolver();

public:
   int	 Do();
   bool	 Done() { return done; }
   bool	 Error() { return err_msg!=0; }
   const char *ErrorMsg() { return err_msg; }
   sockaddr_u *Result() { return addr; }
   size_t GetResultSize() { return addr_num*sizeof(*addr); }
   int	 GetResultNum() { return addr_num; }
   void  GetResult(void *m) { memcpy(m,addr,GetResultSize()); }
   void	 UseCache(bool y) { no_cache=!y; }
   void	 NoCache() { UseCache(false); }

   Resolver(const char *h,const char *p,const char *defp=0,const char *ser=0,
	    const char *pr=0);

   void Reconfig(const char *name=0);
};

class ResolverCache
{
   class Entry
   {
      friend class ResolverCache;

      char *hostname;
      char *portname;
      char *defport;
      char *service;
      char *proto;

      int addr_num;
      sockaddr_u *addr;
      time_t timestamp;

      Entry *next;

      Entry(Entry *nxt,const char *h,const char *p,const char *defp,
         const char *ser,const char *pr,const sockaddr_u *a,int n)
	 {
	    next=nxt;

	    hostname=xstrdup(h);
	    portname=xstrdup(p);
	    service=xstrdup(ser);
	    proto=xstrdup(pr);
	    defport=xstrdup(defp);

	    addr_num=n;
	    addr=(sockaddr_u*)xmalloc(n*sizeof(*addr));
	    memcpy(addr,a,n*sizeof(*addr));

	    timestamp=SMTask::now;
	 }
      ~Entry()
	 {
	    xfree(hostname);
	    xfree(portname);
	    xfree(service);
	    xfree(proto);
	    xfree(defport);
	    xfree(addr);
	 }
   };

   Entry *chain;

   Entry **FindPtr(const char *h,const char *p,const char *defp,
         const char *ser,const char *pr);

   void CacheCheck(); // prune cache as needed

public:
   void Add(const char *h,const char *p,const char *defp,
         const char *ser,const char *pr,const sockaddr_u *a,int n);
   void Find(const char *h,const char *p,const char *defp,
         const char *ser,const char *pr,const sockaddr_u **a,int *n);
   void Clear();

   ResolverCache();
};

#endif // RESOLVER_H
