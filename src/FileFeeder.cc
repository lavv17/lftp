/*
 * lftp and utils
 *
 * Copyright (c) 1996-2004 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <errno.h>
#include <unistd.h>
#include "FileFeeder.h"

const char *FileFeeder::NextCmd(CmdExec *exec, const char *)
{
   int fd=in->getfd();
   if(fd<0)
   {
      if(in->error())
      {
	 fprintf(stderr,"source: %s\n",in->error_text);
	 return 0;
      }
      return "";
   }
   in->Kill(SIGCONT);
   int res=read(fd,buffer,buffer_size-1);
   if(res==0)
   {
      return 0;
   }
   if(res<0)
   {
      if(E_RETRY(errno))
      {
	 exec->Block(fd,POLLIN);
	 return "";
      }
      if(SMTask::NonFatalError(errno))
	 return "";
      perror("source");
      return 0;
   }
   buffer[res]=0;
   return buffer;
}
