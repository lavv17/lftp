/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include "xstring.h"
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#define _GNU_SOURCE
#include <fnmatch.h>

#include "xalloca.h"
#include "Glob.h"
#include "xmalloc.h"
#include "misc.h"
#include "url.h"

// Glob implementation
Glob::~Glob()
{
   xfree(pattern);
}

Glob::Glob(const char *p)
{
   pattern=xstrdup(p);
   dirs_only=false;
   files_only=false;
   match_period=true;
   inhibit_tilde=true;
   casefold=false;

   if(pattern[0]=='~')
   {
      char *slash=strchr(pattern,'/');
      if(slash)
      {
	 *slash=0;
	 inhibit_tilde=HasWildcards(pattern);
	 *slash='/';
      }
      else
	 inhibit_tilde=HasWildcards(pattern);;
   }
   if(pattern[0] && !HasWildcards(pattern))
   {
      // no need to glob, just unquote
      char *u=alloca_strdup(pattern);
      UnquoteWildcards(u);
      add(new FileInfo(u));
      done=true;
   }
}

void Glob::add_force(const FileInfo *info)
{
   // insert new file name into list
   list.Add(new FileInfo(*info));
}
void Glob::add(const FileInfo *info)
{
   if(info->defined&info->TYPE)
   {
      if(dirs_only && info->filetype==info->NORMAL)
	 return;   // note that symlinks can point to directories,
		   // so skip normal files only.
      if(files_only && info->filetype==info->DIRECTORY)
	 return;
   }

   char *s=info->name;
   if(s==0)
      return;

   int flags=FNM_PATHNAME;
   if(match_period)
      flags|=FNM_PERIOD;

   if(casefold)
      flags|=FNM_CASEFOLD;

   if(pattern[0]!=0
   && fnmatch(pattern, s, flags)!=0)
      return; // unmatched

   if(s[0]=='~' && inhibit_tilde)
   {
      char *new_name=alloca_strdup2(s,2);
      strcpy(new_name,"./");
      strcat(new_name,s);
      FileInfo new_info(*info);
      new_info.SetName(new_name);
      add_force(&new_info);
   }
   else
   {
      add_force(info);
   }
}

bool Glob::HasWildcards(const char *s)
{
   while(*s)
   {
      switch(*s)
      {
      case '\\':
	 if(s[1])
	    s++;
	 break;
      case '*':
      case '[':
      case ']':
      case '?':
	 return true;
      }
      s++;
   }
   return false;
}

void Glob::UnquoteWildcards(char *s)
{
   char *store=s;
   for(;;)
   {
      if(*s=='\\')
      {
	 if(s[1]=='*'
	 || s[1]=='['
	 || s[1]==']'
	 || s[1]=='?'
	 || s[1]=='\\')
	    s++;
      }
      *store=*s;
      if(*s==0)
	 break;
      s++;
      store++;
   }
}

int NoGlob::Do()
{
   if(!done)
   {
      if(!HasWildcards(pattern))
      {
	 char *p=alloca_strdup(pattern);
	 UnquoteWildcards(p);
	 add(new FileInfo(p));
      }
      done=true;
      return MOVED;
   }
   return STALL;
}
NoGlob::NoGlob(const char *p) : Glob(p)
{
}

GlobURL::GlobURL(FileAccess *s,const char *p)
{
   session=s;
   reuse=false;
   glob=0;
   url_prefix=xstrdup(p);
   url_prefix[url::path_index(p)]=0;

   ParsedURL p_url(p,true);
   if(p_url.proto && p_url.path)
   {
      session=FA::New(&p_url);
      if(session)
      {
	 glob=session->MakeGlob(p_url.path);
	 reuse=true;
      }
   }
   else
   {
      glob=session->MakeGlob(p);
   }
   if(!glob)
      glob=new NoGlob(p);
}
GlobURL::~GlobURL()
{
   SMTask::Delete(glob);
   if(session && reuse)
      SessionPool::Reuse(session);
   xfree(url_prefix);
}
FileSet *GlobURL::GetResult()
{
   FileSet &list=*glob->GetResult();
   if(!reuse)
      return &list;
   for(int i=0; list[i]; i++)
      list[i]->SetName(url_file(url_prefix,list[i]->name));
   return &list;
}

// GenericGlob implementation
GenericGlob::GenericGlob(FileAccess *s,const char *n_pattern)
   : Glob(n_pattern)
{
   session=s;
   dir_list=0;
   updir_glob=0;
   li=0;
   curr_dir=0;

   if(done)
      return;

   char *dir=alloca_strdup(pattern);
   char *slash=strrchr(dir,'/');
   if(!slash)
      dir=0;
   else if(slash>dir)
      *slash=0;	  // non-root directory
   else
      dir[1]=0;	  // root directory

   if(dir)
   {
      updir_glob=new GenericGlob(session,dir);
      updir_glob->DirectoriesOnly();
   }
}

GenericGlob::~GenericGlob()
{
   Delete(li);
   Delete(updir_glob);
}

int GenericGlob::Do()
{
   int m=STALL;

   if(done)
      return m;

   if(!dir_list && updir_glob)
   {
      if(updir_glob->Error())
      {
	 Delete(updir_glob);
	 updir_glob=0;
	 done=true;
	 return MOVED;
      }
      if(!updir_glob->Done())
	 return m;
      dir_list=updir_glob->GetResult();
      dir_list->rewind();
      m=MOVED;
      if(dir_list==0 || dir_list->curr()==0)
      {
	 done=true;
	 return m;
      }
      curr_dir=dir_list->curr()->name;
   }

   if(li)
   {
      if(!li->Done() && !li->Error())
	 return m;

      if(li->Done() && !li->Error())
      {
	 FileSet *set=li->GetResult();
	 set->ExcludeDots();
	 set->rewind();
	 for(FileInfo *info=set->curr(); info!=NULL; info=set->next())
	 {
	    info->SetName(dir_file(curr_dir,info->name));
	    add(info);
	 }
	 delete set;
      }
      Delete(li); li=0;

      if(dir_list)
	 dir_list->next();
      if(!dir_list || dir_list->curr()==0)
      {
	 done=true;
	 return MOVED;
      }
      curr_dir=dir_list->curr()->name;
   }

   li=session->MakeListInfo(curr_dir);
   if(!li)
   {
      // Cannot glob. Just unquote wildcards.
      char *p=alloca_strdup(pattern);
      UnquoteWildcards(p);
      add(new FileInfo(p));
      done=true;
      return MOVED;
   }
   li->UseCache(use_cache);
   return MOVED;
}

const char *GenericGlob::Status()
{
   if(updir_glob && !dir_list)
      return updir_glob->Status();
   if(!li)
      return "";

   const char *st = li->Status();
   if(!*st)
      return "";

   if(!curr_dir)
      return st;

   static char *buf = 0;
   if(buf) xfree(buf);
   buf=xasprintf("%s: %s", curr_dir, st);
   return buf;
}
