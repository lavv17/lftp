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
#include "xalloca.h"

void  PollVec::Empty()
{
   Waiting *scan,*next;
   for(scan=chain; scan; scan=next)
   {
      next=scan->next;
      delete scan;
   }
   chain=0;
}

Waiting *Waiting::DupChain()
{
   Waiting *new_chain;
   Waiting **new_ptr=&new_chain;
   Waiting *w=this;

   while(w)
   {
      *new_ptr=new Waiting(*w);
      new_ptr=&((*new_ptr)->next);
      w=w->next;
   }
   *new_ptr=0;
   return new_chain;
}

void  PollVec::Merge(const PollVec& p)
{
   Waiting **scan;
   for(scan=&chain; *scan; scan=&((*scan)->next));
   *scan=p.chain->DupChain();
}

PollVec::PollVec(Waiting::Type t)
{
   chain=new Waiting;
   chain->wait_type=t;
   chain->next=0;
}

PollVec::PollVec(int fd,int ev)
{
   assert(fd>=0);
   chain=new Waiting;
   chain->wait_type=Waiting::POLLFD;
   chain->next=0;
   chain->pfd.fd=fd;
   chain->pfd.events=ev;
}

PollVec::PollVec(int t)
{
   chain=new Waiting;
   chain->wait_type=Waiting::TIMEOUT;
   chain->next=0;
   chain->timeout=t;
}

void  PollVec::Block() const
{
   int nfd=0;
   Waiting *scan;
   int	 cur_timeout=-1;
   int	 async=0;

   for(scan=chain; scan; scan=scan->next)
   {
      switch(scan->wait_type)
      {
      case(Waiting::POLLFD):
	 nfd++;
	 break;
      case(Waiting::TIMEOUT):
	 if(cur_timeout>scan->timeout || cur_timeout==-1)
	 {
	    cur_timeout=scan->timeout;
	    if(cur_timeout==0)
	       return;
	 }
	 break;
      case(Waiting::ASYNCWAIT):
	 async++;
	 break;
      case(Waiting::NOWAIT):
	 return;
      }
   }

   if(nfd==0)
   {
      if(async==0 && cur_timeout==-1)
      {
	 /* dead lock */
	 fprintf(stderr,"%s: deadlock detected\n","PollVec::Block");
      	 cur_timeout=1000;
      }
      poll(0,0,cur_timeout);
      return;
   }

   pollfd *pfd=(pollfd*)alloca(nfd*sizeof(pollfd));
   int i;

   for(i=0,scan=chain; scan; scan=scan->next)
   {
      if(scan->wait_type==Waiting::POLLFD)
      {
	 int j;
	 for(j=0; j<i; j++)
	 {
	    if(pfd[j].fd==scan->pfd.fd)
	    {
	       // merge two pollfd's (workaround for some systems)
	       pfd[j].events|=scan->pfd.events;
	       nfd--;
	       break;
	    }
	 }
	 if(j>=i)
	    pfd[i++]=scan->pfd;
      }
   }

   poll(pfd,nfd,cur_timeout);
   return;
}
