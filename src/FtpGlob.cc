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
#include "trio.h"
#include "xstring.h"
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

#include "xalloca.h"
#include "FileAccess.h"
#include "FtpGlob.h"
#include "FtpListInfo.h"
#include "xmalloc.h"
#include "LsCache.h"
#include "misc.h"

void FtpGlob::Init(FileAccess *session)
{
   dir=0;
   f=session;
   dir_list=0;
   updir_glob=0;
   li=0;
   base_dir=0;
}

FtpGlob::FtpGlob(FileAccess *session,const char *n_pattern)
   : Glob(n_pattern)
{
   Init(session);
   dir=xstrdup(pattern);
   char *slash=strrchr(dir,'/');
   if(!slash)
      dir[0]=0;	  // current directory
   else if(slash>dir)
      *slash=0;	  // non-root directory
   else
      dir[1]=0;	  // root directory

   if(done)
      return;

   base_dir=xstrdup(f->GetCwd());

   if(dir[0])
   {
      updir_glob=new FtpGlob(session,dir);
      updir_glob->DirectoriesOnly();
   }
}

FtpGlob::~FtpGlob()
{
   if(f)
   {
      if(base_dir)
	 f->Chdir(base_dir,false);
      f->Close();
   }
   Delete(li);
   if(!dir_list)
      xfree(dir);
   Delete(updir_glob);
   xfree(base_dir);
}

int   FtpGlob::Do()
{
   int   m=STALL;

   if(done)
      return m;

   if(!dir_list && updir_glob)
   {
      if(updir_glob->Error())
      {
	 done=true;
	 return MOVED;
      }
      if(!updir_glob->Done())
	 return m;
      dir_list=updir_glob->GetResult();
      xfree(dir);
      m=MOVED;
      if(dir_list==0 || dir_list->curr()==0)
      {
	 done=true;
	 return m;
      }
      dir=dir_list->curr()->name;
   }

   if(!li)
   {
   create_li:
      const char *c=dir;
      if(c[0]!='/' && c[0]!='~')
	 c=dir_file(base_dir,c);
      f->Chdir(c,false);
      li=f->MakeListInfo();
      li->UseCache(use_cache);
   }

   if(li->Error())
   {
      // no need for error message
   next_dir:
      if(dir_list)
	 dir_list->next();
      if(!dir_list || dir_list->curr()==0)
      {
	 f->Chdir(base_dir,false);
	 xfree(base_dir); base_dir=0;
	 done=true;
	 return MOVED;
      }
      dir=dir_list->curr()->name;
      Delete(li); li=0;
      goto create_li;
   }
   if(!li->Done())
      return m;

   FileSet *set=li->GetResult();
   set->ExcludeDots();
   set->rewind();
   for(FileInfo *info=set->curr(); info!=NULL; info=set->next())
   {
      info->SetName(dir_file(dir,info->name));
      add(info);
   }
   delete set;
   goto next_dir;
}

const char *FtpGlob::Status()
{
   if(updir_glob && !dir_list)
      return updir_glob->Status();
   if(li)
      return li->Status();
   return "";
}
