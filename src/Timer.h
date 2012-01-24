/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef TIMER_H
#define TIMER_H

#include "SMTask.h"
#include "ResMgr.h"

class Timer
{
   Time start;
   Time stop;
   TimeInterval last_setting;
   double random_max;
   const char *resource;
   const char *closure;

   static int infty_count;
   static Timer *chain_all;
   Timer *next_all;
   static Timer *chain_running;
   Timer *next_running;
   Timer *prev_running;
   void remove_from_running_list();
   void re_sort();
   void re_set();
   void add_random();
   void set_last_setting(const TimeInterval &);
   void init();
   void reconfig(const char *);

public:
   Timer();
   ~Timer();
   Timer(int s,int ms=0);
   Timer(const TimeInterval &);
   Timer(const char *,const char *);
   bool Stopped() const;
   void Stop() { stop=SMTask::now; re_sort(); }
   void Set(const TimeInterval&);
   void Set(time_t s,int ms=0) { Set(TimeInterval(s,ms)); }
   void SetMilliSeconds(int ms) { Set(TimeInterval(0,ms)); }
   void SetResource(const char *,const char *);
   void AddRandom(double r);
   void Reset(const Time &t);
   void Reset() { Reset(SMTask::now); }
   void Reset(const Timer &t) { Reset(t.GetStartTime()); }
   void ResetDelayed(int s);
   const TimeInterval& GetLastSetting() const { return last_setting; }
   TimeDiff TimePassed() const { return SMTask::now-start; }
   TimeInterval TimeLeft() const;
   bool IsInfty() const { return last_setting.IsInfty(); }
   const Time &GetStartTime() const { return start; }
   static int GetTimeout();
   static void ReconfigAll(const char *);
};

#endif
