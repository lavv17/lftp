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

#include <unistd.h>
#include "CharReader.h"

int CharReader::Do()
{
   if(ch!=NOCHAR)
      return STALL;
   struct pollfd pfd;
   pfd.fd=fd;
   pfd.events=POLLIN;
   int res=poll(&pfd,1,0);
   if(res==-1
   || (res==1 && !(pfd.revents&(POLLIN|POLLNVAL))))
   {
      ch=EOFCHAR;
      return MOVED;
   }
   if(res==0)
   {
      block.Merge(PollVec(fd,POLLIN));
      return STALL;
   }
   unsigned char c;
   res=read(fd,&c,1);
   if(res==1)
      ch=c;
   else
      ch=EOFCHAR;
   return MOVED;
}
