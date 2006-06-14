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
#include "SMTask.h"

void time_tuple::normalize()
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
void time_tuple::add(time_t s,int ms)
{
   sec+=s;
   msec+=ms;
   if(msec>=1000)
      msec-=1000,sec++;
   else if(msec<0)
      msec+=1000,sec--;
}
void time_tuple::add(double s)
{
   time_t s_int=time_t(s);
   add(s_int,int((s-s_int)*1000));
}
double time_tuple::to_double() const
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
Time::Time()
{
   // this saves a system call
   *this=SMTask::now;
}
bool Time::Passed(int s) const
{
   return TimeDiff(SMTask::now,*this)>=s;
}
void TimeDate::set_local_time()
{
   if(local_time_unix==UnixTime())
      return;
   time_t t=UnixTime();
   local_time=*localtime(&t);
}
const char *TimeDate::IsoDateTime()
{
   static char buf[21];
   set_local_time();
   strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M",&local_time);
   buf[sizeof(buf)-1]=0;
   return buf;
}
int TimeDiff::MilliSeconds() const
{
   return get_seconds()*1000+get_milliseconds();
}
time_t TimeDiff::Seconds() const
{
   return get_seconds()+(get_milliseconds()+500)/1000;
}
void TimeDiff::Set(double s)
{
   time_t s_int=(time_t)s;
   set(s_int,int((s-s_int)*1000));
}

bool TimeInterval::Finished(const Time &base) const
{
   if(infty)
      return false;
   TimeDiff elapsed(SMTask::now,base);
   if(!lt(elapsed))
      return false;
   return true;
}
int TimeInterval::GetTimeout(const Time &base) const
{
   if(infty)
      return HOUR*1000;	// to avoid dead-lock message
   TimeDiff elapsed(SMTask::now,base);
   if(lt(elapsed))
      return 0;
   elapsed-=*this;
   if(-elapsed.Seconds()>HOUR)
      return HOUR*1000;
   return -elapsed.MilliSeconds();
}
