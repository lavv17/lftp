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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stddef.h>
#include "trio.h"
#include "History.h"
#include "url.h"
#include "misc.h"

#define super KeyValueDB

History::History()
{
   full=0;
   stamp=0;
   fd=-1;
   modified=false;
   const char *home=get_lftp_data_dir();
   if(home)
      file.vset(home,"/cwd_history",NULL);
}

History::~History()
{
   Close();
   if(full)
      delete full;
}

const char *History::extract_url(const char *res)
{
   const char *url=strchr(res,':');
   if(url)
      url++;
   else
      url=res;

   if(url::is_url(url))
      return url;

   return url::decode(url);
}

time_t History::extract_stamp(const char *res)
{
   return atol(res);
}

const char *History::Lookup(const FileAccess *s)
{
   const char *url=s->GetConnectURL(s->NO_PATH|s->NO_PASSWORD);
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
   if(!file)
      return;
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
   if(!file)
      return;
   if(fd==-1)
   {
      fd=open(file,O_RDONLY);
      if(fd==-1)
	 return;
      fcntl(fd,F_SETFD,FD_CLOEXEC);
      if(Lock(fd,F_RDLCK)==-1)
	 fprintf(stderr,"%s: lock for reading failed, trying to read anyway\n",file.get());
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

void History::Set(const FileAccess *s,const FileAccess::Path &cwd)
{
   if(cwd.path==0 || !strcmp(cwd.path,"~") || s->GetHostName()==0)
      return;
   xstring res;
   res.setf("%lu:",(unsigned long)time(0));
   if(!cwd.url)
   {
      res.append_url_encoded(cwd,URL_PATH_UNSAFE);
      if(!cwd.is_file && url::dir_needs_trailing_slash(s->GetProto()) && res.last_char()!='/')
	 res.append('/');
   }
   else
      res.append(cwd.url);
   super::Add(s->GetConnectURL(s->NO_PATH|s->NO_PASSWORD),res);
   modified=true;
}

void History::Save()
{
   Close();
   if(!file)
      return;
   if(!modified)
      return;
   fd=open(file,O_RDWR|O_CREAT,0600);
   if(fd==-1)
      return;
   fcntl(fd,F_SETFD,FD_CLOEXEC);
   if(Lock(fd,F_WRLCK)==-1)
   {
      fprintf(stderr,"%s: lock for writing failed\n",file.get());
      Close();
      return;
   }
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
   if(ftruncate(fd,0)==-1) // note the following statement
#endif
   close(open(file,O_WRONLY|O_TRUNC));

   full->Write(fd);
   fd=-1;   // Write closes file
}
