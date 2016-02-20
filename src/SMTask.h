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

#ifndef SMTASK_H
#define SMTASK_H

#include "PollVec.h"
#include "TimeDate.h"
#include "Ref.h"
#include "xarray.h"
#include "xlist.h"
#include "misc.h"
#include "Error.h"
#include <errno.h>

class SMTask
{
   virtual int Do() = 0;

   // all tasks list
   static xlist_head<SMTask> all_tasks;
   xlist<SMTask> all_tasks_node;

   // ready (not suspended) tasks list
   static xlist_head<SMTask> ready_tasks;
   xlist<SMTask> ready_tasks_node;

   // just created or resumed tasks
   static xlist_head<SMTask> new_tasks;
   xlist<SMTask> new_tasks_node;

   // deleted and going to be destroyed tasks
   static xlist_head<SMTask> deleted_tasks;
   xlist<SMTask> deleted_tasks_node;

   static PollVec block;
   enum { SMTASK_MAX_DEPTH=64 };
   static SMTask *stack[SMTASK_MAX_DEPTH];
   static int stack_ptr;

   bool	 suspended;
   bool	 suspended_slave;

   int	 running;
   int	 ref_count;
   bool	 deleting;

   int ScheduleThis();
   static int ScheduleNew();

protected:
   enum
   {
      STALL=0,
      MOVED=1,	  // STALL|MOVED==MOVED.
      WANTDIE=2	  // for AcceptSig
   };

   // SuspendInternal and ResumeInternal usually suspend and resume slave tasks
   virtual void SuspendInternal() {}
   virtual void ResumeInternal();
   virtual void PrepareToDie() {}  // it is called from Delete no matter of running and ref_count

   bool Deleted() const { return deleting; }
   virtual ~SMTask();

public:
   static void Block(int fd,int mask) { block.AddFD(fd,mask); }
   static void TimeoutU(int us) { block.AddTimeoutU(us); }
   static void Timeout(int ms) { TimeoutU(1000*ms); }
   static void TimeoutS(int s) { TimeoutU(1000000*s); }
   static bool Ready(int fd,int mask) { return block.FDReady(fd,mask); }
   static void SetNotReady(int fd,int mask) { block.FDSetNotReady(fd,mask); }

   static TimeDate now;
   static void UpdateNow() { now.SetToCurrentTime(); }

   static void Schedule();
   static int CollectGarbage();
   static void Block();
   static time_t last_block;

   void Suspend();
   void Resume();

   // SuspendSlave and ResumeSlave are used in SuspendInternal/ResumeInternal
   // to suspend/resume slave tasks
   void SuspendSlave();
   void ResumeSlave();

   bool IsSuspended() { return suspended|suspended_slave; }

   virtual const char *GetLogContext() { return 0; }
   static const char *GetCurrentLogContext() { return current->GetLogContext(); }

   SMTask();

   void DeleteLater();
   static void Delete(SMTask *);
   void IncRefCount() { ref_count++; }
   void DecRefCount() { if(ref_count>0) ref_count--; }
   static SMTask *_MakeRef(SMTask *task) { if(task) task->IncRefCount(); return task; }
   static void _DeleteRef(SMTask *task)  { if(task) { task->DecRefCount(); Delete(task); } }
   static SMTask *_SetRef(SMTask *task,SMTask *new_task);
   template<typename T> static T *MakeRef(T *task) { _MakeRef(task); return task; }
   static int Roll(SMTask *);
   int Roll() { return Roll(this); }
   static void RollAll(const TimeInterval &max_time);

   static SMTask *current;

   static void Enter(SMTask *task);
   static void Leave(SMTask *task);
   void Enter() { Enter(this); }
   void Leave() { Leave(this); }

   static int TaskCount();
   static void PrintTasks();
   static bool NonFatalError(int err);
   static bool TemporaryNetworkError(int err) { return temporary_network_error(err); }
   static Error *SysError(int e=errno) { return new Error(e,strerror(e),!NonFatalError(e)); }

   static void Cleanup();
};

class SMTaskInit : public SMTask
{
   int Do();
public:
   SMTaskInit();
   ~SMTaskInit();
};

template<class T> class SMTaskRef
{
   SMTaskRef<T>(const SMTaskRef<T>&);  // disable cloning
   void operator=(const SMTaskRef<T>&);   // and assignment

protected:
   T *ptr;

public:
   SMTaskRef() { ptr=0; }
   SMTaskRef<T>(T *p) : ptr(SMTask::MakeRef(p)) {}
   ~SMTaskRef<T>() { SMTask::_DeleteRef(ptr); ptr=0; }
   void operator=(T *p) { ptr=static_cast<T*>(SMTask::_SetRef(ptr,p)); }
   operator const T*() const { return ptr; }
   T *operator->() const { return ptr; }
   T *borrow() { if(ptr) ptr->DecRefCount(); return replace_value(ptr,(T*)0); }
   const T *get() const { return ptr; }
   T *get_non_const() const { return ptr; }

   template<class C> const SMTaskRef<C>& Cast() const
      { void(static_cast<C*>(ptr)); return *(const SMTaskRef<C>*)this; }

   static const SMTaskRef<T> null;

   void _set(T *p) { ptr=p; }
   void _clear() { ptr=0; }
   void unset() { *this=0; }
};

template<typename T>
class TaskRefArray : public _RefArray< T,SMTaskRef<T> > {
   TaskRefArray& operator=(const TaskRefArray&); // make assignment fail
   TaskRefArray(const TaskRefArray&);	       // disable cloning
public:
   TaskRefArray() : _RefArray< T,SMTaskRef<T> >() {}
};

#endif /* SMTASK_H */
