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

#include <config.h>
#include <stdio.h>
#include <assert.h>
#include "PollVec.h"
#include "xmalloc.h"

void  PollVec::Empty()
{
   fds_num=0;
   timeout=-1;
}

void PollVec::Init()
{
   fds=0;
   fds_num=0;
   fds_allocated=0;
   timeout=-1;
}

PollVec::PollVec()
{
   Init();
}
PollVec::~PollVec()
{
   xfree(fds);
}

void PollVec::SetTimeout(int t)
{
   if(t==0)
      Empty();
   timeout=t;
}

void PollVec::AddTimeout(int t)
{
   if(t==-1)
      return;
   if(timeout!=-1 && timeout<t)
      return;
   SetTimeout(t);
}

void PollVec::AddFD(int fd,int mask)
{
   if(timeout==0)
      return;
   for(int i=0; i<fds_num; i++)
   {
      if(fds[i].fd==fd)
      {
	 fds[i].events|=mask;
	 return;
      }
   }
   if(fds_num+1>fds_allocated)
   {
      fds_allocated=fds_num+16;
      fds=(struct pollfd*)xrealloc(fds,fds_allocated*sizeof(*fds));
   }
   fds[fds_num].fd=fd;
   fds[fds_num].events=mask;
   fds_num++;
}

void  PollVec::Block() const
{
   if(timeout==0)
      return;

   if(fds_num==0)
   {
      if(/*async==0 && */ timeout==-1)
      {
	 /* dead lock */
	 fprintf(stderr,_("%s: BUG - deadlock detected\n"),"PollVec::Block");
      	 poll(0,0,1000);
	 return;
      }
      poll(0,0,timeout);
      return;
   }

   poll(fds,fds_num,timeout);
   return;
}

void PollVec::Merge(const PollVec &p)
{
   AddTimeout(p.timeout);
   if(timeout==0)
      return;
   for(int i=0; i<p.fds_num; i++)
      AddFD(p.fds[i].fd,p.fds[i].events);
}
