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

#include <config.h>
#include "trio.h"
#include "PollVec.h"

void PollVec::Init()
{
   timeout=-1;
}

PollVec::PollVec()
{
   Init();
}

void PollVec::SetTimeout(int t)
{
   if(t==0)
      Empty();
   timeout=t;
}

void PollVec::AddTimeout(int t)
{
   if(t<0)
      t=0;
   if(timeout>=0 && timeout<t)
      return;
   SetTimeout(t);
}

void PollVec::AddFD(int fd,int mask)
{
   if(timeout==0)
      return;
   for(int i=0; i<fds.count(); i++)
   {
      if(fds[i].fd==fd)
      {
	 fds[i].events|=mask;
	 return;
      }
   }
   pollfd add;
   memset(&add,0,sizeof(add));
   add.fd=fd;
   add.events=mask;
   fds.append(add);
}

void  PollVec::Block()
{
   if(timeout==0)
      return;

   if(fds.count()==0)
   {
      if(/*async==0 && */ timeout<0)
      {
	 /* dead lock */
	 fprintf(stderr,_("%s: BUG - deadlock detected\n"),"PollVec::Block");
      	 poll(0,0,1000);
	 return;
      }
      poll(0,0,timeout);
      return;
   }

   poll(fds.get_non_const(),fds.count(),timeout);
   return;
}

void PollVec::Merge(const PollVec &p)
{
   if(p.timeout>=0)
      AddTimeout(p.timeout);
   if(timeout==0)
      return;
   for(int i=0; i<p.fds.count(); i++)
      AddFD(p.fds[i].fd,p.fds[i].events);
}
