/*
 * lftp and utils
 *
 * Copyright (c) 1996-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#define MAX_TASK_RECURSION 32

class SMTask
{
   virtual int Do() = 0;

   SMTask *next;

   static SMTask *chain;
   static PollVec sched_total;
   static SMTask *stack[MAX_TASK_RECURSION];
   static int stack_ptr;

protected:
   int	 running;
   bool	 suspended;
   bool	 deleting;

   enum
   {
      STALL=0,
      MOVED=1,	  // STALL|MOVED==MOVED.
      WANTDIE=2	  // for AcceptSig
   };

   virtual ~SMTask();

public:
   PollVec  block;

   void Block(int fd,int mask) { block.AddFD(fd,mask); }
   void Timeout(int ms) { block.AddTimeout(ms); }
   void TimeoutS(int s) { Timeout(1000*s); }

   static TimeDate now;
   static void UpdateNow() { now.SetToCurrentTime(); }

   static void Schedule();
   static void Block() { sched_total.Block(); }

   virtual void Suspend();
   virtual void Resume();
   bool IsSuspended() { return suspended; }

   virtual void Reconfig(const char *name=0) {};
   static void ReconfigAll(const char *name);

   virtual const char *GetLogContext() { return 0; }

   SMTask();

   static void Delete(SMTask *);
   static int Roll(SMTask *);
   static void DeleteAll();
   static void RollAll(int max_time);

   static SMTask *current;

   static void Enter(SMTask *task)
      {
	 task->running++;
	 stack[stack_ptr++]=current;
	 current=task;
      }
   static void Leave(SMTask *task)
      {
	 current=stack[--stack_ptr];
	 task->running--;
      }
   void Enter() { Enter(this); }
   void Leave() { Leave(this); }

   static int TaskCount();
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
