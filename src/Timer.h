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

#ifndef TIMER_H
#define TIMER_H

#include "SMTask.h"
#include "ResMgr.h"

class Timer : public SMTask
{
   Time start;
   Time stop;
   TimeInterval last_setting;
   const char *resource;
   const char *closure;
   void set_timeout();
public:
   Timer();
   Timer(const TimeDiff &);
   int Do();
   bool Stopped();
   void Set(const TimeDiff &d);
   void SetResource(const char *,const char *);
   void SetMilliSeconds(int ms) { Set(TimeDiff(0,ms)); }
   void Reset();
   void Reconfig(const char *);
};

#endif
