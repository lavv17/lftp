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
   next=prev=0;
   next_all=chain_all;
   chain_all=this;
}
Timer::~Timer()
{
   infty_count-=last_setting.IsInfty();
   if(next)
      next->prev=prev;
   if(prev)
      prev->next=next;
   if(chain_running==this)
      chain_running=next;
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
void Timer::re_sort()
{
   if(now>=stop || last_setting.IsInfty())
   {
      // make sure it is not in the list.
      if(prev==0 && next==0 && chain_running!=this)
	 return;
      if(prev)
	 prev->next=next;
      if(next)
	 next->prev=prev;
      if(chain_running==this)
	 chain_running=next;
      next=prev=0;
   }
   else
   {
      // find new location in the list.
      Timer *new_next=next;
      Timer *new_prev=prev;
      if(prev==0 && next==0 && chain_running!=this)
	 new_next=chain_running;
      else if((!prev || prev->stop<stop)
	   && (!next || stop<next->stop))
	 return;
      if(next)
	 next->prev=prev;
      if(prev)
	 prev->next=next;
      while(new_next && new_next->stop<stop)
      {
	 new_prev=new_next;
	 new_next=new_next->next;
      }
      while(new_prev && stop<new_prev->stop)
      {
	 new_next=new_prev;
	 new_prev=new_prev->prev;
      }
      next=new_next;
      prev=new_prev;
      if(new_next)
	 new_next->prev=this;
      if(new_prev)
	 new_prev->next=this;
      if(!new_prev)
	 chain_running=this;
   }
}
void Timer::ReconfigAll(const char *r)
{
   for(Timer *scan=chain_all; scan; scan=scan->next_all)
{
printf("reconfig(%p,%s)\n",scan,r);
      scan->reconfig(r);
}
}
