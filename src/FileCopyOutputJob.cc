/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "FileCopy.h"
#include "OutputJob.h"

FileCopyPeerOutputJob::FileCopyPeerOutputJob(OutputJob *new_o)
   : FileCopyPeer(PUT)
{
   o=new_o;
   DontCopyDate();
}

int FileCopyPeerOutputJob::Put_LL(const char *buf,int len)
{
   off_t io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   if(len==0 && eof)
      return 0;

   if(o->Full())
      return 0;

   o->Put(buf,len);

   seek_pos+=len; // mainly to indicate that there was some output.
   return len;
}

int FileCopyPeerOutputJob::Do()
{
   int m=STALL;

   if(broken || done)
      return m;
   if(o->Error())
   {
      broken=true;
      return MOVED;
   }
   if(eof && !in_buffer)
   {
      done=true;
      return MOVED;
   }

   if(!write_allowed)
      return m;

   while(in_buffer>0)
   {
      int res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 m=MOVED;
      }
      if(res<0)
	 return MOVED;
      if(res==0)
	 break;
   }
   return m;
}

void FileCopyPeerOutputJob::Fg()
{
   o->Fg();
   FileCopyPeer::Fg();
}
void FileCopyPeerOutputJob::Bg()
{
   o->Bg();
   FileCopyPeer::Bg();
}
