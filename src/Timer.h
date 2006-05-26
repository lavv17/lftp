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
   void set_timeout() const;
public:
   Timer();
   Timer(const TimeDiff &);
   int Do();
   bool Stopped() const;
   void Stop() { stop=now; }
   void Set(const TimeInterval&);
   void Set(time_t s,int ms=0) { Set(TimeInterval(s,ms)); }
   void SetMilliSeconds(int ms) { Set(TimeInterval(0,ms)); }
   void SetResource(const char *,const char *);
   void Reset(const Time &t);
   void Reset() { Reset(now); }
   void Reset(const Timer &t) { Reset(t.GetStartTime()); }
   void Reconfig(const char *);
   const TimeInterval& GetLastSetting() const { return last_setting; }
/*   TimeInterval TimeRemains() const { return Stopped()?TimeInterval(0,0):last_setting-(now-start); }*/
   TimeDiff TimePassed() const { return now-start; }
   const Time &GetStartTime() const { return start; }
};

#endif
