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
#include "SMTask.h"
#include "Timer.h"
#include "xstring.h"
#include "misc.h"

#define now SMTask::now

xlist_head<Timer> Timer::all_timers;
xheap<Timer> Timer::running_timers;
int Timer::infty_count;

timeval Timer::GetTimeoutTV()
{
   Timer *t;
   while((t=running_timers.get_min())!=0 && t->Stopped())
      running_timers.pop_min();
   if(!t) {
      timeval tv={infty_count?HOUR:-1, 0};
      return tv;
   }
   TimeDiff remains(t->stop,now);
   return remains.toTimeval();
}

TimeInterval Timer::TimeLeft() const
{
   if(IsInfty())
      return TimeInterval();
   if(now>=stop)
      return TimeInterval(0,0);
   return TimeInterval(stop-now);
}
void Timer::set_last_setting(const TimeInterval &i)
{
   infty_count-=IsInfty();
   last_setting=i;
   infty_count+=IsInfty();
   re_set();
}
void Timer::add_random()
{
   if(random_max>0.0001) {
      stop+=TimeDiff::valueOf(random_max*random01());
   }
}
void Timer::re_set()
{
   stop=start;
   stop+=last_setting;
   add_random();
   re_sort();
}
void Timer::AddRandom(double r) {
   random_max=r;
   add_random();
   re_sort();
}
void Timer::Set(const TimeInterval &i)
{
   resource.unset();
   closure.unset();
   start=SMTask::now;
   set_last_setting(i);
}
void Timer::Reset(const Time &t)
{
   if(start>=t && stop>t)
      return;
   start=t;
   re_set();
}
void Timer::ResetDelayed(int s)
{
   Reset(SMTask::now+TimeDiff(s,0));
}
void Timer::StopDelayed(int s)
{
   stop=SMTask::now+TimeDiff(s,0);
   re_sort();
}
void Timer::SetResource(const char *r,const char *c)
{
   if(resource!=r || closure!=c)
   {
      resource.set(r);
      closure.set(c);
      start=now;
      reconfig(r);
   }
   else
   {
      Reset();
   }
}
bool Timer::Stopped() const
{
   if(IsInfty())
      return false;
   return now>=stop;
}
void Timer::reconfig(const char *r)
{
   if(resource && (!r || !strcmp(r,resource)))
      set_last_setting(TimeIntervalR(ResMgr::Query(resource,closure)));
}
void Timer::init()
{
   random_max=0;
   all_timers.add(all_timers_node);
}
Timer::~Timer()
{
   running_timers.remove(running_timers_node);
   all_timers_node.remove();
   infty_count-=IsInfty();
}
Timer::Timer() : last_setting(1,0),
   all_timers_node(this), running_timers_node(this)
{
   init();
}
Timer::Timer(const TimeInterval &d) : last_setting(d),
   all_timers_node(this), running_timers_node(this)
{
   init();
   infty_count+=IsInfty();
   re_set();
}
Timer::Timer(const char *r,const char *c) : last_setting(0,0),
   all_timers_node(this), running_timers_node(this)
{
   init();
   resource.set(r);
   closure.set(c);
   start=now;
   reconfig(r);
}
Timer::Timer(int s,int ms) :
   all_timers_node(this), running_timers_node(this)
{
   init();
   Set(TimeInterval(s,ms));
}
void Timer::re_sort()
{
   running_timers.remove(running_timers_node);
   if(now<stop && !IsInfty())
      running_timers.add(running_timers_node);
}
void Timer::ReconfigAll(const char *r)
{
   xlist_for_each(Timer,all_timers,node,scan)
      scan->reconfig(r);
}

bool operator<(const Timer& a,const Timer& b)
{
   return a.TimeLeft()<b.TimeLeft();
}
