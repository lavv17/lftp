/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "trio.h"
#include "xstring.h"
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>
#include <fnmatch.h>
#include <stddef.h>

#include "FileGlob.h"
#include "misc.h"
#include "url.h"
#include "ResMgr.h"

ResDecl res_nullglob("cmd:nullglob","yes",ResMgr::BoolValidate,ResMgr::NoClosure);

// Glob implementation
Glob::Glob(FileAccess *s,const char *p)
   : FileAccessOperation(s), pattern(p)
{
   dirs_only=false;
   files_only=false;
   match_period=true;
   inhibit_tilde=true;
   casefold=false;

   if(pattern[0]=='~')
   {
      const char *slash=strchr(pattern,'/');
      if(slash)
	 inhibit_tilde=HasWildcards(xstring::get_tmp(pattern,slash-pattern));
      else
	 inhibit_tilde=HasWildcards(pattern);
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
Glob::~Glob()
{
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

   const char *s=info->name;
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
NoGlob::NoGlob(const char *p) : Glob(0,p)
{
}

void GlobURL::NewGlob(const char *p)
{
   glob=0;
   session=orig_session;

   url_prefix.set(p);
   url_prefix.truncate(url::path_index(p));

   ParsedURL p_url(p,true);
   if(p_url.proto && p_url.path)
   {
      session=my_session=FA::New(&p_url);
      if(session)
	 glob=session->MakeGlob(p_url.path);
   }
   else
   {
      glob=session->MakeGlob(p);
   }
   if(!glob)
      glob=new NoGlob(p);
   if(type==FILES_ONLY)
      glob->FilesOnly();
   else if(type==DIRS_ONLY)
      glob->DirectoriesOnly();
}

GlobURL::GlobURL(const FileAccessRef& s,const char *p,type_select t)
   : orig_session(s), session(orig_session), type(t)
{
   nullglob=ResMgr::QueryBool("cmd:nullglob",0);
   NewGlob(p);
}
GlobURL::~GlobURL() {}

FileSet *GlobURL::GetResult()
{
   FileSet &list=*glob->GetResult();
   if(list.count()==0 && !nullglob)
      list.Add(new FileInfo(glob->GetPattern()));
   if(session==orig_session)
      return &list;
   for(int i=0; list[i]; i++)
      list[i]->SetName(url_file(url_prefix,list[i]->name));
   return &list;
}

// GenericGlob implementation
GenericGlob::GenericGlob(FileAccess *s,const char *n_pattern)
   : Glob(s,n_pattern)
{
   dir_list=0;
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
      updir_glob=new GenericGlob(s,dir);
      updir_glob->DirectoriesOnly();
      updir_glob->Suspend(); // don't run now, wait for options.
   }
}

int GenericGlob::Do()
{
   int m=STALL;

   if(done)
      return m;

   if(!dir_list && updir_glob)
   {
      if(updir_glob->IsSuspended())
      {
	 // pass the options.
	 updir_glob->MatchPeriod(match_period);
	 updir_glob->InhibitTilde(inhibit_tilde);
	 updir_glob->CaseFold(casefold);
	 updir_glob->Resume();
      }
      if(updir_glob->Error())
      {
	 SetError(updir_glob->ErrorText());
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
	 set->rewind();
	 for(FileInfo *info=set->curr(); info!=NULL; info=set->next())
	 {
	    const char *name=info->name;
	    if(name[0]=='.' && name[1]=='/')
	       name+=2;
	    if(curr_dir && curr_dir[0])
	       name=dir_file(curr_dir,name);
	    info->SetName(name);
	    add(info);
	 }
	 delete set;
      }
      if(dir_list)
	 dir_list->next();
      if(!dir_list || dir_list->curr()==0)
      {
	 if(li && li->Error())
	    SetError(li->ErrorText());
	 li=0;
	 done=true;
	 return MOVED;
      }
      li=0;
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

   static xstring buf;
   buf.vset(curr_dir,": ",st,NULL);
   return buf;
}
