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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include "history.h"
#include "xalloca.h"

#define super KeyValueDB

History::History()
{
   full=0;
   stamp=0;
   fd=-1;

   char *home=getenv("HOME");
   if(home==0)
      home=".";
   int home_len=strlen(home);
   const char *add="/.lftp/cwd_history";
   file=(char*)xmalloc(home_len+strlen(add)+1);
   strcat(strcpy(file,home),add);
}

History::~History()
{
   Close();
   if(full)
      delete full;
   xfree(file);
}

const char *History::extract_url(const char *res)
{
   const char *url=strchr(res,':');
   if(url)
      return url+1;
   else
      return res;
}

time_t History::extract_stamp(const char *res)
{
   return atol(res);
}

const char *History::Lookup(FileAccess *s)
{
   const char *url=s->GetConnectURL(s->NO_CWD);
   if(!url)
      return 0;
   const char *res=super::Lookup(url);
   if(res)
      return extract_url(res);
   Refresh();
   Close();
   if(!full)
      return 0;
   res=full->Lookup(url);
   if(res)
      return extract_url(res);
   return 0;
}

void History::Refresh()
{
   struct stat st;
   if((fd==-1 ? stat(file,&st) : fstat(fd,&st)) == -1)
      return;
   if(st.st_mtime==stamp)
      return;
   Load();
}

void History::Load()
{
   if(full)
      full->Empty();
   if(fd==-1)
   {
      fd=open(file,O_RDONLY);
      if(fd==-1)
	 return;
      Lock(fd,F_RDLCK);
      fcntl(fd,F_SETFD,FD_CLOEXEC);
   }
   if(!full)
      full=new KeyValueDB;
   struct stat st;
   fstat(fd,&st);
   stamp=st.st_mtime;
   lseek(fd,0,SEEK_SET);

   full->Read(dup(fd));	// Read	closes fd
}

void History::Close()
{
   if(fd!=-1)
   {
      close(fd);
      fd=-1;
   }
}

void History::Set(FileAccess *s,const char *cwd)
{
   if(cwd==0 || !strcmp(cwd,"~") || s->GetHostName()==0)
      return;
   char *res=(char*)alloca(32+strlen(cwd)+1);
   sprintf(res,"%lu:%s",(unsigned long)time(0),cwd);
   super::Add(s->GetConnectURL(s->NO_CWD),res);
}

void History::Save()
{
   Close();
   fd=open(file,O_RDWR|O_CREAT,0600);
   if(fd==-1)
      return;
   Lock(fd,F_WRLCK);
   fcntl(fd,F_SETFD,FD_CLOEXEC);
   Refresh();

   // merge
   int count=0;
   for(Pair *p=chain; p; p=p->next)
   {
      time_t new_stamp=extract_stamp(p->value);
      time_t old_stamp=0;
      const char *old_value=full->Lookup(p->key);
      if(old_value)
	 old_stamp=extract_stamp(old_value);
      if(old_stamp<new_stamp)
      {
	 full->Add(p->key,p->value);
      	 count++;
      }
   }

   if(count==0)
   {
      Close();
      return;
   }

   lseek(fd,0,SEEK_SET);

#ifdef HAVE_FTRUNCATE
   ftruncate(fd,0);
#else
   close(open(file,O_WRONLY|O_TRUNC));
#endif

   full->Write(fd);
   fd=-1;   // Write closes file
}
