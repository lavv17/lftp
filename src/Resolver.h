/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "Cache.h"

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
   xstring hostname;
   xstring portname;

   xstring service;
   xstring proto;
   xstring defport;

   int port_number;

   int pipe_to_child[2];
   ProcWait *w;
   IOBuffer *buf;
   Timer timeout_timer;

   int addr_num;
   sockaddr_u *addr;

   void AddAddress(int family,const char *a,int len);

   xstring err_msg;
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
   const char *GetLogContext() { return hostname; }
};

class ResolverCacheEntryLoc
{
   char *hostname;
   char *portname;
   char *defport;
   char *service;
   char *proto;
public:
   ResolverCacheEntryLoc(const char *h,const char *p,const char *defp,const char *ser,const char *pr) {
      hostname=xstrdup(h);
      portname=xstrdup(p);
      service=xstrdup(ser);
      proto=xstrdup(pr);
      defport=xstrdup(defp);
   }
   ~ResolverCacheEntryLoc() {
      xfree(hostname);
      xfree(portname);
      xfree(service);
      xfree(proto);
      xfree(defport);
   }
   const char *GetClosure() const { return hostname; }
   bool Matches(const char *h,const char *p,const char *defp,const char *ser,const char *pr);
};
class ResolverCacheEntryData
{
   int addr_num;
   sockaddr_u *addr;
public:
   ResolverCacheEntryData(const sockaddr_u *a,int n) {
      addr_num=n;
      addr=(sockaddr_u*)xmemdup(a,n*sizeof(*addr));
   }
   ~ResolverCacheEntryData() {
      xfree(addr);
   }
   void SetData(const sockaddr_u *a,int n) {
      xfree(addr);
      addr_num=n;
      addr=(sockaddr_u*)xmemdup(a,n*sizeof(*addr));
   }
   void GetData(const sockaddr_u **a,int *n) {
      *n=addr_num;
      *a=addr;
   }
};
class ResolverCacheEntry : public CacheEntry, public ResolverCacheEntryLoc, public ResolverCacheEntryData
{
public:
   ResolverCacheEntry(const char *h,const char *p,const char *defp,const char *ser,const char *pr,
	 const sockaddr_u *a,int n) : ResolverCacheEntryLoc(h,p,defp,ser,pr), ResolverCacheEntryData(a,n) {
      SetResource("dns:cache-expire",GetClosure());
   }
};
class ResolverCache : public Cache, public ResClient
{
   ResolverCacheEntry *Find(const char *h,const char *p,const char *defp,const char *ser,const char *pr);
   ResolverCacheEntry *IterateFirst() { return (ResolverCacheEntry*)Cache::IterateFirst(); }
   ResolverCacheEntry *IterateNext()  { return (ResolverCacheEntry*)Cache::IterateNext(); }
   ResolverCacheEntry *IterateDelete(){ return (ResolverCacheEntry*)Cache::IterateDelete(); }
public:
   void Add(const char *h,const char *p,const char *defp,
         const char *ser,const char *pr,const sockaddr_u *a,int n);
   void Find(const char *h,const char *p,const char *defp,
         const char *ser,const char *pr,const sockaddr_u **a,int *n);
   ResolverCache();
   void Reconfig(const char *);
};

#endif // RESOLVER_H
