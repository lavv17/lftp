/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef SLEEPJOB_H
#define SLEEPJOB_H

#include "Job.h"
#include "ResMgr.h"

class SleepJob : public SessionJob
{
   time_t start_time;
   TimeInterval next_time;
   char *cmd;
   int exit_code;
   bool done;
   char *saved_cwd;
   class CmdExec *exec;
   bool   repeat;
   int    repeat_count;

public:
   int Do();
   int Done() { return done; }
   int ExitCode() { return exit_code; }

   SleepJob(const TimeInterval &when,FileAccess *s=0,char *what=0);
   ~SleepJob();

   void PrintStatus(int v);

   void Repeat() { repeat=true; start_time-=next_time.Seconds(); }
};

#endif//SLEEPJOB_H
