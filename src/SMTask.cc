/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#ifdef TIME_WITH_SYS_TIME
# include <time.h>
#endif

#include "SMTask.h"

SMTask	 *SMTask::chain=0;
SMTask	 *SMTask::sched_scan=0;
SMTask	 *SMTask::current=0;
PollVec	 SMTask::sched_total;
time_t	 SMTask::now=time(0);
int	 SMTask::now_ms; // milliseconds

static int task_count=0;
static SMTask *init_task=new SMTaskInit;

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
   deleting=false;
   task_count++;
#ifdef TASK_DEBUG
   printf("new SMTask %p (count=%d)\n",this,task_count);
#endif
}

void  SMTask::Suspend() { suspended=true; }
void  SMTask::Resume()  { suspended=false; }

SMTask::~SMTask()
{
#ifdef TASK_DEBUG
   printf("delete SMTask %p (count=%d)\n",this,task_count);
#endif
   task_count--;
   assert(!running);
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

void SMTask::Delete(SMTask *task)
{
   if(!task)
      return;
   if(task->deleting)
      return;
   task->deleting=true;
   if(!task->running)
      delete task;
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

int SMTask::Roll(SMTask *task)
{
   int m=STALL;
   if(task->running)
      return m;
   task->running=true;
   while(!task->deleting && task->Do()==MOVED)
      m=MOVED;
   task->running=false;
   return m;
}

void SMTask::RollAll(int max_time)
{
   time_t time_limit=now+(now_ms+500)/1000+max_time;
   do { Schedule(); }
   while(sched_total.GetTimeout()==0
	 && (max_time==0 || now<time_limit));
}

void SMTask::Schedule()
{
   SMTask *old_current=current;
   assert(!current || current->running);

   for(sched_scan=chain; sched_scan; sched_scan=sched_scan->next)
   {
      if(!sched_scan->running)
	 sched_scan->block.Empty();
   }

   sched_total.Empty();

   // get time once and assume Do() don't take much time
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
      if(repeat)
	 sched_scan->block.SetTimeout(0); // little optimization
      int res=STALL;
      current=sched_scan;
      if(!current->deleting)
      {
	 current->running=true;
	 res=current->Do();
	 current->running=false;
      }
#if 0
      if(res==MOVED)
	 printf("MOVED: %p\n",current);
      if(!repeat && current->block.GetTimeout()==0)
	 printf("timeout==0: %p\n",current);
#endif
      if(sched_scan) // if the task called Schedule recursively, sched_scan==0.
	 sched_scan=sched_scan->next;
      if(current->deleting)
      {
	 delete current;
	 res=MOVED;
      }
      if(res==MOVED)
	 repeat=true;
      current=0;
   }
   if(repeat)
   {
      sched_total.SetTimeout(0);
      goto out;
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
out:
   current=old_current;
   return;
}

void SMTask::ReconfigAll(const char *name)
{
   UpdateNow();
   for(SMTask *scan=chain; scan; scan=scan->next)
      scan->Reconfig(name);
   sched_total.SetTimeout(0);  // for new values handling
}
void SMTask::DeleteAll()
{
   SMTask **scan=&chain;
   while(*scan)
   {
      SMTask *old=*scan;
      Delete(*scan);
      if(*scan==old)
	 scan=&(*scan)->next;
   }
}

int SMTaskInit::Do()
{
   return STALL;
}
SMTaskInit::SMTaskInit()
{
   running=true;
   current=this;
}
SMTaskInit::~SMTaskInit()
{
   running=false;
   current=0;
}

int SMTask::TaskCount()
{
   int count=0;
   for(SMTask *scan=chain; scan; scan=scan->next)
      count++;
   return count;
}
