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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif
#include <termios.h>
#include <stddef.h>

#include "PtyShell.h"
#include "lftp_pty.h"
#include "SignalHook.h"
#include "ArgV.h"
#include "misc.h"

int PtyShell::getfd()
{
   if(fd!=-1 || error() || closed)
      return fd;

   int ptyfd,ttyfd;
   pid_t pid;
   int pipe0[2];
   int pipe1[2];

   if(use_pipes)
   {
      if(pipe(pipe0)<0)
	 return -1;
      if(pipe(pipe1)<0)
      {
	 close(pipe0[0]);
	 close(pipe0[1]);
	 return -1;
      }
   }

   const char *tty_name=open_pty(&ptyfd,&ttyfd);
   if(!tty_name)
   {
      if(!NonFatalError(errno))
	 error_text.vset(_("pseudo-tty allocation failed: "),strerror(errno),NULL);
      if(use_pipes)
      {
	 close(pipe0[0]);
	 close(pipe0[1]);
	 close(pipe1[0]);
	 close(pipe1[1]);
      }
      return -1;
   }

   struct termios tc;
   tcgetattr(ttyfd,&tc);
   tc.c_lflag=0;
   tc.c_oflag=0;
   tc.c_iflag=0;
   tc.c_cc[VMIN]=1;
   tc.c_cc[VTIME]=0;
   tcsetattr(ttyfd,TCSANOW,&tc);

   ProcWait::Signal(false);

   fflush(stderr);
   switch(pid=fork())
   {
   case(0): /* child */
      close(ptyfd);
      if(use_pipes)
      {
	 close(pipe0[1]);
	 close(pipe1[0]);
	 dup2(pipe0[0],0);
	 dup2(pipe1[1],1);
	 if(pipe0[0]>2)
	    close(pipe0[0]);
	 if(pipe1[1]>2)
	    close(pipe1[1]);
      }
      else
      {
	 dup2(ttyfd,0);
	 dup2(ttyfd,1);
      }
      dup2(ttyfd,2);
      if(ttyfd>2)
	 close(ttyfd);

      /* start new session */
      setsid();
      /* make the pseudo-tty our controlling tty */
#ifdef TIOCSCTTY
      /* use ioctl if available. FD 2 is tty even if use_pipes==true */
      ioctl(2, TIOCSCTTY, NULL);
#else
      /* otherwise open the tty without O_NOCTTY flag */
      ttyfd=open(tty_name, O_RDWR);
      if(ttyfd>=0)
	 close(ttyfd);
#endif

      SignalHook::RestoreAll();
      kill(getpid(),SIGSTOP);

      if(oldcwd)
      {
	 if(chdir(oldcwd)==-1)
	 {
	    fprintf(stderr,_("chdir(%s) failed: %s\n"),oldcwd.get(),strerror(errno));
	    fflush(stderr);
	    _exit(1);
	 }
      }
      /* force the messages to be in english */
      putenv((char*)"LC_ALL=C");
      putenv((char*)"LANG=C");
      putenv((char*)"LANGUAGE=C");
      if(a)
	 execvp(a->a0(),a->GetVNonConst());
      execl("/bin/sh","sh","-c",name.get(),NULL);
      fprintf(stderr,_("execl(/bin/sh) failed: %s\n"),strerror(errno));
      fflush(stderr);
      _exit(1);
   case(-1): /* error */
      close(ttyfd);
      close(ptyfd);
      if(use_pipes)
      {
	 close(pipe0[0]);
	 close(pipe0[1]);
	 close(pipe1[0]);
	 close(pipe1[1]);
      }
      goto out;
   }
   /* parent */
   if(pg==0)
      pg=pid;

   close(ttyfd);
   fd=ptyfd;

   fcntl(fd,F_SETFD,FD_CLOEXEC);
   fcntl(fd,F_SETFL,O_NONBLOCK);

   if(use_pipes)
   {
      close(pipe0[0]);
      pipe_out=pipe0[1];
      close(pipe1[1]);
      pipe_in=pipe1[0];
      fcntl(pipe_in,F_SETFD,FD_CLOEXEC);
      fcntl(pipe_in,F_SETFL,O_NONBLOCK);
      fcntl(pipe_out,F_SETFD,FD_CLOEXEC);
      fcntl(pipe_out,F_SETFL,O_NONBLOCK);
   }

   oldcwd.set(0);

   int info;
   waitpid(pid,&info,WUNTRACED);
   w=new ProcWait(pid);
out:
   ProcWait::Signal(true);
   return fd;
}

void PtyShell::Init()
{
   xgetcwd_to(oldcwd);
   pg=0;
   closed=false;
   use_pipes=false;
   pipe_in=-1;
   pipe_out=-1;
}

void PtyShell::SetCwd(const char *cwd)
{
   oldcwd.set(cwd);
}

PtyShell::PtyShell(const char *filter)
   : FDStream(-1,filter)
{
   Init();
}

PtyShell::PtyShell(ArgV *a1)
   : FDStream(-1,0), a(a1)
{
   Init();
   a->CombineTo(name);
}

PtyShell::~PtyShell()
{
   if(fd!=-1)
      close(fd);
   if(pipe_in!=-1)
      close(pipe_in);
   if(pipe_out!=-1)
      close(pipe_out);
   if(w) {
      w->Kill();
      w.borrow()->Auto();
   }
}

bool PtyShell::Done()
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
bool PtyShell::broken()
{
   if(w==0)
      return false;
   if(fd==-1)
      return false;
   if(w->GetState()!=w->RUNNING)
      return true; // filter process terminated - pipe is broken
   return false;
}
