/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef SMTASK_H
#define SMTASK_H

#include "PollVec.h"
#include "TimeDate.h"

class SMTask
{
   virtual int Do() = 0;

   SMTask *next;

   static SMTask *chain;
   static PollVec sched_total;
   static SMTask **stack;
   static int stack_ptr;
   static int stack_size;

   bool	 suspended;
   bool	 suspended_slave;

protected:
   int	 running;
   int	 ref_count;
   bool	 deleting;

   enum
   {
      STALL=0,
      MOVED=1,	  // STALL|MOVED==MOVED.
      WANTDIE=2	  // for AcceptSig
   };

   virtual ~SMTask();

   // SuspendInternal and ResumeInternal usually suspend and resume slave tasks
   virtual void SuspendInternal() {}
   virtual void ResumeInternal() {}

public:
   PollVec  block;

   void Block(int fd,int mask) { block.AddFD(fd,mask); }
   void Timeout(int ms) { block.AddTimeout(ms); }
   void TimeoutS(int s) { Timeout(1000*s); }

   static TimeDate now;
   static void UpdateNow() { now.SetToCurrentTime(); }

   static void Schedule();
   static int CollectGarbage();
   static void Block() { sched_total.Block(); }

   void Suspend();
   void Resume();

   // SuspendSlave and ResumeSlave are used in SuspendInternal/ResumeInternal
   // to suspend/resume slave tasks
   void SuspendSlave();
   void ResumeSlave();

   bool IsSuspended() { return suspended|suspended_slave; }

   virtual void Reconfig(const char *name=0) {};
   static void ReconfigAll(const char *name);

   virtual const char *GetLogContext() { return 0; }

   SMTask();

   void DeleteLater() { deleting=true; }
   static void Delete(SMTask *);
   static SMTask *_MakeRef(SMTask *task) { if(task) task->ref_count++; return task; }
   static void _DeleteRef(SMTask *task)  { if(task) task->ref_count--; Delete(task); }
#if __GNUC__
   template<typename T> static void DeleteRef(T *&task) { _DeleteRef(task); task=0; }
   template<typename T> static T *MakeRef(T *task) { _MakeRef(task); return task; }
#else
# define DeleteRef(task) do { _DeleteRef((task)); (task)=0; } while(0)
# define MakeRef(task)   (_MakeRef((task)),(task))
#endif
   static int Roll(SMTask *);
   static void RollAll(const TimeInterval &max_time);

   static SMTask *current;

   static void Enter(SMTask *task);
   static void Leave(SMTask *task);
   void Enter() { Enter(this); }
   void Leave() { Leave(this); }

   static int TaskCount();
   static void PrintTasks();
   static bool NonFatalError(int err);
   static bool TemporaryNetworkError(int err);

   static void Cleanup();
};

class SMTaskInit : public SMTask
{
   int Do();
public:
   SMTaskInit();
   ~SMTaskInit();
};

#endif /* SMTASK_H */
