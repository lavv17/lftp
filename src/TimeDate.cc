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
#include "TimeDate.h"
#include "misc.h"

void time_touple::normalize()
{
   if(msec>=1000 || msec<=-1000)
   {
      sec+=msec/1000;
      msec%=1000;
   }
   if(msec<0)
   {
      msec+=1000;
      sec-=1;
   }
}
void time_touple::add(const time_touple &o)
{
   sec+=o.sec;
   msec+=o.msec;
   if(msec>=1000)
      msec-=1000,sec++;
   else if(msec<=-1000)
      msec+=1000,sec--;
}
void time_touple::sub(const time_touple &o)
{
   sec-=o.sec;
   msec-=o.msec;
   if(msec>=1000)
      msec-=1000,sec++;
   else if(msec<=-1000)
      msec+=1000,sec--;
}
bool time_touple::lt(const time_touple &o) const
{
   return sec<o.sec || (sec==o.sec && msec<o.msec);
}
double time_touple::to_double() const
{
   return sec+msec/1000.;
}
void Time::SetToCurrentTime()
{
   time_t s;
   int ms;
   xgettimeofday(&s,&ms);
   ms/=1000;
   set(s,ms);
}
void TimeDate::set_local_time()
{
   if(local_time_unix==UnixTime())
      return;
   time_t t=UnixTime();
   local_time=*localtime(&t);
}
int TimeDate::Year()
{
   set_local_time();
   return local_time.tm_year+1900;
}
int TimeDate::Month()
{
   set_local_time();
   return local_time.tm_mon+1;
}
int TimeDate::DayOfMonth()
{
   set_local_time();
   return local_time.tm_mday;
}
int TimeDate::Hour()
{
   set_local_time();
   return local_time.tm_hour;
}
int TimeDate::Minute()
{
   set_local_time();
   return local_time.tm_min;
}
int TimeDate::Second()
{
   set_local_time();
   return local_time.tm_sec;
}
int TimeDiff::MilliSeconds() const
{
   return get_seconds()*1000+get_milliseconds();
}
