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

#define super XferJob

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
      if(dl)
      {
	 if(dl->Error())
	 {
	    eprintf("%s: %s\n",op,dl->ErrorText());
	    failed++;
	    NextFile();
	    return MOVED;
	 }
	 if(in_buffer==buffer_size)
	 {
	    if(buffer_size<0x10000)
	    {
	       if(buffer==0)
		  buffer=(char*)xmalloc(buffer_size=0x1000);
	       else
		  buffer=(char*)xrealloc(buffer,buffer_size*=2);
	    }
	    else
	    {
	       dl->Suspend();
	       goto try_write;
	    }
	 }
	 dl->Resume();
	 const char *tmpbuf=0;
	 dl->Get(&tmpbuf,&res);
	 if(tmpbuf==0)
	 {
	    // EOF
	    got_eof=true;
	    m=MOVED;
	 }
	 else if(res>0)
	 {
	    if((unsigned)res > buffer_size-in_buffer)
	       res=buffer_size-in_buffer;
	    memcpy(buffer+in_buffer,tmpbuf,res);
	    in_buffer+=res;
	    offset+=res;
	    dl->Skip(res);

	    CountBytes(res);
	 }
      }
      else // !dl
      {
	 res=TryRead(session);
	 if(res<0 && res!=FA::DO_AGAIN)
	 {
	    NextFile();
	    return MOVED;
	 }
	 else if(res>=0)
	 {
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
   }
try_write:
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

LsJob::LsJob(FileAccess *s,FDStream *l,ArgV *a,int mode) : XferJob(s)
{
   op="ls";
   local=l;
   if(!local)
      local=new FDStream(1,"<stdout>");
   line_buf=true;
   this->mode=mode;

   print_run_status=!local->usesfd(1);
   args=a;
   arg=a->Combine(1);
   XferJob::NextFile(arg);

   from_cache=false;
   cache_buffer=0;
   cache_buffer_size=0;

   dl=0;
   if(mode==FA::LONG_LIST)
      dl=session->MakeDirList(args);
   if(!dl)
   {
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
}
LsJob::~LsJob()
{
   xfree(arg); arg=0;
   delete args;
   if(local)
   {
      delete local;
      local=0;
   }
   xfree(cache_buffer);
   cache_buffer=0;
   if(dl)
      delete dl;
}

void LsJob::NextFile()
{
   XferJob::NextFile(0);
   if(dl)
   {
      delete dl;
      dl=0;
   }
}

void LsJob::NoCache()
{
   if(dl)
   {
      dl->UseCache(false);
      return;
   }
   if(from_cache)
   {
      session->Open(arg,mode);
      if(cache_buffer)
      {
	 xfree(cache_buffer);
	 cache_buffer=0;
	 cache_buffer_size=0;
      }
      from_cache=false;
   }
   session->UseCache(false);
}

void  LsJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;
   if(Done())
   {
      s->Show("");
      return;
   }

   if(!dl)
      super::ShowRunStatus(s);
   else
      s->Show("%s",dl->Status());
}
