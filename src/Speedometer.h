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

#ifndef SPEEDOMETER_H
#define SPEEDOMETER_H

#include "SMTask.h"

class Speedometer : public SMTask
{
   int period;
   float rate;
   time_t last_second;
   time_t last_bytes;
   time_t start;
   bool terse;
   static char buf_eta[];
   static char buf_rate[];
public:
   Speedometer(int p);
   float Get();
   static const char *GetStr(float r);
   const char *GetStr() { return GetStr(Get()); }
   static const char *GetStrS(float r);
   const char *GetStrS() { return GetStrS(Get()); }
   const char *GetETAStrFromSize(long s);
   const char *GetETAStrSFromSize(long s);
   const char *GetETAStrFromTime(long t);
   const char *GetETAStrSFromTime(long t);
   bool Valid();
   void Add(int bytes);
   void Reset();
   void SetPeriod(int p) { period=p; }
   void SetTerseETA(bool t) { terse=t; }
   int Do();
   void Reconfig(const char *s);
};

#endif
