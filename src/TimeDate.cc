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

#include <config.h>
#include <limits.h>
#include "TimeDate.h"
#include "misc.h"
#include "SMTask.h"

void time_tuple::normalize()
{
   if(usec>=1000000 || usec<=-1000000)
   {
      sec+=usec/1000000;
      usec%=1000000;
   }
   if(usec<0)
   {
      usec+=1000000;
      sec-=1;
   }
}
void time_tuple::addU(time_t s,int us)
{
   sec+=s;
   usec+=us;
   if(usec>=1000000)
      usec-=1000000,sec++;
   else if(usec<0)
      usec+=1000000,sec--;
}
void time_tuple::add(double s)
{
   time_t s_int=time_t(s);
   addU(s_int,int((s-s_int)*1000000));
}
double time_tuple::to_double() const
{
   return sec+usec/1000000.;
}
void Time::SetToCurrentTime()
{
   time_t s;
   int us;
   xgettimeofday(&s,&us);
   set(s,0,us);
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
   // avoid repeating localtime calls by caching the result.
   // localtime in some implementation has very bad performance,
   // e.g. it can re-read /etc/localtime on every call.
   time_t t=UnixTime();
   if(local_time_unix==t)
      return;
   local_time=*localtime(&t);
   local_time_unix=t;
}
const char *TimeDate::IsoDateTime()
{
   static char buf[21];
   set_local_time();
   strftime(buf,sizeof(buf),"%Y-%m-%d %H:%M:%S",&local_time);
   buf[sizeof(buf)-1]=0;
   return buf;
}
int TimeDiff::MilliSeconds() const
{
   if(get_seconds()>=INT_MAX/2000)
      return INT_MAX/2;
   return get_seconds()*1000+get_milliseconds();
}
int TimeDiff::MicroSeconds() const
{
   if(get_seconds()>=INT_MAX/2000000)
      return INT_MAX/2;
   return get_seconds()*1000000+get_microseconds();
}
time_t TimeDiff::Seconds() const
{
   return get_seconds()+(get_microseconds()+500000)/1000000;
}
void TimeDiff::Set(double s)
{
   time_t s_int=(time_t)s;
   set(s_int,0,int((s-s_int)*1000000));
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
int TimeInterval::GetTimeoutU(const Time &base) const
{
   if(infty)
      return INT_MAX/2;	// to avoid dead-lock message
   TimeDiff elapsed(SMTask::now,base);
   if(lt(elapsed))
      return 0;
   elapsed-=*this;
   return -elapsed.MicroSeconds();
}

static void append_Nc(xstring& buf,long N,const char *c)
{
   buf.appendf("%ld%.*s",N,mblen(c,strlen(c)),c);
}

const char *TimeInterval::toString(unsigned flags)
{
   if(IsInfty())
      return "infinity";

   long eta2=0;
   long ueta=0;
   long ueta2=0;
   const char *letter=0;
   const char *letter2=0;

   long eta=Seconds();

   xstring &buf=xstring::get_tmp("");

   // for translator: only first letter matters
   const char *day_c=N_("day");
   const char *hour_c=N_("hour");
   const char *minute_c=N_("minute");
   const char *second_c=N_("second");

   if(flags&TO_STR_TRANSLATE) {
      day_c=_(day_c);
      hour_c=_(hour_c);
      minute_c=_(minute_c);
      second_c=_(second_c);
   }

   if(flags&TO_STR_TERSE)
   {
      if(eta>=100*HOUR)
      {
	 ueta=(eta+DAY/2)/DAY;
	 eta2=eta-ueta*DAY;
	 letter=day_c;
	 if(ueta<10)
	 {
	    letter2=hour_c;
	    ueta2=((eta2<-HOUR/2?eta2+DAY:eta2)+HOUR/2)/HOUR;
	    if(ueta2>0 && eta2<-HOUR/2)
	       ueta--;
	 }
      }
      else if(eta>=100*MINUTE)
      {
	 ueta=(eta+HOUR/2)/HOUR;
	 eta2=eta-ueta*HOUR;
	 letter=hour_c;
	 if(ueta<10)
	 {
	    letter2=minute_c;
	    ueta2=((eta2<-MINUTE/2?eta2+HOUR:eta2)+MINUTE/2)/MINUTE;
	    if(ueta2>0 && eta2<-MINUTE/2)
	       ueta--;
	 }
      }
      else if(eta>=100)
      {
	 ueta=(eta+MINUTE/2)/MINUTE;
	 letter=minute_c;
      }
      else
      {
	 ueta=eta;
	 letter=second_c;
      }
      append_Nc(buf,ueta,letter);
      if(letter2 && ueta2>0)
	 append_Nc(buf,ueta2,letter2);
   }
   else // verbose eta (by Ben Winslow)
   {
      if(eta>=DAY)
	 append_Nc(buf,eta/DAY,day_c);
      if(eta>=HOUR)
	 append_Nc(buf,(eta/HOUR)%24,hour_c);
      if(eta>=MINUTE)
	 append_Nc(buf,(eta/MINUTE)%60,minute_c);
      append_Nc(buf,eta%60,second_c);
   }
   return buf;
}

const TimeDiff& TimeDiff::valueOf(double v)
{
   static TimeDiff diff;
   diff.Set(v);
   return diff;
}
