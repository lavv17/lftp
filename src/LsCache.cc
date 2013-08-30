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
#include <assert.h>
#include "FileAccess.h"
#include "LsCache.h"
#include "plural.h"
#include "misc.h"

int LsCacheEntry::EstimateSize() const
{
   int size=sizeof(*this);
   size+=LsCacheEntryLoc::EstimateSize();
   size+=LsCacheEntryData::EstimateSize();
   return size;
}
LsCacheEntryLoc::LsCacheEntryLoc(const FileAccess *p_loc,const char *a,int m)
   : arg(a), mode(m)
{
   loc=p_loc->Clone();
   loc->Suspend();
}
const char *LsCacheEntryLoc::GetClosure() const
{
   return loc->GetHostName();
}
LsCacheEntryData::LsCacheEntryData(int e,const char *d,int l,const FileSet *fs)
{
   SetData(e,d,l,fs);
}

LsCacheEntry::LsCacheEntry(const FileAccess *p_loc,const char *a,int m,int e,const char *d,int l,const FileSet *fs)
   : LsCacheEntryLoc(p_loc,a,m), LsCacheEntryData(e,d,l,fs)
{
   SetResource(e==FA::OK?"cache:expire":"cache:expire-negative",GetClosure());
}

void LsCacheEntryData::SetData(int e,const char *d,int l,const FileSet *fs)
{
   afset=fs?new FileSet(fs):0;
   data.nset(d,l);
   err_code=e;
}
void LsCacheEntryData::GetData(int *e,const char **d,int *l,const FileSet **fs)
{
   if(d && l)
   {
      *d=data;
      *l=data.length();
   }
   if(fs)
      *fs=afset;
   *e=err_code;
}
bool LsCacheEntryLoc::Matches(const FileAccess *p_loc,const char *a,int m)
{
   return (m==-1 || mode==m) && arg.eq(a) && p_loc->SameLocationAs(loc);
}

ResDecl res_cache_empty_listings("cache:cache-empty-listings","no",ResMgr::BoolValidate,0);
ResDecl res_cache_enable("cache:enable","yes",ResMgr::BoolValidate,0);
ResDecl res_cache_expire("cache:expire","60m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_expire_neg("cache:expire-negative","1m",ResMgr::TimeIntervalValidate,0);
ResDecl res_cache_size  ("cache:size","16M",ResMgr::UNumberValidate,ResMgr::NoClosure);

LsCache::LsCache() : Cache(&res_cache_size,&res_cache_enable) {}

void LsCache::Add(const FileAccess *p_loc,const char *a,int m,int e,const char *d,int l,const FileSet *fs)
{
   if(!strcmp(p_loc->GetProto(),"file"))
      return;  // don't cache local objects
   if(l == 0 &&
	 !res_cache_empty_listings.QueryBool(p_loc->GetHostName()))
      return;
   if(e!=FA::OK && e!=FA::NO_FILE && e!=FA::NOT_SUPP)
      return;

   Trim();

   LsCacheEntry *c=Find(p_loc,a,m);
   if(!c)
   {
      if(!IsEnabled(p_loc->GetHostName()))
	 return;
      AddCacheEntry(new LsCacheEntry(p_loc,a,m,e,d,l,fs));
   }
   else
   {
      c->SetData(e,d,l,fs);
   }
}

void LsCache::Add(const FileAccess *p_loc,const char *a,int m,int e,const Buffer *ubuf,const FileSet *fs)
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

LsCacheEntry *LsCache::Find(const FileAccess *p_loc,const char *a,int m)
{
   if(!IsEnabled(p_loc->GetHostName()))
      return 0;

   LsCacheEntry *c;
   for(c=IterateFirst(); c; c=IterateNext())
   {
      if(c->Matches(p_loc,a,m))
	 break;
   }
   if(c && c->Stopped())
   {
      Trim();
      return 0;
   }
   return c;
}

bool LsCache::Find(const FileAccess *p_loc,const char *a,int m,int *e,const char **d,int *l,const FileSet **fs)
{
   LsCacheEntry *c=Find(p_loc,a,m);
   if(!c)
      return false;
   c->GetData(e,d,l,fs);
   return true;
}

const FileSet *LsCache::FindFileSet(const FileAccess *p_loc,const char *a,int m)
{
   LsCacheEntry *c=Find(p_loc,a,m);
   if(!c)
      return 0;
   return c->GetFileSet(c->loc);
}
const FileSet *LsCacheEntryData::GetFileSet(const FileAccess *parser)
{
   if(afset)
      return afset;
   if(err_code!=FA::OK)
      return 0;
   afset=parser->ParseLongList(data, data.length());
   return afset;
}

void LsCache::UpdateFileSet(const FileAccess *p_loc,const char *a,int m,const FileSet *fs)
{
   if(!fs)
      return;
   LsCacheEntry *c=Find(p_loc,a,m);
   if(!c)
      return;
   c->UpdateFileSet(fs);
}

void LsCache::List()
{
   Trim();

   long vol=0;
   for(LsCacheEntry *c=IterateFirst(); c; c=IterateNext())
      vol+=c->EstimateSize();

   printf(plural("%ld $#l#byte|bytes$ cached",vol),vol);

   long sizelimit=res_cache_size.Query(0);
   if(sizelimit<0)
      puts(_(", no size limit"));
   else
      printf(_(", maximum size %ld\n"),sizelimit);
}

void LsCache::Changed(change_mode m,const FileAccess *f,const char *dir)
{
   xstring fdir(dir_file(f->GetCwd(),dir));
   if(m==FILE_CHANGED)
      dirname_modify(fdir);

   LsCacheEntry *c=IterateFirst();
   while(c)
   {
      const FileAccess *sloc=c->loc;
      if(f->SameLocationAs(sloc) || (f->SameSiteAs(sloc)
	       && (m==TREE_CHANGED?
		     !strncmp(fdir,dir_file(sloc->GetCwd(),c->arg),fdir.length())
		   : !strcmp (fdir,dir_file(sloc->GetCwd(),c->arg)))))
	 c=IterateDelete();
      else
	 c=IterateNext();
   }
}

/* Mark a path as a directory or file. (We have other ways of knowing this;
 * this is the most explicit and least expensive.) */
void LsCache::SetDirectory(const FileAccess *p_loc, const char *path, bool dir)
{
   if(!path)
      return;

   FileAccess::Path new_cwd = p_loc->GetCwd();
   new_cwd.Change(path,!dir);
   SMTaskRef<FileAccess> new_p_loc(p_loc->Clone());
   new_p_loc->SetCwd(new_cwd);

   const char *entry = dir? "1":"0";
   LsCache::Add(new_p_loc,"",FileAccess::CHANGE_DIR, dir?FA::OK:FA::NO_FILE, entry, strlen(entry));
}

/* This is a hint function. If file type is really needed, use GetFileInfo
 * with showdir set to true. (GetFileInfo uses this function.)
 * Returns -1 if type is not known, 1 if a directory, 0 if a file. */

int LsCache::IsDirectory(const FileAccess *p_loc,const char *dir_c)
{
   FileAccess::Path new_cwd(p_loc->GetCwd());
   new_cwd.Change(dir_c);
   FileAccessRef new_p_loc(p_loc->Clone());
   new_p_loc->SetCwd(new_cwd);

   int ret = -1;

   /* Cheap tests first:
    *
    * First, we know the path is a directory or not if we have an expicit
    * CHANGE_DIR entry for it. */
   const char *buf_c;
   int bufsiz;
   int e;
   if(Find(new_p_loc, "", FileAccess::CHANGE_DIR, &e, &buf_c,&bufsiz))
   {
      assert(bufsiz==1);
      return (e==FA::OK);
   }

   /* We know the path is a directory if we have a cache entry for it.  This is
    * true regardless of the list type.  (Unless it's a CHANGE_DIR entry; do this
    * test after the CHANGE_DIR check.) */
   if(Find(new_p_loc, "", FA::LONG_LIST, &e, 0,0))
      return(e==FA::OK);
   if(Find(new_p_loc, "", FA::MP_LIST, &e, 0,0))
      return(e==FA::OK);
   if(Find(new_p_loc, "", FA::LIST, &e, 0,0))
      return(e==FA::OK);

   /* We know this is a file or a directory if the dirname is cached and
    * contains the basename. */
   {
      const char *bn=basename_ptr(new_cwd.path);
      bn=alloca_strdup(bn); // save basename

      new_cwd.Change("..");
      new_p_loc->SetCwd(new_cwd);

      const FileSet *fs=FindFileSet(new_p_loc, "", FA::MP_LIST);
      if(!fs)
	 fs=FindFileSet(new_p_loc, "", FA::LONG_LIST);
      if(fs)
      {
	 FileInfo *fi=fs->FindByName(bn);
	 if(fi && (fi->defined&fi->TYPE))
	    return(fi->filetype == fi->DIRECTORY);
      }
   }

   return ret;
}
