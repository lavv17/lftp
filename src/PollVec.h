/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef POLLVEC_H
#define POLLVEC_H

#include <sys/types.h>

#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h>
#endif

#ifdef HAVE_SYS_POLL_H
# include <sys/poll.h>
#else
# include <poll.h>
#endif

class PollVec
{
   struct pollfd *fds;
   int fds_num;
   int fds_allocated;

   int timeout;

public:
   void Init();
   PollVec();

   void	 Empty();
   void	 Merge(const PollVec&);
   void	 Block() const;

   void SetTimeout(int t);
   void AddTimeout(int t);
   void AddFD(int fd,int events);
   void NoWait() { SetTimeout(0); }

   int GetTimeout() { return timeout; }

   ~PollVec();
};

#endif /* POLLVEC_H */
