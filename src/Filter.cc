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

#include <config.h>

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "Filter.h"
#include "xmalloc.h"
#include "SignalHook.h"
#include "ArgV.h"
#include "misc.h"

#ifndef O_BINARY
# define O_BINARY 0
#endif

FDStream::FDStream(int new_fd,const char *new_name)
{
   fd=new_fd;
   if(new_name)
      name=xstrdup(expand_home_relative(new_name));
   else
      name=0;
   error_text=0;
}
FDStream::FDStream()
{
   fd=-1;
   name=0;
   error_text=0;
}
void FDStream::MakeErrorText()
{
   if(errno==ENFILE || errno==EMFILE || errno==EAGAIN || errno==EWOULDBLOCK)
      return;  // not a serious error - can be retried
   char *syserr=strerror(errno);
   error_text=(char*)xmalloc(strlen(name)+strlen(syserr)+3);
   sprintf(error_text,"%s: %s",name,syserr);
}
FDStream::~FDStream()
{
   // don't close fd
   xfree(name);
   xfree(error_text);
};

off_t FDStream::getsize_and_seek_end()
{
   int fd=getfd();
   if(fd==-1)
      return -1;
   off_t size=lseek(fd,0,SEEK_END);
   if(size<0)
      size=0;
   return size;
}

void OutputFilter::Parent(int *p)
{
   close(p[0]);
   fd=p[1];
}
void InputFilter::Parent(int *p)
{
   close(p[1]);
   fd=p[0];
}
void OutputFilter::Child(int *p)
{
   close(p[1]);
   if(p[0]!=0)
   {
      dup2(p[0],0);
      close(p[0]);
   }
   if(second_fd!=-1)
   {
      if(second_fd!=1)
      {
	 dup2(second_fd,1);
	 close(second_fd);
      }
      int fl;
      fcntl(1,F_GETFL,&fl);
      fcntl(1,F_SETFL,fl&~O_NONBLOCK);
   }
}
void InputFilter::Child(int *p)
{
   close(p[0]);
   if(p[1]!=1)
   {
      dup2(p[1],1);
      close(p[1]);
   }
   if(second_fd!=-1)
   {
      if(second_fd!=0)
      {
	 dup2(second_fd,0);
	 close(second_fd);
      }
      int fl;
      fcntl(0,F_GETFL,&fl);
      fcntl(0,F_SETFL,fl&~O_NONBLOCK);
   }
}

int OutputFilter::getfd()
{
   if(fd!=-1 || error() || closed)
      return fd;

   if(second && second_fd==-1)
   {
      second_fd=second->getfd();
      if(second_fd==-1)
      {
	 if(second->error())
	    error_text=xstrdup("chain output error");
	 return -1;
      }
      if(pg==0)
	 pg=second->GetProcGroup();
   }

   int	 p[2];
   char  s[256];
   pid_t pid;

   if(pipe(p)==-1)
   {
      if(errno==EMFILE || errno==ENFILE)
	 return -1;
      sprintf(s,"pipe() failed: %s",strerror(errno));
      error_text=xstrdup(s);
      return -1;
   }

   ProcWait::Signal(false);

   fflush(stderr);
   switch(pid=fork())
   {
   case(0): /* child */
      setpgid(0,pg);
      kill(getpid(),SIGSTOP);
      SignalHook::RestoreAll();
      Child(p);
      if(oldcwd)
      {
	 if(chdir(oldcwd)==-1)
	 {
	    fprintf(stderr,"chdir(%s) failed: %s\n",oldcwd,strerror(errno));
	    fflush(stderr);
	    _exit(1);
	 }
      }
      if(a)
	 execvp(a->a0(),a->GetV());
      execl("/bin/sh","sh","-c",name,NULL);
      fprintf(stderr,"execl(/bin/sh) failed: %s\n",strerror(errno));
      fflush(stderr);
      _exit(1);
   case(-1): /* error */
      close(p[0]);
      close(p[1]);
      goto out;
   }
   /* parent */
   Parent(p);

   if(pg==0)
      pg=pid;

   fcntl(fd,F_SETFD,FD_CLOEXEC);
   fcntl(fd,F_SETFL,O_NONBLOCK);

   xfree(oldcwd);
   oldcwd=0;

   int info;
   waitpid(pid,&info,WUNTRACED);
   w=new ProcWait(pid);
out:
   ProcWait::Signal(true);
   return fd;
}

void OutputFilter::Init()
{
   a=0;
   w=0;
   second=0;
   second_fd=-1;
   oldcwd=xgetcwd();
   pg=0;
   closed=false;
}

void OutputFilter::SetCwd(const char *cwd)
{
   xfree(oldcwd);
   oldcwd=xstrdup(cwd);
}

OutputFilter::OutputFilter(const char *filter,int new_second_fd)
   : FDStream(-1,filter)
{
   Init();
   second_fd=new_second_fd;
}

OutputFilter::OutputFilter(const char *filter,FDStream *new_second)
   : FDStream(-1,filter)
{
   Init();
   second=new_second;
}
OutputFilter::OutputFilter(ArgV *a1,int new_second_fd)
   : FDStream(-1,0)
{
   Init();
   second_fd=new_second_fd;
   a=a1;
   name=a->Combine();
}

OutputFilter::OutputFilter(ArgV *a1,FDStream *new_second)
   : FDStream(-1,0)
{
   Init();
   second=new_second;
   a=a1;
   name=a->Combine();
}

OutputFilter::~OutputFilter()
{
   // `a' is not deleted here

   close(fd);
   fd=-1;

   if(w)
      w->Auto();

   xfree(oldcwd);
}

bool OutputFilter::Done()
{
   if(w==0)
      return true;
   if(fd!=-1)
   {
      close(fd);
      fd=-1;
      closed=true;
   }
   if(w->GetState()!=w->RUNNING)
      return true;
   return false;
}
bool OutputFilter::broken()
{
   if(w==0)
      return false;
   if(fd==-1)
      return false;
   if(w->GetState()!=w->RUNNING)
      return true; // filter process terminated - pipe is broken
   return false;
}

void FileStream::setmtime(time_t t)
{
   getfd(); // this might create the file... But can fail retriably. FIXME.
   struct utimbuf ut;
   ut.actime=ut.modtime=t;
   utime(full_name,&ut);
}
FileStream::FileStream(const char *fname,int new_mode) : FDStream(-1,fname)
{
   mode=new_mode;
   full_name=0;
   if(name[0]=='/')
      full_name=name;
   else
   {
      char *cwd=xgetcwd();
      full_name=xstrdup(dir_file(cwd,name));
      xfree(cwd);
   }
}
FileStream::~FileStream()
{
   if(full_name!=name)
      xfree(full_name);
   if(fd!=-1)
      close(fd);
   fd=-1;
}
void FileStream::remove()
{
   ::remove(full_name);
}
void FileStream::remove_if_empty()
{
   if(!full_name)
      return;
   struct stat st;
   int res=stat(full_name,&st);
   if(res!=-1 && st.st_size==0)
      remove();
}

int   FileStream::getfd()
{
   if(fd!=-1 || error())
      return fd;
   fd=open(full_name,mode|O_NONBLOCK|O_BINARY,0664);
   if(fd==-1)
   {
      MakeErrorText();
      return -1;
   }
   fcntl(fd,F_SETFD,FD_CLOEXEC);
   return fd;
}

bool FileStream::can_seek()
{
   if(mode&O_APPEND)
      return false;  // whatever we seek, the writes will go to end of file.
   return true;
}
