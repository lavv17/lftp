/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "FileAccess.h"
#include "FtpDirList.h"
#include "LsCache.h"
#include "ArgV.h"

#define super DirList

int FtpDirList::Do()
{
   if(done)
      return STALL;

   if(buf->Eof())
   {
      done=true;
      return MOVED;
   }

   if(!ubuf)
   {
      char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,pattern,FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size))
      {
	 from_cache=true;
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 ubuf=new FileInputBuffer(session);
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(ubuf->Eof() && len==upos) // eof
   {
      buf->PutEOF();
      if(!from_cache)
      {
	 const char *cache_buffer;
	 int cache_buffer_size;
	 ubuf->Get(&cache_buffer,&cache_buffer_size);
	 if(cache_buffer && cache_buffer_size>0)
	 {
	    LsCache::Add(session,pattern,FA::LONG_LIST,
			   cache_buffer,cache_buffer_size);
	 }
      }
      return MOVED;
   }
   int m=STALL;
   b+=upos;
   len-=upos;
   while(len>0)
   {
      const char *cr=(const char *)memchr(b,'\r',len);
      if(!cr)
      {
	 buf->Put(b,len);
	 upos+=len;
	 m=MOVED;
	 break;
      }
      else
      {
	 if(cr-b>0)
	 {
	    buf->Put(b,cr-b);
	    upos+=(cr-b);
	    m=MOVED;
	    len-=cr-b;
	    b=cr;
	 }
	 if(len==1)
	    break;
	 if(b[1]!='\n')
	 {
	    buf->Put(b,1);
	    m=MOVED;
	 }
	 upos++;
	 b++,len--;
      }
   }
   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

FtpDirList::FtpDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   upos=0;
   from_cache=false;
   pattern=args->Combine(1);
}

FtpDirList::~FtpDirList()
{
   delete ubuf;
   xfree(pattern);
}

const char *FtpDirList::Status()
{
   return "FtpDirList";	// FIXME
}

void FtpDirList::Suspend()
{
   if(ubuf)
      ubuf->Suspend();
   super::Suspend();
}
void FtpDirList::Resume()
{
   super::Resume();
   if(ubuf)
      ubuf->Resume();
}
