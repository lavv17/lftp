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
#include <assert.h>
#include "LsCache.h"
#include "xmalloc.h"
#include "plural.h"
#include "misc.h"

LsCache *LsCache::chain=0;
bool	 LsCache::use=true;
long	 LsCache::sizelimit=1024*1024;
TimeInterval LsCache::ttl("60m");  // time to live = 60 minutes
TimeInterval LsCache::ttl_neg("1m");  // time to live for negative cache
LsCache::ExpireHelper LsCache::expire_helper;

void LsCache::CheckSize()
{
   if(sizelimit<0)
      return;  // no limit

   LsCache **oldest;
   time_t   oldest_expire_time;
   LsCache **scan;

   for(;;)
   {
      long size=0;
      oldest=&chain;
      oldest_expire_time=0;
      for(scan=&chain; *scan; scan=&(*scan)->next)
      {
	 time_t e_time=(*scan)->ExpireTime();
	 if(oldest_expire_time<e_time)
	 {
	    oldest=scan;
	    oldest_expire_time=e_time;
	 }
	 size+=scan[0]->data_len;
	 if(scan[0]->afset)
	    size+=scan[0]->afset->EstimateMemory();
      }
      if(size<=sizelimit)
	 break;
      LsCache *tmp=oldest[0]->next;
      delete(*oldest);
      *oldest=tmp;
   }
}

ResDecl res_cache_empty_listings("cache:cache-empty-listings","no",ResMgr::BoolValidate,0);
ResDecl res_cache_enable("cache:enable","yes",ResMgr::BoolValidate,0);
ResDecl res_cache_expire("cache:expire","60m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_expire_neg("cache:expire-negative","1m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_size  ("cache:size","1048576",ResMgr::UNumberValidate,ResMgr::NoClosure);

void LsCache::Add(FileAccess *p_loc,const char *a,int m,int e,const char *d,int l,const FileSet *fs)
{
   if(!strcmp(p_loc->GetProto(),"file"))
      return;  // don't cache local objects
   if(l == 0 &&
	 !res_cache_empty_listings.QueryBool(p_loc->GetHostName()))
      return;
   if(e!=FA::OK && e!=FA::NO_FILE && e!=FA::NOT_SUPP)
      return;

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
      xfree(scan->data);
      delete scan->afset;
   }
   scan->err_code=e;
   scan->data=(char*)xmemdup(d,l);
   scan->data_len=l;
   scan->afset=fs?new FileSet(fs):0;
   scan->timestamp=SMTask::now;
   if(expire_helper.expiring==0
   || scan->ExpireTime()<expire_helper.expiring->ExpireTime())
      expire_helper.expiring=scan;
}

void LsCache::Add(FileAccess *p_loc,const char *a,int m,int e,const Buffer *ubuf,const FileSet *fs)
{
   if(!ubuf->IsSaving())
      return;

   const char *cache_buffer;
   int cache_buffer_size;
   if(e)
   {
      cache_buffer=ubuf->ErrorText();
      cache_buffer_size=strlen(cache_buffer)+1;
   }
   else
      ubuf->GetSaved(&cache_buffer,&cache_buffer_size);
   LsCache::Add(p_loc,a,m,e,cache_buffer,cache_buffer_size,fs);
}

int LsCache::Find(FileAccess *p_loc,const char *a,int m,int *e,const char **d,int *l,FileSet **fs)
{
   if(!ResMgr::QueryBool("cache:enable",p_loc->GetHostName()))
      return 0;

   for(LsCache *scan=chain; scan; scan=scan->next)
   {
      if((m == -1 || scan->mode==m) && !strcmp(scan->arg,a) && p_loc->SameLocationAs(scan->loc))
      {
	 if(d && l)
	 {
	    *d=scan->data;
	    *l=scan->data_len;
	 }
	 if(fs)
	    *fs=scan->afset;
	 *e=scan->err_code;
	 return 1;
      }
   }
   return 0;
}

FileSet *LsCache::FindFileSet(FileAccess *p_loc,const char *a,int m)
{
   const char *buf_c;
   int bufsiz;
   int e;
   FileSet *fs=0;
   if(!Find(p_loc, a, m, &e, &buf_c, &bufsiz, &fs))
      return 0;

   if(fs)
      return fs;

   fs=p_loc->ParseLongList(buf_c, bufsiz);
   if(!fs)
      return 0;

   for(LsCache *scan=chain; scan; scan=scan->next)
   {
      if(scan->data==buf_c)
	 scan->afset=fs;
   }
   return fs;
}

LsCache::~LsCache()
{
   if(expire_helper.expiring==this)
      expire_helper.expiring=0;
   SMTask::Delete(loc);
   xfree(data);
   xfree(arg);
   delete afset;
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
      vol+=scan->data_len+(scan->afset?scan->afset->EstimateMemory():0);

   printf(plural("%ld $#l#byte|bytes$ cached",vol),vol);

   if(sizelimit<0)
      puts(_(", no size limit"));
   else
      printf(_(", maximum size %ld\n"),sizelimit);

   if(ttl.IsInfty() || ttl.Seconds()==0)
      puts(_("Cache entries do not expire"));
   else if(ttl.Seconds()<60)
      printf(plural("Cache entries expire in %ld $#l#second|seconds$\n",
		     long(ttl.Seconds())),long(ttl.Seconds()));
   else
   {
      long ttl_min=(long)(ttl.Seconds()+30)/60;
      printf(plural("Cache entries expire in %ld $#l#minute|minutes$\n",
		     ttl_min),ttl_min);
   }
}

time_t LsCache::ExpireTime()
{
   TimeInterval *this_ttl=(err_code==FA::OK?&ttl:&ttl_neg);
   if(this_ttl->IsInfty() || this_ttl->Seconds()==0)
      return 0;
   return(timestamp+this_ttl->Seconds());
}
bool LsCache::Expired()
{
   time_t e_time=ExpireTime();
   return(e_time && e_time<=(time_t)SMTask::now);
}

int LsCache::ExpireHelper::Do()
{
   if((ttl.IsInfty() || ttl.Seconds()==0)
   && (ttl_neg.IsInfty() || ttl_neg.Seconds()==0))
      return STALL;
   if(!expiring || expiring->Expired())
   {
      LsCache **scan=&LsCache::chain;
      while(*scan)
      {
	 if((*scan)->Expired())
	 {
	    LsCache *tmp=*scan;
	    *scan=tmp->next;
	    delete tmp;
	    scan=&LsCache::chain;   // rescan the chain to find next expiring
	    continue;
	 }
	 if(!expiring)
	    expiring=*scan;
	 else
	 {
	    // compare expire times and find the least one
   	    time_t expiring_e_time=expiring->ExpireTime();
	    time_t scan_e_time=(*scan)->ExpireTime();
	    if(scan_e_time!=0 && scan_e_time!=0 && expiring_e_time > scan_e_time);
	       expiring=*scan;
	 }
	 scan=&scan[0]->next;
      }
      if(!expiring)
	 return STALL;
   }
   time_t t_out=expiring->ExpireTime()-(time_t)now;
   if(t_out>1024)
      t_out=1024;
   TimeoutS(t_out);
   return STALL;
}
void LsCache::ExpireHelper::Reconfig(const char *name)
{
   SetSizeLimit(ResMgr::Query("cache:size",0));
   SetExpire(TimeInterval(ResMgr::Query("cache:expire",0)));
   SetExpireNegative(TimeInterval(ResMgr::Query("cache:expire-negative",0)));
   use=ResMgr::QueryBool("cache:enable",0);
}

void LsCache::Changed(change_mode m,FileAccess *f,const char *dir)
{
   const char *fdir_c=dir_file(f->GetCwd(),dir);
   char *fdir=alloca_strdup(fdir_c);
   if(m==FILE_CHANGED)
      dirname_modify(fdir);
   int fdir_len=strlen(fdir);

   LsCache **scan=&LsCache::chain;
   while(*scan)
   {
      FileAccess *sloc=(*scan)->loc;
      if(f->SameLocationAs(sloc) || (f->SameSiteAs(sloc)
	       && (m==TREE_CHANGED?
		     !strncmp(fdir,dir_file(sloc->GetCwd(),(*scan)->arg),fdir_len)
		   : !strcmp (fdir,dir_file(sloc->GetCwd(),(*scan)->arg)))))
      {
	 LsCache *tmp=*scan;
	 *scan=tmp->next;
	 delete tmp;
	 continue;
      }
      scan=&scan[0]->next;
   }
}

/* Mark a path as a directory or file. (We have other ways of knowing this;
 * this is the most explicit and least expensive.) */
void LsCache::SetDirectory(FileAccess *p_loc, const char *path, bool dir)
{
   if(!path)
      return;

   char *origdir = alloca_strdup(p_loc->GetCwd());

   p_loc->Chdir(path,false);
   const char *entry = dir? "1":"0";
   LsCache::Add(p_loc,"",FileAccess::CHANGE_DIR, dir?FA::OK:FA::NO_FILE, entry, strlen(entry));
   p_loc->SetCwd(origdir);
}

/* This is a hint function. If file type is really needed, use GetFileInfo
 * with showdir set to true. (GetFileInfo uses this function.)
 * Returns -1 if type is not known, 1 if a directory, 0 if a file. */

int LsCache::IsDirectory(FileAccess *p_loc,const char *dir_c)
{
   char *origdir = alloca_strdup(p_loc->GetCwd());
   p_loc->Chdir(dir_c, false);

   int ret = -1;

   /* Cheap tests first:
    *
    * First, we know the path is a directory or not if we have an expicit
    * CHANGE_DIR entry for it. */
   const char *buf_c;
   int bufsiz;
   int e;
   if(Find(p_loc, "", FileAccess::CHANGE_DIR, &e, &buf_c,&bufsiz))
   {
      assert(bufsiz==1);
      ret = (e==FA::OK);
      goto leave;
   }

   /* We know the path is a directory if we have a cache entry for it.  This is
    * true regardless of the list type.  (Unless it's a CHANGE_DIR entry; do this
    * test after the CHANGE_DIR check.) */
   if(Find(p_loc, "", FA::LONG_LIST, &e, 0,0))
   {
      ret = (e==FA::OK);
      goto leave;
   }
   if(Find(p_loc, "", FA::MP_LIST, &e, 0,0))
   {
      ret = (e==FA::OK);
      goto leave;
   }
   if(Find(p_loc, "", FA::LIST, &e, 0,0))
   {
      ret = (e==FA::OK);
      goto leave;
   }

   /* We know this is a file or a directory if the dirname is cached and
    * contains the basename. */
   {
      p_loc->SetCwd(origdir);
      char *dir = alloca_strdup(dir_c);
      p_loc->Chdir(dirname_modify(dir), false);


      const FileSet *fs=FindFileSet(p_loc, "", FA::MP_LIST);
      if(!fs)
	 fs=FindFileSet(p_loc, "", FA::LONG_LIST);
      if(fs)
      {
	 FileInfo *fi=fs->FindByName(basename_ptr(dir_c));
	 if(fi && (fi->defined&fi->TYPE))
	    ret = (fi->filetype == fi->DIRECTORY);
      }
   }

leave:
   p_loc->SetCwd(origdir);
   return ret;
}
