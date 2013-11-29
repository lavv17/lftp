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

#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "GetPass.h"
#include "CharReader.h"
#include "SignalHook.h"

const char *GetPass(const char *prompt)
{
   static xstring_c oldpass;
   static int tty_fd=-2;

   if(tty_fd==-2)
   {
      if(isatty(0))
	 tty_fd=0;
      else
      {
	 tty_fd=open("/dev/tty",O_RDONLY);
   	 if(tty_fd!=-1)
	    fcntl(tty_fd,F_SETFD,FD_CLOEXEC);
      }
   }
   if(tty_fd==-1)
      return 0;

   write(tty_fd,prompt,strlen(prompt));

   struct termios tc;
   tcgetattr(tty_fd,&tc);
   tcflag_t old_lflag=tc.c_lflag;
   tc.c_lflag&=~ECHO;
   tcsetattr(tty_fd,TCSANOW,&tc);

   oldpass.set_allocated(readline_from_file(tty_fd));

   tc.c_lflag=old_lflag;
   tcsetattr(tty_fd,TCSANOW,&tc);

   write(tty_fd,"\r\n",2);

   return oldpass;
}

char *readline_from_file(int fd)
{
   xstring line("");
   for(;;)
   {
      SMTaskRef<CharReader> rr(new CharReader(fd));
      CharReader& r=*rr.get_non_const();
      int c;
      for(;;)
      {
	 SMTask::Schedule();
	 c=r.GetChar();
	 if(c!=r.NOCHAR)
	    break;
	 SMTask::Block();
	 if(SignalHook::GetCount(SIGINT)>0)
	    return(xstrdup(""));
      }
      if(c==r.EOFCHAR && line.length()==0)
	 return(NULL);
      if(c==r.EOFCHAR || c=='\n')
	 return(line.borrow());
      line.append(c);
   }
}
