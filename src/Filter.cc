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
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <stddef.h>

#include "Filter.h"
#include "SignalHook.h"
#include "ArgV.h"
#include "misc.h"
#include "FileSet.h"
#include "ResMgr.h"
#include "log.h"

#ifndef O_BINARY
# define O_BINARY 0
#endif

FDStream::FDStream(int new_fd,const char *new_name)
   : close_when_done(false), closed(false), fd(new_fd), name(new_name?expand_home_relative(new_name):0), status(0) {}
FDStream::FDStream()
   : close_when_done(false), closed(false), fd(-1), status(0) {}

void FDStream::MakeErrorText(int e)
{
   if(!e)
      e=errno;
   if(NonFatalError(e))
      return;  // not a serious error - can be retried
   error_text.vset(name.get(),": ",strerror(e),NULL);
   revert_backup();
}
void FDStream::SetCwd(const char *new_cwd)
{
   cwd.set(new_cwd);
}
void FDStream::DoCloseFD()
{
   if(fd!=-1) {
      if(close_when_done) {
	 close(fd);
	 Log::global->Format(11,"closed FD %d\n",fd);
      }
      fd=-1;
   }
}
void FDStream::SetFD(int new_fd,bool c)
{
   DoCloseFD();
   fd=new_fd;
   close_when_done=c;
}
bool FDStream::Done()
{
   if(closed)
      return true;
   DoCloseFD();
   closed=true;
   return true;
}
FDStream::~FDStream()
{
   DoCloseFD();
};

void OutputFilter::Parent(int *p)
{
   close(p[0]);
   SetFD(p[1],true);
}

void InputFilter::Parent(int *p)
{
   close(p[1]);
   SetFD(p[0],true);
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
	    error_text.set(second->error_text);
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
      error_text.vset(_("pipe() failed: "),strerror(errno),NULL);
      return -1;
   }

   ProcWait::Signal(false);

   bool had_pg=(pg!=0);

   fflush(stdout);
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
	    fprintf(stderr,_("chdir(%s) failed: %s\n"),cwd.get(),strerror(errno));
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
	 execl("/bin/sh","sh","-c",name.get(),NULL);
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
   w=0;
   second_fd=-1;
   xgetcwd_to(cwd);
   pg=0;
   stderr_to_stdout=false;
   stdout_to_null=false;
   if(a)
      a->CombineTo(name);
}

OutputFilter::OutputFilter(const char *filter,int new_second_fd)
   : FDStream(-1,filter), second(my_second), second_fd(new_second_fd)
{
   Init();
}

OutputFilter::OutputFilter(const char *filter,FDStream *new_second)
   : FDStream(-1,filter), my_second(new_second), second(my_second)
{
   Init();
}
OutputFilter::OutputFilter(const char *filter,const Ref<FDStream>& new_second)
   : FDStream(-1,filter), second(new_second)
{
   Init();
}
OutputFilter::OutputFilter(ArgV *a1,int new_second_fd)
   : FDStream(-1,0), a(a1), second(my_second), second_fd(new_second_fd)
{
   Init();
}

OutputFilter::OutputFilter(ArgV *a1,FDStream *new_second)
   : FDStream(-1,0), a(a1), my_second(new_second), second(my_second)
{
   Init();
}
OutputFilter::OutputFilter(ArgV *a1,const Ref<FDStream>& new_second)
   : FDStream(-1,0), a(a1), second(new_second)
{
   Init();
}

OutputFilter::~OutputFilter()
{
   if(w)
      w->Auto();
}

bool OutputFilter::Done()
{
   if(!FDStream::Done())
      return false;
   if(w==0)
      return true;
   if(w->GetState()!=w->RUNNING)
   {
      if(my_second)
	 return my_second->Done();
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


#define NO_MODE ((mode_t)-1)

void FileStream::setmtime(const FileTimestamp &ts)
{
   getfd(); // this might create the file... But can fail retriably. FIXME.

   // skip the time update if the timestamp is already accurate enough.
   struct stat st;
   if(fstat(fd,&st)!=-1 && labs(st.st_mtime-ts)<=ts.ts_prec)
      return;

   struct utimbuf ut;
   ut.actime=ut.modtime=ts;
   utime(full_name,&ut);
}
FileStream::FileStream(const char *fname,int new_mode)
   : FDStream(-1,fname), mode(new_mode), create_mode(0664),
     do_lock(ResMgr::QueryBool("file:use-lock",0)),
     no_keep_backup(false),
     old_file_mode(NO_MODE)
{
   if(name[0]=='/')
      full_name.set(name);
   else
   {
      xgetcwd_to(cwd);
      full_name.set(dir_file(cwd,name));
   }
}
FileStream::~FileStream()
{
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

void FileStream::revert_backup()
{
   if(backup_file) {
      rename(backup_file,full_name);
      backup_file.unset();
   }
}
void FileStream::remove_backup()
{
   if(backup_file && (no_keep_backup || !ResMgr::QueryBool("xfer:keep-backup",0))) {
      ::remove(backup_file);
      backup_file.unset();
   }
   if(old_file_mode!=NO_MODE)
      chmod(full_name,old_file_mode);
}

int   FileStream::getfd()
{
   if(fd!=-1 || error() || closed)
      return fd;

   bool clobber=!(mode&O_EXCL);
   bool truncate=(mode&O_TRUNC);

   struct stat st;
   if((!clobber || truncate) && stat(full_name,&st)!=-1 && st.st_size>0 && S_ISREG(st.st_mode))
   {
      if(!clobber)
      {
	 error_text.vset(name.get(),": ",_("file already exists and xfer:clobber is unset"),NULL);
	 return -1;
      }
      if(truncate && ResMgr::QueryBool("xfer:make-backup",0))
      {
	 /* rename old file if exists and size>0 */
	 xstring_ca suffix(xstrftime(ResMgr::Query("xfer:backup-suffix",0),SMTask::now));
	 backup_file.vset(full_name.get(),suffix.get(),NULL);
	 if(rename(full_name,backup_file)!=0)
	    backup_file.set(0);
	 else
	    create_mode=old_file_mode=st.st_mode;
      }
   }

   int new_fd=open(full_name,mode|O_NONBLOCK|O_BINARY,create_mode);
   if(new_fd==-1)
   {
      MakeErrorText();
      return -1;
   }
   Log::global->Format(11,"opened FD %d (%s)\n",new_fd,full_name.get());
   SetFD(new_fd,true);
   fcntl(fd,F_SETFD,FD_CLOEXEC);
   if(do_lock && !(mode&O_APPEND)) {
      struct flock lk;
      lk.l_type=((mode&3)==0)?F_RDLCK:F_WRLCK;
      lk.l_whence=SEEK_SET;
      lk.l_start=0;
      lk.l_len=0;
      if(fcntl(fd,F_SETLKW,&lk)==-1) {
	 MakeErrorText();
	 DoCloseFD();
	 return -1;
      }
   }
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
