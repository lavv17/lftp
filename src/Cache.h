/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef CACHE_H
#define CACHE_H

#include "Timer.h"

class CacheEntry : public Timer
{
   friend class Cache;
   CacheEntry *next;
public:
   CacheEntry() { next=0; }
   virtual int EstimateSize() const { return 1; }
   virtual ~CacheEntry() {}
};
class Cache
{
   const ResType *res_max_size;
   const ResType *res_enable;
protected:
   CacheEntry *chain;
   CacheEntry **curr;
   CacheEntry *IterateFirst();
   CacheEntry *IterateNext();
   CacheEntry *IterateDelete();
public:
   void Trim();
   void Flush();
   Cache(const ResType *s,const ResType *e) {
      res_max_size=s;
      res_enable=e;
      chain=0;
      curr=0;
   }
   ~Cache() { Flush(); }
   bool IsEnabled(const char *closure) { return res_enable->QueryBool(closure); }
   long SizeLimit() { return res_max_size->Query(0); }
   void AddCacheEntry(CacheEntry *e) {
      e->next=chain;
      chain=e;
   }
};

#endif//CACHE_H
