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
#include <assert.h>
#include <sys/types.h>
#include <sys/time.h>
#ifdef TIME_WITH_SYS_TIME
# include <time.h>
#endif

#include "SMTask.h"

SMTask	 *SMTask::chain=0;
SMTask	 *SMTask::sched_scan=0;
PollVec	 SMTask::sched_total;
time_t	 SMTask::now=time(0);
int	 SMTask::now_ms; // milliseconds

SMTask::SMTask()
{
   // insert in the chain
   if(sched_scan)
   {
      // insert it so that it would be scanned next
      next=sched_scan->next;
      sched_scan->next=this;
   }
   else
   {
      next=chain;
      chain=this;
   }
   suspended=false;
   running=false;
}

void  SMTask::Suspend() { suspended=1; }
void  SMTask::Resume()  { suspended=0; }

SMTask::~SMTask()
{
   // remove from the chain
   SMTask **scan=&chain;
   while(*scan)
   {
      if(*scan==this)
      {
	 assert(this!=sched_scan);

	 *scan=next;
	 break;
      }
      scan=&((*scan)->next);
   }
}

void SMTask::UpdateNow()
{
#ifdef HAVE_GETTIMEOFDAY
   struct timeval tv;
   gettimeofday(&tv,0);
   now=tv.tv_sec;
   now_ms=tv.tv_usec/1000;
#else
   time(&now);
#endif
}

void SMTask::Schedule()
{
   // get time onec and assume Do() don't take much time
   UpdateNow();

   bool repeat=false;
   sched_scan=chain;
   while(sched_scan)
   {
      if(sched_scan->running || sched_scan->suspended)
      {
	 sched_scan=sched_scan->next;
	 continue;
      }
      sched_scan->block.Empty();
      SMTask *tmp=sched_scan;
      tmp->running=true;
      int res=tmp->Do();
      tmp->running=false;
      if(sched_scan)
	 sched_scan=sched_scan->next;
      if(res==WANTDIE)
	 delete tmp;
      else if(res==MOVED)
	 repeat=true;
   }
   sched_total.Empty();
   if(repeat)
   {
      sched_total+=NoWait();
      return;
   }

   // now collect all wake up conditions; excluding suspended and
   // already running tasks, because they can't run again on this
   // level of recursion, and thus could cause spinning by their wake up
   // conditions.
   for(sched_scan=chain; sched_scan; sched_scan=sched_scan->next)
   {
      if(!sched_scan->suspended
      && !sched_scan->running)
	 sched_total.Merge(sched_scan->block);
   }
   return;
}

void SMTask::ReconfigAll()
{
   UpdateNow();
   for(SMTask *scan=chain; scan; scan=scan->next)
      scan->Reconfig();
   sched_total+=NoWait();  // for new values handling
}
