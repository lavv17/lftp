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

#ifndef SMTASK_H
#define SMTASK_H

#include "PollVec.h"

class SMTask
{
   virtual int Do() = 0;

   SMTask *next;

   static SMTask *chain;
   static SMTask *sched_scan;
   static PollVec sched_total;


protected:
   bool	 running;
   bool	 suspended;

   enum
   {
      STALL,
      MOVED,
      WANTDIE
   };

public:
   PollVec  block;

   static time_t now;
   static int now_ms;
   static void UpdateNow();

   static void Schedule();
   static void Block() { sched_total.Block(); }

   virtual void Suspend();
   virtual void Resume();

   virtual void Reconfig() {};
   static void ReconfigAll();

   SMTask();
   virtual ~SMTask();
};

#endif /* SMTASK_H */
