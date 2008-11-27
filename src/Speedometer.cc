/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include <config.h>
#include <math.h>
#include <stdlib.h>
#include "Speedometer.h"
#include "misc.h"
#include "xstring.h"
#include "ResMgr.h"

#undef super
#define super SMTask

char Speedometer::buf_rate[40];
char Speedometer::buf_eta[40];

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
int Speedometer::Do()
{
   return STALL;
}
float Speedometer::Get()
{
   Add(0);
   if(Valid())
      current->TimeoutS(1);
   return rate;
}
void Speedometer::Add(int b)
{
   if(b==0 && TimeDiff(now,last_second).MilliSeconds()<100)
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
const char *Speedometer::GetStr(float r)
{
   buf_rate[0]=0;
   if(r<1)
      return "";
   if(r<1024)
      // for translator: those are the units. This is 'byte per second'
      sprintf(buf_rate,_("%.0fb/s"),r);
   else if(r<1024*1024)
      // for translator: This is 'kilobyte per second'
      sprintf(buf_rate,_("%.1fK/s"),r/1024.);
   else
      // for translator: This is 'Megabyte per second'
      sprintf(buf_rate,_("%.2fM/s"),r/1024./1024.);
   return buf_rate;
}
const char *Speedometer::GetETAStrFromSize(off_t size)
{
   buf_eta[0]=0;

   if(!Valid() || Get()<1)
      return buf_eta;

   return GetETAStrFromTime(long(size/rate+.5));
}
const char *Speedometer::GetETAStrFromTime(long eta)
{
   buf_eta[0]=0;

   if(eta<0)
      return buf_eta;

   long eta2=0;
   long ueta=0;
   long ueta2=0;
   const char *letter=0;
   const char *letter2=0;

   // for translator: only first letter matters
   const char *day_c=_("day");
   const char *hour_c=_("hour");
   const char *minute_c=_("minute");
   const char *second_c=_("second");

   // for translator: Estimated Time of Arrival.
   const char *tr_eta=_("eta:");

   if(terse)
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
      if(letter2 && ueta2>0)
	 sprintf(buf_eta,"%s%ld%.*s%ld%.*s",tr_eta,ueta,mblen(letter,strlen(letter)),letter,
						   ueta2,mblen(letter2,strlen(letter2)),letter2);
      else
	 sprintf(buf_eta,"%s%ld%.*s",tr_eta,ueta,mblen(letter,strlen(letter)),letter);
   }
   else // verbose eta (by Ben Winslow)
   {
      long unit;
      strcpy(buf_eta, tr_eta);

      if(eta>=DAY)
      {
	 unit=eta/DAY;
	 sprintf(buf_eta+strlen(buf_eta),"%ld%.*s",unit,mblen(day_c,strlen(day_c)),day_c);
      }
      if(eta>=HOUR)
      {
	 unit=(eta/HOUR)%24;
	 sprintf(buf_eta+strlen(buf_eta),"%ld%.*s",unit,mblen(hour_c,strlen(hour_c)),hour_c);
      }
      if(eta>=MINUTE)
      {
	 unit=(eta/MINUTE)%60;
	 sprintf(buf_eta+strlen(buf_eta),"%ld%.*s",unit,mblen(minute_c,strlen(minute_c)),minute_c);
      }
      unit=eta%60;
      sprintf(buf_eta+strlen(buf_eta), "%ld%.*s",unit,mblen(second_c,strlen(second_c)),second_c);
   }
   return buf_eta;
}
const char *Speedometer::GetStrS(float r)
{
   GetStr(r);
   if(buf_rate[0])
      strcat(buf_rate," ");
   return buf_rate;
}
const char *Speedometer::GetETAStrSFromSize(off_t s)
{
   GetETAStrFromSize(s);
   if(buf_eta[0])
      strcat(buf_eta," ");
   return buf_eta;
}
const char *Speedometer::GetETAStrSFromTime(long s)
{
   GetETAStrFromTime(s);
   if(buf_eta[0])
      strcat(buf_eta," ");
   return buf_eta;
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
