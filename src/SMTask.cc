/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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

SMTask	 *SMTask::chain;
SMTask	 *SMTask::chain_ready;
SMTask	 *SMTask::chain_deleting;
SMTask	 *SMTask::current;
SMTask	 *SMTask::stack[SMTASK_MAX_DEPTH];
int	 SMTask::stack_ptr;
PollVec	 SMTask::block;
TimeDate SMTask::now;

static int task_count=0;
static SMTask *init_task=new SMTaskInit;

void SMTask::AddToReadyList()
{
   if(prev_next_ready)
      return;
   if(current && current->prev_next_ready) {
      // insert it so that it would be scanned next
      prev_next_ready=&(current->next_ready);
   } else {
      prev_next_ready=&chain_ready;
   }
   next_ready=*prev_next_ready;
   if(*prev_next_ready)
      (*prev_next_ready)->prev_next_ready=&next_ready;
   *prev_next_ready=this;
}
void SMTask::RemoveFromReadyList()
{
   if(!prev_next_ready)
      return;
   if(next_ready)
      next_ready->prev_next_ready=prev_next_ready;
   *prev_next_ready=next_ready;
   prev_next_ready=0;
   next_ready=0;
}

SMTask::SMTask()
{
   // insert in the chain
   next=chain;
   chain=this;

   next_ready=0;
   prev_next_ready=0;
   next_deleting=0;
   suspended=false;
   suspended_slave=false;
   running=0;
   ref_count=0;
   deleting=false;
   task_count++;
   AddToReadyList();
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
      for(int i=0; i<stack_ptr; i++)
	 fprintf(stderr," %p",stack[i]);
      fprintf(stderr,"; current=%p\n",current);
      abort();
   }
   assert(!ref_count);
   assert(!next_ready && !prev_next_ready);
   assert(deleting);

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
   if(deleting)
      return;
   deleting=true;
   RemoveFromReadyList();
   this->next_deleting=chain_deleting;
   chain_deleting=this;
   PrepareToDie();
}
void SMTask::Delete(SMTask *task)
{
   if(!task)
      return;
   task->DeleteLater();
   // CollectGarbage will delete the task gracefully
}
SMTask *SMTask::_SetRef(SMTask *task,SMTask *new_task)
{
   _DeleteRef(task);
   _MakeRef(new_task);
   return new_task;
}

void SMTask::Enter(SMTask *task)
{
   assert(stack_ptr<SMTASK_MAX_DEPTH);
   stack[stack_ptr++]=current;
   current=task;
   current->running++;
}
void SMTask::Leave(SMTask *task)
{
   assert(current==task);
   current->running--;
   assert(stack_ptr>0);
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
      SMTask *new_chain_deleting=0;
      repeat_gc=false;
      while(chain_deleting)
      {
	 SMTask *scan=chain_deleting;
	 chain_deleting=scan->next;
	 scan->next=0;
	 if(scan->running || scan->ref_count)
	 {
	    scan->next=new_chain_deleting;
	    new_chain_deleting=scan;
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
      chain_deleting=new_chain_deleting;
   } while(repeat_gc && chain_deleting);
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
   for(scan=chain_ready; scan; scan=scan->next_ready)
   {
      if(scan->running || scan->IsSuspended())
	 continue;
      Enter(scan);	   // mark it current and running.
      res|=scan->Do();	   // let it run.
      Leave(scan);	   // unmark it running and change current.
   }
   CollectGarbage();
   if(res)
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
   Delete(init_task);
   CollectGarbage();
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
