/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <sys/wait.h>
#include <errno.h>
#include "trio.h"
#include "ProcWait.h"
#include "SignalHook.h"

xmap<ProcWait*> ProcWait::all_proc;

const xstring& ProcWait::proc_key(pid_t p)
{
   static xstring tmp_key;
   tmp_key.nset((const char*)&p,sizeof(p));
   return tmp_key;
}

int ProcWait::Do()
{
   int m=STALL;
   if(status!=RUNNING)
   {
   final:
      if(auto_die)
      {
	 Delete(this);
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
   : pid(p)
{
   auto_die=false;
   status=RUNNING;
   term_info=-1;
   saved_errno=0;

   all_proc.add(proc_key(pid),this);
}

ProcWait::~ProcWait()
{
   all_proc.remove(proc_key(pid));
}

void ProcWait::Signal(bool yes)
{
   if(yes)
   {
      SignalHook::DoCount(SIGCHLD);   // select() will return -1 with EINTR
      SignalHook::Unblock(SIGCHLD);
   }
   else
      SignalHook::Block(SIGCHLD);
}

void ProcWait::DeleteAll()
{
   Signal(false);
   for(ProcWait *w=all_proc.each_begin(); w; w=all_proc.each_next())
      Delete(w);
}
