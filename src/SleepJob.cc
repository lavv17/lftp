/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "SleepJob.h"
#include "CmdExec.h"
#include "misc.h"

SleepJob::SleepJob(time_t when,FileAccess *s,char *what)
   : SessionJob(s)
{
   the_time=when;
   cmd=what;
   exit_code=0;
   done=false;
   saved_cwd=xgetcwd();
}
SleepJob::~SleepJob()
{
   xfree(cmd);
   xfree(saved_cwd);
}

int SleepJob::Do()
{
   if(Done())
      return STALL;

   if(waiting)
   {
      if(!waiting->Done())
	 return STALL;
      exit_code=waiting->ExitCode();
      delete waiting;
      waiting=0;
      done=true;
      return MOVED;
   }

   time_t now=time(0);
   if(now>=the_time)
   {
      if(cmd)
      {
	 CmdExec *exec=new CmdExec(session);
	 session=0;
	 exec->parent=this;
	 exec->SetCWD(saved_cwd);
	 exec->FeedCmd(cmd);
	 exec->FeedCmd("\n");
	 waiting=exec;
	 return MOVED;
      }
      done=true;
      return MOVED;
   }
   time_t diff=the_time-now;
   if(diff>1024)
      diff=1024;  // prevent overflow
   block+=TimeOut(diff*1000);
   return STALL;
}
