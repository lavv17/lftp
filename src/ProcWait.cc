/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: ProcWait.cc,v 1.19 2008/11/27 05:56:24 lav Exp $ */

#include <config.h>
#include <sys/wait.h>
#include <errno.h>
#include "trio.h"
#include "ProcWait.h"
#include "SignalHook.h"

ProcWait *ProcWait::chain=0;

int ProcWait::Do()
{
   int m=STALL;
   if(status!=RUNNING)
   {
   final:
      if(auto_die)
      {
	 deleting=true;
	 return MOVED;
      }
      return m;
   }

   int info;
   int res=waitpid(pid,&info,WNOHANG|WUNTRACED);
   if(res==-1)
   {
      if(status!=RUNNING)
	 return MOVED;
      // waitpid failed, check the process existence
      if(kill(pid,0)==-1)
      {
	 status=TERMINATED;
	 term_info=255;
	 m=MOVED;
	 goto final;
      }
      goto leave;
   }
   if(res==pid)
   {
      if(handle_info(info))
      {
	 m=MOVED;
	 goto final;
      }
   }
leave:
   Timeout(500); // check from time to time, in case SIGCHLD fails
   return m;
}

bool ProcWait::handle_info(int info)
{
   if(WIFSTOPPED(info))
   {
      SignalHook::IncreaseCount(SIGTSTP);
      return false;
   }
   else
   {
      if(WIFSIGNALED(info) && WTERMSIG(info)==SIGINT)
	 SignalHook::IncreaseCount(SIGINT);
      status=TERMINATED;
      term_info=info;
      return true;
   }
}

int ProcWait::Kill(int sig)
{
   Do();
   if(status!=RUNNING)
      return -1;

   int res;
   res=kill(-pid,sig);
   if(res==-1)
      res=kill(pid,sig);
   return res;
}

ProcWait::ProcWait(pid_t p)
{
   auto_die=false;
   pid=p;
   status=RUNNING;
   term_info=-1;
   saved_errno=0;

   next=chain;
   chain=this;
}

ProcWait::~ProcWait()
{
   for(ProcWait **scan=&chain; *scan; scan=&(*scan)->next)
   {
      if(*scan==this)
      {
	 *scan=next;
	 return;
      }
   }
}

void ProcWait::SIGCHLD_handler(int sig)
{
   (void)sig;
   int info;
   pid_t pp=waitpid(-1,&info,WUNTRACED|WNOHANG);
   if(pp==-1)
      return;
   for(ProcWait *scan=chain; scan; scan=scan->next)
   {
      if(scan->pid==pp)
      {
	 scan->handle_info(info);
	 return;
      }
   }
   // no WaitProc for the pid. Probably the process died too fast,
   // but next waitpid should take care of it.
}

void ProcWait::Signal(bool yes)
{
   if(yes)
   {
      SignalHook::Handle(SIGCHLD,&ProcWait::SIGCHLD_handler);
      SignalHook::Unblock(SIGCHLD);
   }
   else
      SignalHook::Block(SIGCHLD);
}

void ProcWait::DeleteAll()
{
   Signal(false);
   for(ProcWait *scan=chain; scan; scan=scan->next)
      scan->deleting=true;
}
