/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "Timer.h"

void Timer::set_timeout() const
{
   if(last_setting.IsInfty())
      current->TimeoutS(HOUR);
   else
   {
      TimeDiff remains(stop,now);
      current->Timeout(remains.MilliSeconds());
   }
}

void Timer::Set(const TimeDiff &diff)
{
   last_setting=diff;
   resource=closure=0;
   Reset();
}
void Timer::Reset()
{
   start=now;
   stop=now;
   stop+=last_setting;
   set_timeout();
}
void Timer::SetResource(const char *r,const char *c)
{
   if(resource!=r || closure!=c)
   {
      resource=r;
      closure=c;
      start=now;
      Reconfig(r);
   }
   else
   {
      Reset();
   }
}
int Timer::Do()
{
   if(!Stopped())
      set_timeout();
   return STALL;
}
bool Timer::Stopped() const
{
   if(last_setting.IsInfty())
      return false;
   return now>=stop;
}
void Timer::Reconfig(const char *r)
{
   if(resource && (!r || !strcmp(r,resource)))
   {
      last_setting=TimeIntervalR(ResMgr::Query(resource,closure));
      stop=start;
      stop+=last_setting;
      set_timeout();
   }
}
Timer::Timer() : last_setting(1)
{
   resource=0;
   closure=0;
}
Timer::Timer(const TimeDiff &d) : last_setting(d)
{
   resource=0;
   closure=0;
   Reset();
}
