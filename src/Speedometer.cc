/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#include <config.h>
#include <math.h>
#include "Speedometer.h"
#include "misc.h"
#include "xstring.h"
#include "ResMgr.h"

#undef super
#define super SMTask

char Speedometer::buf_rate[40];
char Speedometer::buf_eta[40];

Speedometer::Speedometer(int c)
{
   period=c;
   rate=0;
   last_second=now;
   start=now;
   last_bytes=0;
   terse=true;
}
bool Speedometer::Valid()
{
   return now>start+2 && now<last_bytes+period;
}
int Speedometer::Do()
{
   if(Valid())
      Timeout(1000);
   return STALL;
}
float Speedometer::Get()
{
   Add(0);
   return rate;
}
void Speedometer::Add(int b)
{
   float div=period;
   if(start>now)
      start=now;  // time was adjusted?
   if(now<last_second)
      last_second=now;
   if(now-start<div)
      div=now-start+1;
   rate*=pow(double(div-1)/div,now-last_second);
   rate+=b/div;
   last_second=now;
   if(b>0)
      last_bytes=now;
}
const char *Speedometer::GetStr(float r)
{
   buf_rate[0]=0;
   if(r<1)
      return "";
   if(r<1024)
      sprintf(buf_rate,_("%.0fb/s"),r);
   else if(r<1024*1024)
      sprintf(buf_rate,_("%.1fK/s"),r/1024.);
   else
      sprintf(buf_rate,_("%.2fM/s"),r/1024./1024.);
   return buf_rate;
}
const char *Speedometer::GetETAStrFromSize(long size)
{
   buf_eta[0]=0;

   if(!Valid() || Get()<1)
      return buf_eta;

   return GetETAStrFromTime(long(size/Get()+.5));
}
const char *Speedometer::GetETAStrFromTime(long eta)
{
   buf_eta[0]=0;

   if(eta<0)
      return buf_eta;

   long eta2=0;
   long ueta=0;
   long ueta2=0;
   char letter=0;
   char letter2=0;

   // for translator: only first letter matters
   const char day_c=_("day")[0];
   const char hour_c=_("hour")[0];
   const char minute_c=_("minute")[0];
   const char second_c=_("second")[0];

   const char *tr_eta=_("eta:");

   if(terse)
   {
      if(eta>=DAY)
      {
	 ueta=(eta+DAY/2)/DAY;
	 eta2=eta-ueta*DAY;
	 letter=day_c;
	 if(ueta<10)
	 {
	    letter2=hour_c;
	    ueta2=((eta2<0?eta2+DAY:eta2)+HOUR/2)/HOUR;
	    if(ueta2>0 && eta2<0)
	       ueta--;
	 }
      }
      else if(eta>=HOUR)
      {
	 ueta=(eta+HOUR/2)/HOUR;
	 eta2=eta-ueta*HOUR;
	 letter=hour_c;
	 if(ueta<10)
	 {
	    letter2=minute_c;
	    ueta2=((eta2<0?eta2+HOUR:eta2)+MINUTE/2)/MINUTE;
	    if(ueta2>0 && eta2<0)
	       ueta--;
	 }
      }
      else if(eta>=MINUTE)
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
	 sprintf(buf_eta,"%s%ld%c%ld%c",tr_eta,ueta,letter,ueta2,letter2);
      else
	 sprintf(buf_eta,"%s%ld%c",tr_eta,ueta,letter);
   }
   else // verbose eta (by Ben Winslow)
   {
      long unit;
      strcpy(buf_eta, tr_eta);

      if(eta>=DAY)
      {
	 unit=eta/DAY;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, day_c);
      }
      if(eta>=HOUR)
      {
	 unit=(eta/HOUR)%24;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, hour_c);
      }
      if(eta>=MINUTE)
      {
	 unit=(eta/MINUTE)%60;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, minute_c);
      }
      unit=eta%60;
      sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, second_c);
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
const char *Speedometer::GetETAStrSFromSize(long s)
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

ResDecl res_eta_terse("xfer:eta-terse",  "yes",ResMgr::BoolValidate,0);
void Speedometer::Reconfig(const char *n)
{
   terse=res_eta_terse.Query(0);
}
