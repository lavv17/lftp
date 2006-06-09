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

#include <config.h>
#include <assert.h>
#include "LsCache.h"
#include "xmalloc.h"
#include "plural.h"
#include "misc.h"

static const char p[]="cache:";

int LsCache::EstimateMemory() const
{
   int size=sizeof(*this)+data_len+xstrlen(arg)+(arg!=0);
   if(afset)
      size+=afset->EstimateMemory();
   return size;
}

void LsCache::Trim()
{
   long sizelimit=SizeLimit();

   if(sizelimit<0)
      return;  // no limit

   long size=0;
   LsCache *c;
   Timer **scan=IterateAll(0,p);
   while((c=(LsCache*)*scan)!=0)
   {
      if(c->Stopped())
      {
	 delete c;
	 scan=IterateAll(scan,p,0);
      }
      else
      {
	 size+=c->EstimateMemory();
	 scan=IterateAll(scan,p);
      }
   }
   for(scan=IterateRunning(0,p); (c=(LsCache*)*scan)!=0 && size>sizelimit; scan=IterateRunning(scan,p,0))
   {
      LsCache *c=(LsCache*)*scan;
      size-=c->EstimateMemory();
      delete c;
   }
}

ResDecl res_cache_empty_listings("cache:cache-empty-listings","no",ResMgr::BoolValidate,0);
ResDecl res_cache_enable("cache:enable","yes",ResMgr::BoolValidate,0);
ResDecl res_cache_expire("cache:expire","60m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_expire_neg("cache:expire-negative","1m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_size  ("cache:size","1048576",ResMgr::UNumberValidate,ResMgr::NoClosure);

bool LsCache::IsEnabled(const char *closure)
{
   return res_cache_enable.QueryBool(closure);
}
long LsCache::SizeLimit()
{
   return res_cache_size.Query(0);
}

void LsCache::Add(FileAccess *p_loc,const char *a,int m,int e,const char *d,int l,const FileSet *fs)
{
   if(!strcmp(p_loc->GetProto(),"file"))
      return;  // don't cache local objects
   if(l == 0 &&
	 !res_cache_empty_listings.QueryBool(p_loc->GetHostName()))
      return;
   if(e!=FA::OK && e!=FA::NO_FILE && e!=FA::NOT_SUPP)
      return;

   Trim();

   LsCache *c;
   for(Timer **scan=IterateAll(0,p); (c=(LsCache*)*scan)!=0; scan=IterateAll(scan,p))
   {
      if(c->mode==m && !strcmp(c->arg,a) && p_loc->SameLocationAs(c->loc))
	 break;
   }
   if(!c)
   {
      if(!IsEnabled(p_loc->GetHostName()))
	 return;
      c=new LsCache();
      c->loc=p_loc->Clone();
      c->loc->Suspend();
      c->arg=xstrdup(a);
      c->mode=m;
   }
   else
   {
      xfree(c->data);
      delete c->afset;
   }
   c->err_code=e;
   c->data=(char*)xmemdup(d,l);
   c->data_len=l;
   c->afset=fs?new FileSet(fs):0;
   c->SetResource(e==FA::OK?"cache:expire":"cache:expire-negative",c->loc->GetHostName());
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

LsCache *LsCache::Find(FileAccess *p_loc,const char *a,int m)
{
   if(!IsEnabled(p_loc->GetHostName()))
      return 0;

   LsCache *c;
   for(Timer **scan=IterateAll(0,p); (c=(LsCache*)*scan)!=0; scan=IterateAll(scan,p))
   {
      if((m == -1 || c->mode==m) && !strcmp(c->arg,a) && p_loc->SameLocationAs(c->loc))
      {
	 if(c->Stopped())
	 {
	    Trim();
	    return 0;
	 }
	 return c;
      }
   }
   return 0;
}

int LsCache::Find(FileAccess *p_loc,const char *a,int m,int *e,const char **d,int *l,FileSet **fs)
{
   LsCache *c=Find(p_loc,a,m);
   if(!c)
      return 0;
   if(d && l)
   {
      *d=c->data;
      *l=c->data_len;
   }
   if(fs)
      *fs=c->afset;
   *e=c->err_code;
   return 1;
}

FileSet *LsCache::FindFileSet(FileAccess *p_loc,const char *a,int m)
{
   LsCache *c=Find(p_loc,a,m);
   if(!c)
      return 0;
   if(c->afset)
      return c->afset;
   if(c->err_code!=FA::OK)
      return 0;
   return c->afset=p_loc->ParseLongList(c->data, c->data_len);
}

LsCache::~LsCache()
{
   loc->DeleteLater();
   xfree(data);
   xfree(arg);
   delete afset;
}

void LsCache::Flush()
{
   LsCache *c;
   for(Timer **scan=IterateAll(0,p); (c=(LsCache*)*scan)!=0; scan=IterateAll(scan,p,0))
      delete c;
}

void LsCache::List()
{
   Trim();

   long vol=0;
   LsCache *c;
   for(Timer **scan=IterateAll(0,p); (c=(LsCache*)*scan)!=0; scan=IterateAll(scan,p))
      vol+=c->EstimateMemory();

   printf(plural("%ld $#l#byte|bytes$ cached",vol),vol);

   long sizelimit=res_cache_size.Query(0);
   if(sizelimit<0)
      puts(_(", no size limit"));
   else
      printf(_(", maximum size %ld\n"),sizelimit);
}

void LsCache::Changed(change_mode m,FileAccess *f,const char *dir)
{
   const char *fdir_c=dir_file(f->GetCwd(),dir);
   char *fdir=alloca_strdup(fdir_c);
   if(m==FILE_CHANGED)
      dirname_modify(fdir);
   int fdir_len=strlen(fdir);

   LsCache *c;
   Timer **scan=IterateAll(0,p);
   while((c=(LsCache*)*scan)!=0)
   {
      FileAccess *sloc=c->loc;
      if(f->SameLocationAs(sloc) || (f->SameSiteAs(sloc)
	       && (m==TREE_CHANGED?
		     !strncmp(fdir,dir_file(sloc->GetCwd(),c->arg),fdir_len)
		   : !strcmp (fdir,dir_file(sloc->GetCwd(),c->arg)))))
      {
	 delete c;
	 scan=IterateAll(scan,p,0);
      }
      else
	 scan=IterateAll(scan,p);
   }
}

/* Mark a path as a directory or file. (We have other ways of knowing this;
 * this is the most explicit and least expensive.) */
void LsCache::SetDirectory(FileAccess *p_loc, const char *path, bool dir)
{
   if(!path)
      return;

   FileAccess::Path origdir = p_loc->GetCwd();
   FileAccess::Path new_cwd = origdir;

   new_cwd.Change(path,!dir);
   const char *entry = dir? "1":"0";
   p_loc->SetCwd(new_cwd);
   LsCache::Add(p_loc,"",FileAccess::CHANGE_DIR, dir?FA::OK:FA::NO_FILE, entry, strlen(entry));
   p_loc->SetCwd(origdir);
}

/* This is a hint function. If file type is really needed, use GetFileInfo
 * with showdir set to true. (GetFileInfo uses this function.)
 * Returns -1 if type is not known, 1 if a directory, 0 if a file. */

int LsCache::IsDirectory(FileAccess *p_loc,const char *dir_c)
{
   FileAccess::Path origdir = p_loc->GetCwd();
   FileAccess::Path new_cwd = origdir;
   new_cwd.Change(dir_c);
   p_loc->SetCwd(new_cwd);

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
      new_cwd.Change("..");
      p_loc->SetCwd(new_cwd);

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
