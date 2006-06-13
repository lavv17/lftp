/*
 * lftp and utils
 *
 * Copyright (c) 2001-2006 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "SMTask.h"
#include "Timer.h"
#include "xstring.h"

#define now SMTask::now

Timer *Timer::chain_all;
Timer *Timer::chain_running;
int Timer::infty_count;

int Timer::GetTimeout()
{
   while(chain_running && chain_running->Stopped())
      chain_running->re_sort();
   if(!chain_running)
      return infty_count?HOUR*1000:-1;
   TimeDiff remains(chain_running->stop,now);
   return remains.MilliSeconds();
}
TimeInterval Timer::TimeLeft() const
{
   if(last_setting.IsInfty())
      return TimeInterval();
   if(now>=stop)
      return TimeInterval(0,0);
   return TimeInterval(stop-now);
}
void Timer::set_last_setting(const TimeInterval &i)
{
   infty_count-=last_setting.IsInfty();
   last_setting=i;
   infty_count+=last_setting.IsInfty();
}
void Timer::Set(const TimeInterval &i)
{
   resource=closure=0;
   set_last_setting(i);
   Reset();
}
void Timer::Reset(const Time &t)
{
   if(start>=t)
      return;
   start=t;
   stop=t;
   stop+=last_setting;
   re_sort();
}
void Timer::ResetDelayed(int s)
{
   Reset(SMTask::now+TimeDiff(s,0));
}
void Timer::SetResource(const char *r,const char *c)
{
   if(resource!=r || closure!=c)
   {
      resource=r;
      closure=c;
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
   if(last_setting.IsInfty())
      return false;
   return now>=stop;
}
void Timer::reconfig(const char *r)
{
   if(resource && (!r || !strcmp(r,resource)))
   {
      set_last_setting(TimeIntervalR(ResMgr::Query(resource,closure)));
      stop=start;
      stop+=last_setting;
      re_sort();
   }
}
void Timer::init()
{
   resource=0;
   closure=0;
   next_running=prev_running=0;
   next_all=chain_all;
   chain_all=this;
}
Timer::~Timer()
{
   infty_count-=last_setting.IsInfty();
   if(next_running)
      next_running->prev_running=prev_running;
   if(prev_running)
      prev_running->next_running=next_running;
   if(chain_running==this)
      chain_running=next_running;
   Timer **scan=&chain_all;
   while(*scan!=this)
      scan=&scan[0]->next_all;
   *scan=next_all;
}
Timer::Timer() : last_setting(1)
{
   init();
}
Timer::Timer(const TimeInterval &d) : last_setting(d)
{
   init();
   infty_count+=last_setting.IsInfty();
   Reset();
}
Timer::Timer(const char *r,const char *c) : last_setting(0)
{
   init();
   resource=r;
   closure=c;
   start=now;
   reconfig(r);
}
void Timer::re_sort()
{
   if(now>=stop || last_setting.IsInfty())
   {
      // make sure it is not in the list.
      if(prev_running==0 && next_running==0 && chain_running!=this)
	 return;
      if(prev_running)
	 prev_running->next_running=next_running;
      if(next_running)
	 next_running->prev_running=prev_running;
      if(chain_running==this)
	 chain_running=next_running;
      next_running=prev_running=0;
   }
   else
   {
      // find new location in the list.
      Timer *new_next=next_running;
      Timer *new_prev=prev_running;
      if(prev_running==0 && next_running==0 && chain_running!=this)
	 new_next=chain_running;
      else if((!prev_running || prev_running->stop<stop)
	   && (!next_running || stop<next_running->stop))
	 return;
      if(next_running)
	 next_running->prev_running=prev_running;
      if(prev_running)
	 prev_running->next_running=next_running;
      while(new_next && new_next->stop<stop)
      {
	 new_prev=new_next;
	 new_next=new_next->next_running;
      }
      while(new_prev && stop<new_prev->stop)
      {
	 new_next=new_prev;
	 new_prev=new_prev->prev_running;
      }
      next_running=new_next;
      prev_running=new_prev;
      if(new_next)
	 new_next->prev_running=this;
      if(new_prev)
	 new_prev->next_running=this;
      if(!new_prev)
	 chain_running=this;
   }
}
void Timer::ReconfigAll(const char *r)
{
   for(Timer *scan=chain_all; scan; scan=scan->next_all)
      scan->reconfig(r);
}
