/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "trio.h"
#include "PollVec.h"

static inline bool operator<(const timeval& a,const timeval& b)
{
   if(a.tv_sec!=b.tv_sec)
      return a.tv_sec<b.tv_sec;
   return a.tv_usec<b.tv_usec;
}

void PollVec::AddTimeoutU(unsigned t)
{
   struct timeval new_timeout={static_cast<time_t>(t/1000000),static_cast<suseconds_t>(t%1000000)};
   if(tv_timeout.tv_sec<0 || new_timeout<tv_timeout)
      SetTimeout(new_timeout);
}

void PollVec::AddFD(int fd,int mask)
{
   if(mask&IN)
      FD_SET(fd,&in);
   if(mask&OUT)
      FD_SET(fd,&out);
   if(nfds<=fd)
      nfds=fd+1;
}
bool PollVec::FDReady(int fd,int mask)
{
   bool res=false;
   if(mask&IN)
      res|=(!FD_ISSET(fd,&in_polled) || FD_ISSET(fd,&in_ready));
   if(mask&OUT)
      res|=(!FD_ISSET(fd,&out_polled) || FD_ISSET(fd,&out_ready));
   return res;
}
void PollVec::FDSetNotReady(int fd,int mask)
{
   if(mask&IN)
      FD_CLR(fd,&in_ready);
   if(mask&OUT)
      FD_CLR(fd,&out_ready);
}

void  PollVec::Block()
{
   if(nfds<1 && tv_timeout.tv_sec<0)
   {
      /* dead lock */
      fprintf(stderr,_("%s: BUG - deadlock detected\n"),"PollVec::Block");
      tv_timeout.tv_sec=1;
   }

   in_ready=in_polled=in;
   out_ready=out_polled=out;
   timeval *select_timeout=0;
   if(tv_timeout.tv_sec!=-1)
      select_timeout=&tv_timeout;
   select(nfds,&in_ready,&out_ready,0,select_timeout);
}
