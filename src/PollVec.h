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

#ifndef POLLVEC_H
#define POLLVEC_H

#include <sys/types.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/select.h>
CDECL_BEGIN
#include <poll.h>
CDECL_END

class PollVec
{
   fd_set in;
   fd_set out;
   fd_set in_polled;
   fd_set out_polled;
   fd_set in_ready;
   fd_set out_ready;
   int nfds;
   struct timeval tv_timeout;

public:
   PollVec() {
      Empty();
      FD_ZERO(&in_polled);
      FD_ZERO(&out_polled);
      FD_ZERO(&in_ready);
      FD_ZERO(&out_ready);
   }

   void	 Empty()
      {
	 FD_ZERO(&in);
	 FD_ZERO(&out);
	 nfds=0;
	 tv_timeout.tv_sec=-1;
	 tv_timeout.tv_usec=0;
      }
   void	 Block();

   enum {
      IN=1,
      OUT=4,
   };

   void SetTimeout(const timeval &t) { tv_timeout=t; }
   void SetTimeoutU(unsigned t) {
      tv_timeout.tv_sec=t/1000000;
      tv_timeout.tv_usec=t%1000000;
   }
   void AddTimeoutU(unsigned t);
   void AddFD(int fd,int events);
   bool FDReady(int fd,int events);
   void FDSetNotReady(int fd,int events);
   void NoWait() { tv_timeout.tv_sec=tv_timeout.tv_usec=0; }
   bool WillNotBlock() { return tv_timeout.tv_sec==0 && tv_timeout.tv_usec==0; }
};

#endif /* POLLVEC_H */
