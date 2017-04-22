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

#ifndef TIMER_H
#define TIMER_H

#include "SMTask.h"
#include "ResMgr.h"
#include "xlist.h"
#include "xheap.h"

class Timer
{
   Time start;
   Time stop;
   TimeInterval last_setting;
   double random_max;
   xstring_c resource;
   xstring_c closure;

   static int infty_count;
   static xlist_head<Timer> all_timers;
   xlist<Timer> all_timers_node;
   static xheap<Timer> running_timers;
   xheap<Timer>::node running_timers_node;

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
   void SetMicroSeconds(int us) { Set(TimeInterval(0,0,us)); }
   void SetResource(const char *,const char *);
   void AddRandom(double r);
   void Reset(const Time &t);
   void Reset() { Reset(SMTask::now); }
   void Reset(const Timer &t) { Reset(t.GetStartTime()); }
   void ResetDelayed(int s);
   void StopDelayed(int s);
   const TimeInterval& GetLastSetting() const { return last_setting; }
   TimeDiff TimePassed() const { return SMTask::now-start; }
   TimeInterval TimeLeft() const;
   bool IsInfty() const { return last_setting.IsInfty(); }
   const Time &GetStartTime() const { return start; }
   static timeval GetTimeoutTV();
   static void ReconfigAll(const char *);
};

bool operator<(const Timer& a,const Timer& b);

#endif
