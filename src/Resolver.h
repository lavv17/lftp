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

#ifndef RESOLVER_H
#define RESOLVER_H

#include "ProcWait.h"
#include "buffer.h"
#include "xarray.h"
#include "Cache.h"
#include "network.h"

class Resolver : public SMTask, protected ProtoLog, protected Networker
{
   xstring hostname;
   xstring portname;

   xstring service;
   xstring proto;
   xstring defport;

   int port_number;

   int pipe_to_child[2];
   SMTaskRef<ProcWait> w;
   SMTaskRef<IOBuffer> buf;
   Timer timeout_timer;

   xarray<sockaddr_u> addr;

   void AddAddress(int family,const char *a,int len,unsigned int scope);

   xstring err_msg;
   bool done;

   void  MakeErrMsg(const char *f);
   void	 DoGethostbyname();

   static int FindAddressFamily(const char *name);
   static bool IsAddressFamilySupporded(int af);
   static void ParseOrder(const char *s,int *o);

   void LookupOne(const char *name);
   void LookupSRV_RR();
   const char *error;

   static class ResolverCache *cache;

   bool no_cache;
   bool use_fork;

public:
   int	 Do();
   bool	 Done() { return done; }
   bool	 Error() { return err_msg!=0; }
   const char *ErrorMsg() { return err_msg; }
   const xarray<sockaddr_u>& Result() { return addr; }
   size_t GetResultSize() { return addr.count()*addr.get_element_size(); }
   int	 GetResultNum() { return addr.count(); }
   void  GetResult(void *m) { memcpy(m,addr.get(),GetResultSize()); }
   void	 UseCache(bool y) { no_cache=!y; }
   void	 NoCache() { UseCache(false); }

   Resolver(const char *h,const char *p,const char *defp=0,const char *ser=0,
	    const char *pr=0);
   ~Resolver();

   void Reconfig(const char *name=0);
   const char *GetLogContext() { return hostname; }
};

class ResolverCacheEntryLoc
{
   xstring_c hostname;
   xstring_c portname;
   xstring_c defport;
   xstring_c service;
   xstring_c proto;
public:
   ResolverCacheEntryLoc(const char *h,const char *p,const char *defp,const char *ser,const char *pr)
      : hostname(h), portname(p), defport(defp), service(ser), proto(pr) {}
   const char *GetClosure() const { return hostname; }
   bool Matches(const char *h,const char *p,const char *defp,const char *ser,const char *pr);
};
class ResolverCacheEntryData
{
   xarray<sockaddr_u> addr;
public:
   ResolverCacheEntryData(const sockaddr_u *a,int n) {
      addr.nset(a,n);
   }
   void SetData(const sockaddr_u *a,int n) {
      addr.nset(a,n);
   }
   void GetData(const sockaddr_u **a,int *n) {
      *n=addr.count();
      *a=addr.get();
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
