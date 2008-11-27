/*
 * lftp and utils
 *
 * Copyright (c) 1996-2008 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "Timer.h"
#include "misc.h"

SMTask	 *SMTask::chain=0;
SMTask	 *SMTask::current=0;
xarray<SMTask*>	SMTask::stack;
PollVec	 SMTask::block;
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
   suspended_slave=false;
   running=0;
   ref_count=0;
   deleting=false;
   task_count++;
#ifdef TASK_DEBUG
   printf("new SMTask %p (count=%d)\n",this,task_count);
#endif
}

void  SMTask::Suspend()
{
   if(suspended)
      return;
   if(!IsSuspended())
      SuspendInternal();
   suspended=true;
}
void  SMTask::Resume()
{
   if(!suspended)
      return;
   suspended=false;
   if(!IsSuspended())
      ResumeInternal();
}
void  SMTask::SuspendSlave()
{
   if(suspended_slave)
      return;
   if(!IsSuspended())
      SuspendInternal();
   suspended_slave=true;
}
void  SMTask::ResumeSlave()
{
   if(!suspended_slave)
      return;
   suspended_slave=false;
   if(!IsSuspended())
      ResumeInternal();
}

SMTask::~SMTask()
{
#ifdef TASK_DEBUG
   printf("delete SMTask %p (count=%d)\n",this,task_count);
#endif
   task_count--;
   if(__builtin_expect(running,0))
   {
      fprintf(stderr,"SMTask(%p).running=%d\n",this,running);
      fprintf(stderr,"SMTask stack:");
      for(int i=0; i<stack.count(); i++)
	 fprintf(stderr," %p",stack[i]);
      fprintf(stderr,"; current=%p\n",current);
      abort();
   }
   assert(!ref_count);
   // remove from the chain
   SMTask **scan=&chain;
   while(*scan)
   {
      if(*scan==this)
      {
	 *scan=next;
	 return;
      }
      scan=&((*scan)->next);
   }
}

void SMTask::DeleteLater()
{
   deleting=true;
   PrepareToDie();
}
void SMTask::Delete(SMTask *task)
{
   if(!task)
      return;
   task->DeleteLater();
   // if possible, delete now.
   if(!task->running && !task->ref_count)
      delete task;
}
SMTask *SMTask::_SetRef(SMTask *task,SMTask *new_task)
{
   _DeleteRef(task);
   _MakeRef(new_task);
   return new_task;
}

void SMTask::Enter(SMTask *task)
{
   stack.append(current);
   current=task;
   current->running++;
}
void SMTask::Leave(SMTask *task)
{
   assert(current==task);
   current->running--;
   assert(stack.count()>0);
   current=stack.last();
   stack.chop();
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

void SMTask::RollAll(const TimeInterval &max_time)
{
   Timer limit_timer(max_time);
   do { Schedule(); }
   while(block.GetTimeout()==0 && !limit_timer.Stopped());
}

int SMTask::CollectGarbage()
{
   int count=0;
   bool repeat_gc;
   do {
      repeat_gc=false;
      SMTask *scan=chain;
      while(scan)
      {
	 if(scan->running || !scan->deleting || scan->ref_count)
	 {
	    scan=scan->next;
	    continue;
	 }
	 repeat_gc=true;
	 count++;
	 if(scan->next)
	 {
	    Enter(scan->next); // protect it from deleting (in scan's dtor)
	    delete scan;
	    Leave(scan=current);
	 }
	 else
	 {
	    delete scan;
	    break;
	 }
      }
   } while(repeat_gc);
   return count;
}

void SMTask::Schedule()
{
   SMTask *scan;

   block.Empty();

   // get time once and assume Do() don't take much time
   UpdateNow();

   int timer_timeout=Timer::GetTimeout();
   if(timer_timeout>=0)
      block.SetTimeout(timer_timeout);

   int res=STALL;
   for(scan=chain; scan; scan=scan->next)
   {
      if(scan->running || scan->IsSuspended())
	 continue;
      Enter(scan);	   // mark it current and running.
      res|=scan->Do();	   // let it run.
      Leave(scan);	   // unmark it running and change current.
   }
   if(CollectGarbage() || res)
      block.NoWait();
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
   CollectGarbage();
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

void SMTask::PrintTasks()
{
   for(SMTask *scan=chain; scan; scan=scan->next)
   {
      const char *c=scan->GetLogContext();
      if(!c) c="";
      printf("%p\t%c%c%c\t%d\t%s\n",scan,scan->running?'R':' ',
	 scan->suspended?'S':' ',scan->deleting?'D':' ',scan->ref_count,c);
   }
}
