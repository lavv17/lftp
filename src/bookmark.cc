/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "bookmark.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#define super KeyValueDB

Bookmark::Bookmark()
{
   char *home=getenv("HOME");
   if(home==0)
      home=".";
   int home_len=strlen(home);
   const char *add="/.lftp/bookmarks";
   bm_file=(char*)xmalloc(home_len+strlen(add)+1);
   strcat(strcpy(bm_file,home),add);

   bm_fd=-1;
   stamp=(time_t)-1;
}

Bookmark::~Bookmark()
{
   Close();
   xfree(bm_file);
}

void Bookmark::Refresh()
{
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
   if(bm_fd==-1)
   {
      bm_fd=open(bm_file,O_RDONLY);
      if(bm_fd==-1)
	 return;
      Lock(F_RDLCK);
      fcntl(bm_fd,F_SETFD,FD_CLOEXEC);
   }
   struct stat st;
   fstat(bm_fd,&st);
   stamp=st.st_mtime;
   lseek(bm_fd,0,SEEK_SET);
   super::Read(dup(bm_fd));   // Read closes file
}

void Bookmark::PreModify()
{
   Close();
   bm_fd=open(bm_file,O_RDWR|O_CREAT,0600);
   if(bm_fd==-1)
      return;
   Lock(F_WRLCK);
   fcntl(bm_fd,F_SETFD,FD_CLOEXEC);
   Refresh();
}

void Bookmark::PostModify()
{
   Lock(F_WRLCK);
   lseek(bm_fd,0,SEEK_SET);

#ifdef HAVE_FTRUNCATE
   ftruncate(bm_fd,0);
#else
   close(open(bm_file,O_WRONLY|O_TRUNC));
#endif

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

const char *Bookmark::Lookup(const char *key)
{
   Refresh();
   Close();
   return super::Lookup(key);
}

void Bookmark::List()
{
   Refresh();
   Close();
   super::Write(dup(0));
}
char *Bookmark::Format()
{
   Refresh();
   Close();
   return super::Format();
}
