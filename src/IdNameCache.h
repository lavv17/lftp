/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef IDNAMECACHE_H
#define IDNAMECACHE_H

#include "SMTask.h"
#include "Timer.h"
#include "StringPool.h"

struct IdNamePair
{
   int id;
   const char *name;
   IdNamePair *next;

   IdNamePair(int id1,const char *name1) { id=id1; name=StringPool::Get(name1); }
};

class IdNameCache : public SMTask
{
   Timer *expire_timer;

   static const int table_size=131;
   unsigned hash(const char *);
   unsigned hash(int);

   IdNamePair *table_id[table_size];
   IdNamePair *table_name[table_size];

   void init();
   void free();
   static void free_list(IdNamePair *list);

   void add(unsigned h,IdNamePair **,IdNamePair *);

protected:
   virtual IdNamePair *get_record(int id)=0;
   virtual IdNamePair *get_record(const char *name)=0;

   IdNamePair *lookup(int id);
   IdNamePair *lookup(const char *id);

public:
   IdNameCache() { init(); expire_timer=0; }
   virtual ~IdNameCache();
   void Clear() { free(); init(); }
   const char *Lookup(int id);
   int Lookup(const char *);

   void SetExpireTimer(Timer *t) { Delete(expire_timer); expire_timer=t; }

   int Do();
};

class PasswdCache : public IdNameCache
{
   static PasswdCache *instance;
   static PasswdCache *GetInstance();

   IdNamePair *get_record(int id);
   IdNamePair *get_record(const char *name);

public:
   static int LookupS(const char *name) { return GetInstance()->Lookup(name); }
   static const char *LookupS(int id)   { return GetInstance()->Lookup(id); }

   ~PasswdCache();
   static void DeleteInstance() { Delete(instance); }
};
class GroupCache : public IdNameCache
{
   static GroupCache *instance;
   static GroupCache *GetInstance();

   IdNamePair *get_record(int id);
   IdNamePair *get_record(const char *name);

public:
   static int LookupS(const char *name) { return GetInstance()->Lookup(name); }
   static const char *LookupS(int id)   { return GetInstance()->Lookup(id); }

   ~GroupCache();
   static void DeleteInstance() { Delete(instance); }
};

inline void IdNameCacheCleanup()
{
   PasswdCache::DeleteInstance();
   GroupCache::DeleteInstance();
}

#endif
