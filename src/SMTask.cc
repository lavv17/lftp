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
#include <time.h>
#include "trio.h"
#ifdef TIME_WITH_SYS_TIME
# include <sys/types.h>
# include <sys/time.h>
#endif

#include "SMTask.h"
#include "misc.h"

SMTask	 *SMTask::chain=0;
SMTask	 *SMTask::current=0;
SMTask	 **SMTask::stack=0;
int      SMTask::stack_ptr=0;
int      SMTask::stack_size=0;
PollVec	 SMTask::sched_total;
TimeDate SMTask::now;

static int task_count=0;
static SMTask *init_task=new SMTaskInit;

SMTask::SMTask()
{
   // insert in the chain
   if(current)
   {
      // insert it so that it would be scanned next
      next=current->next;
      current->next=this;
   }
   else
   {
      next=chain;
      chain=this;
   }
   suspended=false;
   running=0;
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

void SMTask::Enter(SMTask *task)
{
   if(stack_size<=stack_ptr)
      stack=(SMTask**)xrealloc(stack,(stack_size+=16)*sizeof(*stack));
   stack[stack_ptr++]=current;
   current=task;
   current->running++;
}
void SMTask::Leave(SMTask *task)
{
   assert(current==task);
   current->running--;
   current=stack[--stack_ptr];
}

int SMTask::Roll(SMTask *task)
{
   int m=STALL;
   if(task->running || task->deleting)
      return m;
   Enter(task);
   while(!task->deleting && task->Do()==MOVED)
      m=MOVED;
   Leave(task);
   return m;
}

void SMTask::RollAll(int max_time)
{
   time_t time_limit=now.UnixTime()+max_time;
   do { Schedule(); }
   while(sched_total.GetTimeout()==0
	 && (max_time==0 || now<time_limit));
}

void SMTask::Schedule()
{
   SMTask *scan;

   for(scan=chain; scan; scan=scan->next)
   {
      if(!scan->running)
	 scan->block.Empty();
   }
   sched_total.Empty();

   // get time once and assume Do() don't take much time
   UpdateNow();

   bool repeat=false;
   scan=chain;
   while(scan)
   {
      if(scan->running || scan->suspended)
      {
	 scan=scan->next;
	 continue;
      }
      if(repeat)
	 scan->block.SetTimeout(0); // little optimization

      int res=STALL;

      Enter(scan);	// mark it current and running.
      SMTask *entered=current;
      if(!current->deleting)
	 res=current->Do(); // let it run unless it is dying.
      assert(current==entered);
      scan=scan->next;	// move to a next task.
      SMTask *to_delete=0;
      if(current->deleting)
	 to_delete=current;
      if(res!=MOVED && current->block.GetTimeout()==0)
	 res=MOVED;
#if 0
      if(res==MOVED)
	 printf("MOVED:%p\n",current);
#endif
      Leave(current);	// unmark it running and change current.

      delete to_delete;
      if(res==MOVED || to_delete)
	 repeat=true;
   }
   if(repeat)
   {
      sched_total.SetTimeout(0);
      return;
   }

   // now collect all wake up conditions; excluding suspended and
   // already running tasks, because they can't run again on this
   // level of recursion, and thus could cause spinning by their wake up
   // conditions.
   for(scan=chain; scan; scan=scan->next)
   {
      if(!scan->suspended && !scan->running && !scan->block.IsEmpty())
	 sched_total.Merge(scan->block);
   }
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
   Enter();
}
SMTaskInit::~SMTaskInit()
{
   Leave();
}

int SMTask::TaskCount()
{
   int count=0;
   for(SMTask *scan=chain; scan; scan=scan->next)
      count++;
   return count;
}

void SMTask::Cleanup()
{
  delete init_task;
}

#include <errno.h>
#include "ResMgr.h"
ResDecl enospc_fatal ("xfer:disk-full-fatal","no",ResMgr::BoolValidate,ResMgr::NoClosure);
bool SMTask::NonFatalError(int err)
{
   if(E_RETRY(err))
      return true;

   current->TimeoutS(1);
   if(err==ENFILE || err==EMFILE)
      return true;
#ifdef ENOBUFS
   if(err==ENOBUFS)
      return true;
#endif
#ifdef ENOSR
   if(err==ENOSR)
      return true;
#endif
#ifdef ENOSPC
   if(err==ENOSPC)
      return !enospc_fatal.QueryBool(0);
#endif
#ifdef EDQUOT
   if(err==EDQUOT)
      return !enospc_fatal.QueryBool(0);
#endif

   current->Timeout(0);
   return false; /* fatal error */
}

bool SMTask::TemporaryNetworkError(int err)
{
   return temporary_network_error(err);
}
