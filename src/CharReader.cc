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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "trio.h"
#include "CharReader.h"

int CharReader::Do()
{
   int m=STALL;
   if(ch!=NOCHAR)
      return m;
   if(!Ready(fd,POLLIN))
   {
      Block(fd,POLLIN);
      return m;
   }

   int oldfl=fcntl(fd,F_GETFL);
   if(!(oldfl&O_NONBLOCK))
      fcntl(fd,F_SETFL,oldfl|O_NONBLOCK);

   unsigned char c;
   int res=read(fd,&c,1);
   if(res==-1 && errno==EAGAIN)
      Block(fd,POLLIN);
   else if(res==-1 && errno==EINTR)
      m=MOVED;
   else if(res>0)
   {
      ch=c;
      m=MOVED;
   }
   else	 // eof or error.
   {
      ch=EOFCHAR;
      m=MOVED;
   }

   if(!(oldfl&O_NONBLOCK))
      fcntl(fd,F_SETFL,oldfl);

   if(res==-1 && ch==EOFCHAR)
      fprintf(stderr,"read(%d): %s\n",fd,strerror(errno));

   return m;
}
