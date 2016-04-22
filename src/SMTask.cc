/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifdef TASK_DEBUG
# define DEBUG(x) do{printf x;fflush(stdout);}while(0)
#else
# define DEBUG(x) do{}while(0)
#endif

xlist_head<SMTask>  SMTask::all_tasks;
xlist_head<SMTask>  SMTask::ready_tasks;
xlist_head<SMTask>  SMTask::new_tasks;
xlist_head<SMTask>  SMTask::deleted_tasks;

SMTask	 *SMTask::current;

SMTask	 *SMTask::stack[SMTASK_MAX_DEPTH];
int	 SMTask::stack_ptr;

PollVec	 SMTask::block;
TimeDate SMTask::now;
time_t	 SMTask::last_block;

static SMTask *init_task=new SMTaskInit;

SMTask::SMTask()
 : all_tasks_node(this), ready_tasks_node(this),
   new_tasks_node(this), deleted_tasks_node(this)
{
   // insert in the chain
   all_tasks.add(all_tasks_node);

   suspended=false;
   suspended_slave=false;
   running=0;
   ref_count=0;
   deleting=false;
   new_tasks.add(new_tasks_node);
   DEBUG(("new SMTask %p (count=%d)\n",this,all_tasks.count()));
}

void  SMTask::Suspend()
{
   if(suspended)
      return;
   DEBUG(("Suspend(%p) from %p\n",this,current));
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
   DEBUG(("SuspendSlave(%p) from %p\n",this,current));
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
void SMTask::ResumeInternal()
{
   if(!new_tasks_node.listed() && !ready_tasks_node.listed())
      new_tasks.add_tail(new_tasks_node);
}

SMTask::~SMTask()
{
   DEBUG(("delete SMTask %p (count=%d)\n",this,all_tasks.count()));
   assert(!running);
   assert(!ref_count);
   assert(deleting);

   if(ready_tasks_node.listed())
      ready_tasks_node.remove();
   if(new_tasks_node.listed())
      new_tasks_node.remove();
   assert(!deleted_tasks_node.listed());

   // remove from the chain
   all_tasks_node.remove();
}

void SMTask::DeleteLater()
{
   if(deleting)
      return;
   deleting=true;
   deleted_tasks.add_tail(deleted_tasks_node);
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
   while(block.WillNotBlock() && !limit_timer.Stopped());
}

int SMTask::CollectGarbage()
{
   int count=0;
   xlist_for_each_safe(SMTask,deleted_tasks,node,task,next)
   {
      if(task->running || task->ref_count)
	 continue;
      node->remove();
      delete task;
      count++;
   }
   return count;
}

int SMTask::ScheduleThis()
{
   assert(ready_tasks_node.listed());
   if(running)
      return STALL;
   if(deleting || IsSuspended())
   {
      ready_tasks_node.remove();
      return STALL;
   }
   Enter();	   // mark it current and running.
   int res=Do();   // let it run.
   Leave();	   // unmark it running and change current.
   return res;
}

int SMTask::ScheduleNew()
{
   int res=STALL;
   xlist_for_each_safe(SMTask,new_tasks,node,task,next)
   {
      task->new_tasks_node.remove();
      ready_tasks.add(task->ready_tasks_node);
      SMTask *next_task=next->get_obj();
      if(next_task)  // protect next from deleting
	 next_task->IncRefCount();
      res|=task->ScheduleThis();
      if(next_task)
	 next_task->DecRefCount();
   }
   return res;
}

void SMTask::Schedule()
{
   block.Empty();

   // get time once and assume Do() don't take much time
   UpdateNow();

   timeval timer_timeout=Timer::GetTimeoutTV();
   if(timer_timeout.tv_sec>=0)
      block.SetTimeout(timer_timeout);

   int res=ScheduleNew();
   xlist_for_each_safe(SMTask,ready_tasks,node,task,next)
   {
      SMTask *next_task=next->get_obj();
      if(next_task)  // protect next from deleting
	 next_task->IncRefCount();
      res|=task->ScheduleThis();
      res|=ScheduleNew(); // run just created tasks immediately
      if(next_task)
	 next_task->DecRefCount();
   }
   CollectGarbage();
   if(res)
      block.NoWait();
}

void SMTask::Block()
{
   // use timer to force periodic select to find out which FDs are ready.
   if(block.WillNotBlock() && last_block==now.UnixTime())
      return;
   block.Block();
   last_block=now.UnixTime();
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
   return all_tasks.count();
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

void SMTask::PrintTasks()
{
   xlist_for_each(SMTask,all_tasks,node,scan)
   {
      const char *c=scan->GetLogContext();
      if(!c) c="";
      printf("%p\t%c%c%c\t%d\t%s\n",scan,scan->running?'R':' ',
	 scan->suspended?'S':' ',scan->deleting?'D':' ',scan->ref_count,c);
   }
}
