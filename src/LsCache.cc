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

#include <config.h>
#include "LsCache.h"
#include "xmalloc.h"
#include "plural.h"

LsCache *LsCache::chain=0;
bool	 LsCache::use=true;
long	 LsCache::sizelimit=1024*1024;
time_t	 LsCache::ttl=60*60;  // time to live = 60 minutes
LsCache::ExpireHelper LsCache::expire_helper;

void LsCache::CheckSize()
{
   if(sizelimit<0)
      return;  // no limit

   LsCache **oldest;
   time_t   oldest_time;
   LsCache **scan;

   for(;;)
   {
      long size=0;
      oldest=&chain;
      oldest_time=0;
      for(scan=&chain; *scan; scan=&(*scan)->next)
      {
	 if(*oldest || oldest_time<(*scan)->timestamp)
	 {
	    oldest=scan;
	    oldest_time=(*scan)->timestamp;
	 }
	 size+=scan[0]->data_len;
      }
      if(size<=sizelimit)
	 break;
      LsCache *tmp=oldest[0]->next;
      delete(*oldest);
      *oldest=tmp;
   }
}

void LsCache::Add(FileAccess *p_loc,const char *a,int m,char *d,int l)
{
   CheckSize();

   LsCache *scan;
   for(scan=chain; scan; scan=scan->next)
   {
      if(scan->mode==m && !strcmp(scan->arg,a) && p_loc->SameLocationAs(scan->loc))
	 break;
   }
   if(!scan)
   {
      if(!use)
	 return;
      scan=new LsCache();
      scan->next=chain;
      scan->loc=p_loc->Clone();
      scan->loc->Suspend();
      scan->arg=xstrdup(a);
      scan->mode=m;
      chain=scan;
   }
   else
   {
      free(scan->data);
   }
   scan->data=(char*)xmemdup(d,l);
   scan->data_len=l;
   time(&scan->timestamp);
   if(expire_helper.expiring==0)
      expire_helper.expiring=scan;
   return;
}

int LsCache::Find(FileAccess *p_loc,const char *a,int m,char **d,int *l)
{
   if(!use)
      return 0;

   LsCache *scan;
   for(scan=chain; scan; scan=scan->next)
   {
      if(scan->mode==m && !strcmp(scan->arg,a) && p_loc->SameLocationAs(scan->loc))
      {
	 if(d && l)
	    *d=(char*)xmemdup(scan->data,*l=scan->data_len);
	 return 1;
      }
   }
   return 0;
}

LsCache::~LsCache()
{
   if(expire_helper.expiring==this)
      expire_helper.expiring=0;
   free(data);
   delete(loc);
   free(arg);
}

void LsCache::Flush()
{
   while(chain)
   {
      LsCache *n=chain->next;
      delete chain;
      chain=n;
   }
}

void LsCache::List()
{
   if(use)
      puts(_("Cache is on"));
   else
      puts(_("Cache is off"));

   long vol=0;
   for(LsCache *scan=chain; scan; scan=scan->next)
      vol+=scan->data_len;

   printf(plural("%ld $byte|bytes$ cached",int(vol%100)),vol);

   if(sizelimit<0)
      puts(_(", no size limit"));
   else
      printf(_(", maximum size %ld\n"),sizelimit);

   if(ttl==0)
      puts(_("Cache entries do not expire"));
   else if(ttl<60)
      printf(plural("Cache entries expire in %ld $second|seconds$\n",
		     int(ttl%100)),(long)ttl);
   else
   {
      long ttl_min=(long)(ttl+30)/60;
      printf(plural("Cache entries expire in %ld $minute|minutes$\n",
		     int(ttl_min%100)),ttl_min);
   }
}

int LsCache::ExpireHelper::Do()
{
   if(ttl==0)
      return STALL;
   time_t ct=time(0);
   if(!expiring || expiring->timestamp+ttl <= ct)
   {
      LsCache **scan=&LsCache::chain;
      while(*scan)
      {
	 if((*scan)->timestamp+ttl <= ct)
	 {
	    LsCache *tmp=*scan;
	    *scan=tmp->next;
	    delete tmp;
	    continue;
	 }
	 if(!expiring || expiring->timestamp > (*scan)->timestamp)
	    expiring=*scan;
	 scan=&scan[0]->next;
      }
      if(!expiring)
	 return STALL;
   }
   block+=TimeOut(expiring->timestamp+ttl-ct);
   return STALL;
}
