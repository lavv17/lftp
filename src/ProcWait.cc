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

/* $Id$ */

#include <config.h>
#include <sys/wait.h>
#include <errno.h>
#include "ProcWait.h"
#include "SignalHook.h"

int ProcWait::Do()
{
   int m=STALL;
   if(status!=RUNNING)
   {
   final:
      if(auto_die)
	 return WANTDIE;
      block+=NoWait();
      return m;
   }

   int info;
   int res=waitpid(pid,&info,WNOHANG|WUNTRACED);
   if(res==-1)
   {
      saved_errno=errno;
      status=ERROR;
      m=MOVED;
      goto final;
   }
   if(res==pid)
   {
      if(WIFSTOPPED(info))
      {
	 SignalHook::IncreaseCount(SIGTSTP);
      }
      else
      {
	 status=TERMINATED;
	 term_info=info;
	 m=MOVED;
	 goto final;
      }
   }
   // FIXME: smart SIGCHLD handling...
   block+=TimeOut(200);
   return m;
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
}
