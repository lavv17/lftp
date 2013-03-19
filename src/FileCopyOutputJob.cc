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
#include "FileCopy.h"
#include "FileCopyOutputJob.h"

FileCopyPeerOutputJob::FileCopyPeerOutputJob(const JobRef<OutputJob>& new_o)
   : FileCopyPeer(PUT), o(new_o)
{
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
   if(eof && Size()==0)
   {
      done=true;
      return MOVED;
   }

   if(!write_allowed)
      return m;

   while(Size()>0)
   {
      int res=Put_LL(buffer+buffer_ptr,Size());
      if(res>0)
      {
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
