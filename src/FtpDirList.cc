/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 ubuf=new FileInputBuffer(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      buf->PutEOF();

      const char *cache_buffer;
      int cache_buffer_size;
      ubuf->GetSaved(&cache_buffer,&cache_buffer_size);
      if(cache_buffer && cache_buffer_size>0)
      {
	 LsCache::Add(session,pattern,FA::LONG_LIST,
		      cache_buffer,cache_buffer_size);
      }

      return MOVED;
   }

   int m=STALL;

   if(len==0)
      return m;

   buf->Put(b,len);
   ubuf->Skip(len);
   m=MOVED;

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
   pattern=args->Combine(1);
}

FtpDirList::~FtpDirList()
{
   delete ubuf;
   xfree(pattern);
}

const char *FtpDirList::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%ld) [%s]"),
		     session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
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
