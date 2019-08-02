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
#include <cmath>
#include <stdlib.h>
#include "Speedometer.h"
#include "misc.h"
#include "xstring.h"
#include "ResMgr.h"

#define now SMTask::now

Speedometer::Speedometer(const char *p)
{
   period=15;
   rate=0;
   last_second=now;
   start=now;
   last_bytes=0;
   terse=true;
   period_resource=p;
   Reconfig(0);
}
bool Speedometer::Valid()
{
   return now>=start+TimeDiff(1,0) && now<last_bytes+TimeDiff(period,0);
}
float Speedometer::Get()
{
   Add(0);
   SMTask::current->Timeout(500);
   return rate;
}
void Speedometer::Add(int b)
{
   if(b==0 && (now==last_second || TimeDiff(now,last_second).MilliSeconds()<100))
      return;

   // This makes Speedometer start only when first data come.
   if(rate==0)
      Reset();

   double div=period;

   if(start>now)
      start=now;  // time was adjusted?
   if(now<last_second)
      last_second=now;

   double time_passed_since_start=TimeDiff(now,start);
   double time_passed=TimeDiff(now,last_second);
   if(time_passed_since_start<div)
      div=time_passed_since_start;
   if(div<1)
      div=1;

   rate*=1-time_passed/div;
   rate+=b/div;

   last_second=now;
   if(b>0)
      last_bytes=now;
   if(rate<0)
      rate=0;
}
// These `incorrect' units are used to fit on status line
xstring& Speedometer::GetStr(float r)
{
   if(r<1)
      return xstring::get_tmp("");
   if(r<1024)
      // for translator: those are the units. This is 'byte per second'
      return xstring::format(_("%.0fb/s"),r);
   else if(r<1024*1024)
      // for translator: This is 'Kibibyte per second'
      return xstring::format(_("%.1fK/s"),r/1024.);
   else
      // for translator: This is 'Mebibyte per second'
      return xstring::format(_("%.2fM/s"),r/(1024.*1024));
}
xstring& Speedometer::GetStrProper(float r)
{
   if(r<1)
      return xstring::get_tmp("");
   if(r<1024)
      return xstring::format(_("%.0f B/s"),r);
   else if(r<1024*1024)
      return xstring::format(_("%.1f KiB/s"),r/1024.);
   else
      return xstring::format(_("%.2f MiB/s"),r/(1024.*1024));
}
xstring& Speedometer::GetStr()
{
   return Valid() ? GetStr(Get()) : xstring::get_tmp("");
}
xstring& Speedometer::GetETAStrFromSize(off_t size)
{
   if(!Valid() || Get()<1)
      return xstring::get_tmp("");
   return GetETAStrFromTime(long(size/rate+.5));
}
xstring& Speedometer::GetETAStrFromTime(long eta)
{
   if(eta<0)
      return xstring::get_tmp("");

   unsigned flags=TimeInterval::TO_STR_TRANSLATE;
   if(terse)
      flags+=TimeInterval::TO_STR_TERSE;

   // for translator: Estimated Time of Arrival.
   return xstring::cat(_("eta:"),TimeInterval(eta,0).toString(flags),NULL);
}
const char *Speedometer::GetStrS(float r)
{
   xstring &rate=GetStr(r);
   if(rate.length())
      rate.append(' ');
   return rate;
}
const char *Speedometer::GetStrS()
{
   return Valid() ? GetStrS(Get()) : "";
}
const char *Speedometer::GetETAStrSFromSize(off_t s)
{
   xstring &eta=GetETAStrFromSize(s);
   if(eta.length())
      eta.append(' ');
   return eta;
}
const char *Speedometer::GetETAStrSFromTime(long s)
{
   xstring &eta=GetETAStrFromTime(s);
   if(eta.length())
      eta.append(' ');
   return eta;
}
void Speedometer::Reset()
{
   start=now;
   last_second=now;
   rate=0;
   last_bytes=0;
}

ResDecl res_eta_terse("xfer:eta-terse",  "yes",ResMgr::BoolValidate,ResMgr::NoClosure);
void Speedometer::Reconfig(const char *n)
{
   terse=res_eta_terse.QueryBool(0);
   SetPeriod(ResMgr::Query(period_resource,0));
}
