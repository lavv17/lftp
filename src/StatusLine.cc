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
#ifdef NEED_TRIO
#include "trio.h"
#define vsnprintf trio_vsnprintf
#endif
#include <stdarg.h>
#include "xstring.h"
#include "xmalloc.h"

#include "ResMgr.h"
#include "misc.h"
#include "StatusLine.h"

ResDecl status_interval ("cmd:status-interval",        "1000",   ResMgr::UNumberValidate,0);

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
   strcpy(shown,"");
   strcpy(def_title,"");
   not_term=!isatty(fd);
   LastWidth=GetWidth();
   Reconfig();
}

StatusLine::~StatusLine()
{
}

void StatusLine::Clear()
{
   char newstr[sizeof(shown)];

   newstr[0]=0;
   update(newstr);
   update_delayed=false;
   timer.force();

   WriteTitle(def_title, fd);
}

void StatusLine::DefaultTitle(const char *s)
{
   strncpy(def_title, s, sizeof(def_title));
   def_title[sizeof(def_title)-1] = 0;
}

void StatusLine::Show(const char *f,...)
{
   if(f==0 || f[0]==0)
   {
      Clear();
      return;
   }

   char newstr[sizeof(shown)];

   va_list v;
   va_start(v,f);
   vsnprintf(newstr,sizeof(newstr),f,v);
   va_end(v);

   if(timer.go()) {
      update(newstr);
      update_delayed=false;
      return;
   }

   if(!strcmp(to_be_shown,newstr))
      return;

   /* not yet */
   strcpy(to_be_shown,newstr);
   update_delayed=true;
   Timeout(timer.remaining());
}

void StatusLine::Reconfig(const char *name)
{
   if(name && strcmp(name, "cmd:status-interval"))
      return;

   timer.set_interval((int) status_interval.Query(0));
}

void StatusLine::WriteTitle(const char *s, int fd) const
{
   if(!(bool)ResMgr::Query("cmd:set-term-status", getenv("TERM")))
      return;

   subst_t subst[] = {
      { 'a', "\007" },
      { 'e', "\033" },
      { 'n', "\n" },
      { 's', "lftp" },
      { 'v', VERSION },

      { 'T', s },
      { 0, "" }
   };
   char *disp = Subst(ResMgr::Query("cmd:term-status", getenv("TERM")), subst);
   write(fd, disp, strlen(disp));
   xfree(disp);
}

void StatusLine::update(char *newstr)
{
   if(not_term)
      return;

   if(tcgetpgrp(fd)!=getpgrp())
      return;

   /* Don't write blank titles into the title; let Clear() do that. */
   if(newstr[0]) WriteTitle(newstr, fd);

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
   if(timer.go())
   {
      update(to_be_shown);
      update_delayed=false;
      return STALL;
   }
   Timeout(timer.remaining());
   return STALL;
}
