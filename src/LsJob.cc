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

/* $Id$ */

#include <config.h>
#include "LsJob.h"
#include "LsCache.h"
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

int strip_cr(char *buffer,int size)
{
   for(int i=0; i<size; i++)
   {
      if(buffer[i]=='\r')
      {
	 memmove(buffer+i,buffer+i+1,size-i-1);
	 i--;
	 size--;
      }
   }
   return size;
}

int   LsJob::Done()
{
   return curr==0 && local==0;
}

int   LsJob::Do()
{
   RateDrain();

   int m=STALL;
   int res;

   if(curr==0)
   {
      if(local)
      {
	 if(local->Done())
	 {
	    delete local;
	    local=0;
	    m=MOVED;
	 }
      }
      return m;
   }

   if(from_cache && !got_eof)
   {
      // we have cached data
      buffer=cache_buffer;
      in_buffer=cache_buffer_size;
      cache_buffer=0;
      got_eof=true;
   }
   if(in_buffer==0 && got_eof)
   {
      // we are done
      NextFile();
      m=MOVED;
      return m;
   }
   if(!got_eof)
   {
      res=TryRead(session);
      if(res<0 && res!=FA::DO_AGAIN)
      {
//	    local->remove_if_empty();
	 NextFile();
	 return MOVED;
      }
      else if(res>=0)
      {
	 int diff=res-strip_cr(buffer+in_buffer-res,res);
	 res-=diff;
	 in_buffer-=diff;
	 bytes_transferred-=diff;
	 offset-=diff;
	 m=MOVED;

	 if(res>0)
	 {
	    cache_buffer=(char*)xrealloc(cache_buffer,cache_buffer_size+res);
	    memcpy(cache_buffer+cache_buffer_size,buffer+in_buffer-res,res);
	    cache_buffer_size+=res;
	 }
	 else if(got_eof)
	 {
	    if(cache_buffer)
	    {
	       LsCache::Add(session,curr,mode,
			      cache_buffer,cache_buffer_size);
	       xfree(cache_buffer);
	       cache_buffer=0;
	    }
	 }
      }
   }

   res=TryWrite(local);
   if(res<0)
   {
      NextFile();
      return MOVED;
   }
   if(res>0)
      m=MOVED;

   return m;
}

LsJob::LsJob(FileAccess *s,FDStream *l,char *a,int mode) : XferJob(s)
{
   op="ls";
   local=l;
   if(!local)
      local=new FDStream(1,"<stdout>");
   line_buf=true;
   this->mode=mode;

   print_run_status=!local->usesfd(1);
   arg=a;
   XferJob::NextFile(arg);

   from_cache=false;
   cache_buffer=0;
   cache_buffer_size=0;

   if(LsCache::Find(session,arg,mode,
		     &cache_buffer,&cache_buffer_size))
   {
      from_cache=true;
   }
   else
   {
      session->Open(arg,mode);
   }
}
LsJob::~LsJob()
{
   xfree(arg); arg=0;
   if(local)
   {
      delete local;
      local=0;
   }
   xfree(cache_buffer);
   cache_buffer=0;
}

void LsJob::NextFile()
{
   XferJob::NextFile(0);
}

void LsJob::NoCache()
{
   if(from_cache)
   {
      session->Open(arg,FA::LONG_LIST);
      if(cache_buffer)
      {
	 xfree(cache_buffer);
	 cache_buffer=0;
	 cache_buffer_size=0;
      }
      from_cache=false;
   }
}
