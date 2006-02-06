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

#include <config.h>

#include "trio.h"
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
   full_name=0;
   cwd=0;
   error_text=0;
   status=0;
   close_fd=false;
}
FDStream::FDStream()
{
   fd=-1;
   name=0;
   full_name=0;
   error_text=0;
   status=0;
   close_fd=false;
}
void FDStream::MakeErrorText(int e)
{
   if(!e)
      e=errno;
   if(NonFatalError(e))
      return;  // not a serious error - can be retried
   char *syserr=strerror(e);
   error_text=(char*)xmalloc(strlen(name)+strlen(syserr)+3);
   sprintf(error_text,"%s: %s",name,syserr);
}
void FDStream::SetCwd(const char *new_cwd)
{
   xfree(cwd);
   cwd=xstrdup(new_cwd);
}
FDStream::~FDStream()
{
   if(close_fd)
      close(fd);
   if(full_name!=name)
      xfree(full_name);
   xfree(name);
   xfree(cwd);
   xfree(error_text);
};

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
      int fl=fcntl(1,F_GETFL);
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
      int fl=fcntl(0,F_GETFL);
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
	    error_text=xstrdup(second->error_text);
	 return -1;
      }
      if(pg==0)
	 pg=second->GetProcGroup();
   }

   int	 p[2];
   pid_t pid;

   if(pipe(p)==-1)
   {
      if(NonFatalError(errno))
	 return -1;
      error_text=xasprintf(_("pipe() failed: %s"),strerror(errno));
      return -1;
   }

   ProcWait::Signal(false);

   bool had_pg=(pg!=0);

   fflush(stderr);
   switch(pid=fork())
   {
   case(0): /* child */
      setpgid(0,pg);
      kill(getpid(),SIGSTOP);
      SignalHook::RestoreAll();
      Child(p);
      if(stderr_to_stdout)
	 dup2(1,2);
      if(stdout_to_null)
      {
	 close(1);
	 int null=open("/dev/null",O_RDWR);
	 if(null==-1)
	    perror("open(\"/dev/null\")");
	 else if(null==0) {
	    if(dup(0)==-1)
	       perror("dup");
	 }
      }
      if(cwd)
      {
	 if(chdir(cwd)==-1)
	 {
	    fprintf(stderr,_("chdir(%s) failed: %s\n"),cwd,strerror(errno));
	    fflush(stderr);
	    _exit(1);
	 }
      }
      if(a)
      {
	 execvp(a->a0(),a->GetVNonConst());
	 fprintf(stderr,_("execvp(%s) failed: %s\n"),a->a0(),strerror(errno));
      }
      else
      {
	 execl("/bin/sh","sh","-c",name,(char*)NULL);
	 fprintf(stderr,_("execl(/bin/sh) failed: %s\n"),strerror(errno));
      }
      fflush(stderr);
      _exit(1);
   case(-1): /* error */
      close(p[0]);
      close(p[1]);
      goto out;
   }

   if(pg==0)
      pg=pid;

   /* parent */
   Parent(p);

   fcntl(fd,F_SETFD,FD_CLOEXEC);
   fcntl(fd,F_SETFL,O_NONBLOCK);

   // wait until the child stops.
   int info;
   waitpid(pid,&info,WUNTRACED);

   w=new ProcWait(pid);

   if(had_pg)
      kill(pid,SIGCONT);
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
   cwd=xgetcwd();
   pg=0;
   closed=false;
   stderr_to_stdout=false;
   stdout_to_null=false;
   delete_second=false;
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
   delete a;

   close(fd);
   fd=-1;

   if(w)
      w->Auto();

   if(delete_second)
      delete second;
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
   {
      if(delete_second && second)
	 return second->Done();
      return true;
   }
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
void OutputFilter::Kill(int sig)
{
   if(w)
      w->Kill(sig);
   if(second)
      second->Kill(sig);
}
bool OutputFilter::usesfd(int n_fd)
{
   if(FDStream::usesfd(n_fd))
      return true;
   if(second_fd!=-1 && n_fd==second_fd)
      return true;
   if(second)
      return second->usesfd(n_fd);
   return n_fd<=2;
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
   if(name[0]=='/')
      full_name=name;
   else
   {
      cwd=xgetcwd();
      full_name=xstrdup(dir_file(cwd,name));
   }
}
FileStream::~FileStream()
{
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

off_t FileStream::get_size()
{
   struct stat st;
   if(-1==(fd==-1?stat(full_name,&st):fstat(fd,&st)))
   {
      if(errno==ENOENT)
	 return 0;   // assume non-existent files to be empty.
      return -1;
   }
   return st.st_size;
}

#include "SMTask.h"
bool FDStream::NonFatalError(int err)
{
   if(err==EDQUOT || err==ENOSPC)
   {
      struct stat st;
      if(fd>=0 && fstat(fd,&st)!=-1 && st.st_nlink==0)
	 return false;
   }
   bool non_fatal=SMTask::NonFatalError(err);
   if(non_fatal)
      set_status(strerror(err));
   else
      clear_status();
   return non_fatal;
}
