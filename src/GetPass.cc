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

#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

#include "xmalloc.h"
#include "GetPass.h"
#include "CharReader.h"
#include "SignalHook.h"

char *GetPass(const char *prompt)
{
   static char *oldpass=0;
   static int tty_fd=-2;
   static FILE *f=0;

   xfree(oldpass);
   oldpass=0;

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

   if(f==0)
      f=fdopen(tty_fd,"r");
   if(f==0)
      return 0;

   write(tty_fd,prompt,strlen(prompt));


   struct termios tc;
   tcgetattr(tty_fd,&tc);
   tcflag_t old_lflag=tc.c_lflag;
   tc.c_lflag&=~ECHO;
   tcsetattr(tty_fd,TCSANOW,&tc);

   oldpass=readline_from_file(f);

   tc.c_lflag=old_lflag;
   tcsetattr(tty_fd,TCSANOW,&tc);

   write(tty_fd,"\r\n",2);

   return oldpass;
}

char *readline_from_file(FILE *f)
{
   int	 size=0x800;
   char	 *line=(char*)xmalloc(size);
   int	 len=0;
   char  *ptr=line;

   for(;;)
   {
      CharReader r(fileno(f));
      int c;
      for(;;)
      {
	 SMTask::Schedule();
	 c=r.GetChar();
	 if(c!=r.NOCHAR)
	    break;
	 SMTask::Block();
	 if(SignalHook::GetCount(SIGINT)>0)
	 {
	    xfree(line);
	    return(NULL);
	 }
      }
      if(c==r.EOFCHAR && ptr==line)
      {
	 xfree(line);
	 return(NULL);
      }
      if(c==r.EOFCHAR || c=='\n')
      {
	 *ptr=0;
	 return(line);
      }
      if(len+2>=size)
      {
	 size*=2;
	 line=(char*)xrealloc(line,size);
	 ptr=line+len;
      }
      *ptr++=c;
      len++;
   }
}
