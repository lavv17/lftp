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

#ifndef SPEEDOMETER_H
#define SPEEDOMETER_H

#include "SMTask.h"
#include "ResMgr.h"

class Speedometer : public ResClient
{
   int period;
   float rate;
   Time last_second;
   Time last_bytes;
   Time start;
   bool terse;
   const char *period_resource;
public:
   Speedometer(const char *p="xfer:rate-period");
   float Get();
   float Get() const { return rate; }
   static xstring& GetStr(float r);
   static xstring& GetStrProper(float r);
   xstring& GetStr();
   static const char *GetStrS(float r);
   const char *GetStrS();
   xstring& GetETAStrFromSize(off_t s);
   const char *GetETAStrSFromSize(off_t s);
   xstring& GetETAStrFromTime(long t);
   const char *GetETAStrSFromTime(long t);
   bool Valid();
   void Add(int bytes);
   void Reset();
   void SetPeriod(int p) { period=p; }
   void SetPeriodName(const char *p) { period_resource=p; Reconfig(0); }
   void SetTerseETA(bool t) { terse=t; }
   void Reconfig(const char *s);
};

#endif
