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
#include <sys/types.h>
#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif
#include <unistd.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <stdarg.h>
#include "xstring.h"
#include "xmalloc.h"

#include "StatusLine.h"

int  StatusLine::GetWidth()
{
#ifdef TIOCGWINSZ
   struct winsize sz;
   sz.ws_col=sz.ws_row=0;
   ioctl(fd,TIOCGWINSZ,&sz);
   if(sz.ws_col==0)
      sz.ws_col=80;
   if(sz.ws_row==0)
      sz.ws_row=24;
   return(LastWidth=sz.ws_col);
#else /* !TIOCGWINSZ */
   return 80;
#endif
}

StatusLine::StatusLine(int new_fd)
{
   fd=new_fd;
   update_delayed=false;
   update_time=0;
   strcpy(shown,"");
   not_term=!isatty(fd);
   LastWidth=GetWidth();
}
StatusLine::~StatusLine()
{
}

void StatusLine::Show(const char *f,...)
{
   char newstr[sizeof(shown)];

   if(f==0 || f[0]==0)
   {
      newstr[0]=0;
      update(newstr);
      update_delayed=false;
      update_time=0;
      return;
   }

   va_list v;
   va_start(v,f);
   vsprintf(newstr,f,v);
   va_end(v);

   if(now>update_time)
   {
      update(newstr);
      update_delayed=false;
      update_time=now;
   }
   else if(strcmp(to_be_shown,newstr))
   {
      strcpy(to_be_shown,newstr);
      update_delayed=true;
      SMTask::block+=TimeOut(1000);
   }
}

void StatusLine::update(char *newstr)
{
   if(not_term)
      return;

   if(tcgetpgrp(fd)!=getpgrp())
      return;

   char *end=newstr+strlen(newstr);

   int w=GetWidth();
   if(end-newstr>=w)
      end=newstr+w-1;

   while(end>newstr && end[-1]==' ')
      end--;

   *end=0;

   if(!strcmp(shown,newstr))
      return;

   int dif=strlen(shown)-strlen(newstr)+2;

   strcpy(shown,newstr);

   while(dif-->0 && end-newstr<w)
      *end++=' ';

   *end=0;

   if(end==newstr)
      return;

   *end++='\r';
   *end=0;

   write(fd,"\r",1);
   write(fd,newstr,strlen(newstr));
}

void StatusLine::WriteLine(const char *f,...)
{
   char *newstr=(char*)alloca(sizeof(shown)+strlen(f)+64);

   va_list v;
   va_start(v,f);
   vsprintf(newstr,f,v);
   va_end(v);

   if(not_term || shown[0]==0)
   {
      strcat(newstr,"\n");
      write(fd,newstr,strlen(newstr));
      update_delayed=false;
      return;
   }

   char *end=newstr+strlen(newstr);
   while(end>newstr && end[-1]==' ')
      end--;
   *end=0;
   if(!strcmp(shown,newstr))
   {
      write(fd,"\n",1);
      return;
   }

   int dif=strlen(shown)-strlen(newstr)+2;
   int w=GetWidth();

   while(dif-->0 && end-newstr<w-1)
      *end++=' ';

   *end++='\n';
   *end=0;

   write(fd,"\r",1);
   write(fd,newstr,strlen(newstr));

   strcpy(shown,"");
   update_delayed=false;
}

int StatusLine::Do()
{
   if(!update_delayed)
      return STALL;
   if(now>update_time)
   {
      update(to_be_shown);
      update_delayed=false;
      update_time=now;
      return STALL;
   }
   Timeout(1000);
   return STALL;
}
