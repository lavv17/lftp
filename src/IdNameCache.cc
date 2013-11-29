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

#include <config.h>
#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <ctype.h>
#include "IdNameCache.h"

void IdNameCache::init()
{
   memset(table_id,0,sizeof(table_id));
   memset(table_name,0,sizeof(table_name));
}
void IdNameCache::free_list(IdNamePair *list)
{
   while(list)
   {
      IdNamePair *next=list->next;
      delete list;
      list=next;
   }
}
void IdNameCache::free()
{
   for(int i=0; i<table_size; i++)
   {
      free_list(table_id[i]);
      free_list(table_name[i]);
   }
}
void IdNameCache::add(unsigned h,IdNamePair **p,IdNamePair *r)
{
   r->next=p[h];
   p[h]=r;
}
IdNamePair *IdNameCache::lookup(int id)
{
   unsigned h=hash(id);
   for(IdNamePair *scan=table_id[h]; scan; scan=scan->next)
      if(id==scan->id)
	 return scan;
   IdNamePair *r=get_record(id);
   if(!r)
      r=new IdNamePair(id,0);
   add(h,table_id,r);
   if(r->name)
      add(hash(r->name),table_name,new IdNamePair(r));
   return r;
}
IdNamePair *IdNameCache::lookup(const char *name)
{
   unsigned h=hash(name);
   for(IdNamePair *scan=table_name[h]; scan; scan=scan->next)
      if(!xstrcmp(name,scan->name))
	 return scan;
   IdNamePair *r=get_record(name);
   if(!r)
      r=new IdNamePair(-1,name);
   add(h,table_name,r);
   if(r->id!=-1)
      add(hash(r->id),table_id,new IdNamePair(r));
   return r;
}
const char *IdNameCache::Lookup(int id)
{
   const char *name=lookup(id)->name;
   if(name && name[0])
      return name;
   static char buf[32];
   snprintf(buf,sizeof(buf),"%d",id);
   return buf;
}
int IdNameCache::Lookup(const char *name)
{
   return lookup(name)->id;
}
IdNameCache::IdNameCache()
{
   init();
}
IdNameCache::~IdNameCache()
{
   free();
}
int IdNameCache::Do()
{
   if(expire_timer && expire_timer->Stopped())
      Delete(this);
   return STALL;
}

unsigned IdNameCache::hash(int id)
{
   return unsigned(id)%table_size;
}
unsigned IdNameCache::hash(const char *name)
{
   unsigned h=0;
   while(*name)
      h+=(h<<4)+*name++;
   return h%table_size;
}

IdNamePair *PasswdCache::get_record(int id)
{
   struct passwd *p=getpwuid(id);
   if(!p)
      return 0;
   return new IdNamePair(p->pw_uid,p->pw_name);
}
IdNamePair *GroupCache::get_record(int id)
{
   struct group *p=getgrgid(id);
   if(!p)
      return 0;
   return new IdNamePair(p->gr_gid,p->gr_name);
}

IdNamePair *IdNameCache::get_record(const char *name)
{
   int id,n;
   if(sscanf(name,"%d%n",&id,&n)==1 && !name[n])
      return new IdNamePair(id,name);
   return 0;
}
IdNamePair *PasswdCache::get_record(const char *name)
{
   struct passwd *p=getpwnam(name);
   if(p)
      return new IdNamePair(p->pw_uid,name);
   return IdNameCache::get_record(name);
}
IdNamePair *GroupCache::get_record(const char *name)
{
   struct group *p=getgrnam(name);
   if(p)
      return new IdNamePair(p->gr_gid,name);
   return IdNameCache::get_record(name);
}

PasswdCache *PasswdCache::instance;
GroupCache *GroupCache::instance;
PasswdCache *PasswdCache::GetInstance()
{
   if(instance)
      return instance;
   instance=new PasswdCache();
   instance->SetExpireTimer(new Timer(30));
   return instance;
}
GroupCache *GroupCache::GetInstance()
{
   if(instance)
      return instance;
   instance=new GroupCache();
   instance->SetExpireTimer(new Timer(30));
   return instance;
}
PasswdCache::~PasswdCache()
{
   if(this==instance)
      instance=0;
}
GroupCache::~GroupCache()
{
   if(this==instance)
      instance=0;
}
