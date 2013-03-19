/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "bookmark.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include "trio.h"
#include "ResMgr.h"
#include "misc.h"
#include "url.h"

#define super KeyValueDB

Bookmark lftp_bookmarks;

ResDecl res_auto_sync("bmk:auto-sync","yes",ResMgr::BoolValidate,ResMgr::NoClosure);

Bookmark::Bookmark()
{
   const char *home=get_lftp_data_dir();
   if(home)
      bm_file.vset(home,"/bookmarks",NULL);
   bm_fd=-1;
   stamp=(time_t)-1;
}

Bookmark::~Bookmark()
{
   Close();
}

void Bookmark::Refresh()
{
   if(!bm_file)
      return;
   struct stat st;
   if((bm_fd==-1 ? stat(bm_file,&st) : fstat(bm_fd,&st)) == -1)
      return;
   if(st.st_mtime==stamp)
      return;
   Load();
}

void Bookmark::Load()
{
   super::Empty();
   if(!bm_file)
      return;
   if(bm_fd==-1)
   {
      bm_fd=open(bm_file,O_RDONLY);
      if(bm_fd==-1)
	 return;
      fcntl(bm_fd,F_SETFD,FD_CLOEXEC);
      if(Lock(F_RDLCK)==-1)
	 fprintf(stderr,"%s: lock for reading failed, trying to read anyway\n",bm_file.get());
   }
   struct stat st;
   fstat(bm_fd,&st);
   stamp=st.st_mtime;
   lseek(bm_fd,0,SEEK_SET);
   super::Read(dup(bm_fd));   // Read closes file
}


static bool auto_sync;
void Bookmark::PreModify()
{
   if(!bm_file)
      return;

   auto_sync=ResMgr::QueryBool("bmk:auto-sync",0);
   if(!auto_sync)
      return;

   Close();
   bm_fd=open(bm_file,O_RDWR|O_CREAT,0600);
   if(bm_fd==-1)
      return;
   if(Lock(F_WRLCK)==-1)
   {
      fprintf(stderr,"%s: lock for writing failed\n",bm_file.get());
      Close();
      return;
   }
   Refresh();
}
void Bookmark::PostModify()
{
   if(!bm_file)
      return;

   if(!auto_sync)
      return;

   // the file is already locked in PreModify.
   lseek(bm_fd,0,SEEK_SET);

#ifdef HAVE_FTRUNCATE
   if(ftruncate(bm_fd,0)==-1) // note the following statement
#endif
   close(open(bm_file,O_WRONLY|O_TRUNC));

   super::Write(bm_fd);
   bm_fd=-1;   // Write closes file
}

void Bookmark::UserSave()
{
   if(!bm_file)
      return;
   Close();
   bm_fd=open(bm_file,O_RDWR|O_CREAT|O_TRUNC,0600);
   if(bm_fd==-1)
      return;
   if(Lock(F_WRLCK)==-1)
   {
      fprintf(stderr,"%s: lock for writing failed\n",bm_file.get());
      Close();
      return;
   }
   super::Write(bm_fd);
   bm_fd=-1;   // Write closes file
}

void Bookmark::Add(const char *key,const char *value)
{
   PreModify();
   super::Add(key,value);
   PostModify();
}

void Bookmark::Remove(const char *key)
{
   PreModify();
   super::Remove(key);
   PostModify();
}

void Bookmark::Close()
{
   if(bm_fd!=-1)
   {
      close(bm_fd);
      bm_fd=-1;
   }
}

void Bookmark::AutoSync()
{
   if(ResMgr::QueryBool("bmk:auto-sync",0))
   {
      Refresh();
      Close();
   }
}

const char *Bookmark::Lookup(const char *key)
{
   AutoSync();
   return super::Lookup(key);
}

#if 0
void Bookmark::List()
{
   Refresh();
   Close();
   super::Write(dup(0));
}
#endif

char *Bookmark::Format()
{
   AutoSync();
   return super::Format();
}

char *Bookmark::FormatHidePasswords()
{
   AutoSync();
   return super::Format(url::hide_password);
}
