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
#include "Timer.h"

void Timer::set_timeout()
{
   TimeDiff remains(stop,now);
   Timeout(remains.MilliSeconds());
}

void Timer::Set(const TimeDiff &diff)
{
   stop=now;
   stop+=diff;
   set_timeout();
}
int Timer::Do()
{
   set_timeout();
   return STALL;
}
bool Timer::Stopped()
{
   return now>=stop;
}
