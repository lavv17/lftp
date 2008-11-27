/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef POLLVEC_H
#define POLLVEC_H

#include <sys/types.h>
CDECL_BEGIN
#include <poll.h>
CDECL_END

#include "xarray.h"

class PollVec
{
   xarray<pollfd> fds;
   int timeout;

public:
   void Init();
   PollVec();

   void	 Empty()
      {
	 fds.set_length(0);
	 timeout=-1;
      }
   bool IsEmpty()
      {
	 return fds.length()==0 && timeout==-1;
      }
   void	 Merge(const PollVec&);
   void	 Block();

   void SetTimeout(int t);
   void AddTimeout(int t);
   void AddFD(int fd,int events);
   void NoWait() { SetTimeout(0); }

   int GetTimeout() { return timeout; }
};

#endif /* POLLVEC_H */
