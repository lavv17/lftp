/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>

#include "PtyShell.h"
#include "lftp_pty.h"
#include "xmalloc.h"
#include "SignalHook.h"
#include "ArgV.h"
#include "misc.h"

int PtyShell::getfd()
{
   if(fd!=-1 || error() || closed)
      return fd;

   int ptyfd,ttyfd;
   pid_t pid;

   const char *tty_name=open_pty(&ptyfd,&ttyfd);
   if(!tty_name)
   {
      if(errno==EMFILE || errno==ENFILE || E_RETRY(errno))
      	 return -1;
      char s[256];
      sprintf(s,_("pseudo-tty allocation failed: %s"),strerror(errno));
      error_text=xstrdup(s);
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
      dup2(ttyfd,0);
      dup2(ttyfd,1);
      dup2(ttyfd,2);
      if(ttyfd>2)
	 close(ttyfd);

      /* start new session */
      setsid();
      /* make the pseudo-tty our controlling tty */
#ifdef TIOCSCTTY
      /* use ioctl if available */
      ioctl(0, TIOCSCTTY, NULL);
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
	    fprintf(stderr,_("chdir(%s) failed: %s\n"),oldcwd,strerror(errno));
	    fflush(stderr);
	    _exit(1);
	 }
      }
      /* force the messages to be in english */
      putenv((char*)"LC_ALL=C");
      putenv((char*)"LANG=C");
      putenv((char*)"LANGUAGE=C");
      if(a)
	 execvp(a->a0(),a->GetV());
      execl("/bin/sh","sh","-c",name,NULL);
      fprintf(stderr,_("execl(/bin/sh) failed: %s\n"),strerror(errno));
      fflush(stderr);
      _exit(1);
   case(-1): /* error */
      close(ttyfd);
      close(ptyfd);
      goto out;
   }
   /* parent */
   close(ttyfd);
   fd=ptyfd;

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

void PtyShell::Init()
{
   a=0;
   w=0;
   oldcwd=xgetcwd();
   pg=0;
   closed=false;
}

void PtyShell::SetCwd(const char *cwd)
{
   xfree(oldcwd);
   oldcwd=xstrdup(cwd);
}

PtyShell::PtyShell(const char *filter)
   : FDStream(-1,filter)
{
   Init();
}

PtyShell::PtyShell(ArgV *a1)
   : FDStream(-1,0)
{
   Init();
   a=a1;
   name=a->Combine();
}

PtyShell::~PtyShell()
{
   delete a;
   close(fd);
   fd=-1;
   if(w)
      w->Auto();
   xfree(oldcwd);
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
