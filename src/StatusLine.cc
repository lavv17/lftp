/*
 * lftp and utils
 *
 * Copyright (c) 1996-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "trio.h"
#include <stdarg.h>
#include <stdlib.h>
#include <mbswidth.h>
#include "xstring.h"
#include "xmalloc.h"

#include "ResMgr.h"
#include "misc.h"
#include "StatusLine.h"

#include "lftp_tinfo.h"

ResDecl res_status_interval ("cmd:status-interval", "0.8s", ResMgr::TimeIntervalValidate,ResMgr::NoClosure);

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
   next_update_title_only=false;
   strcpy(shown,"");
   strcpy(def_title,"");
   not_term=!isatty(fd);
   LastWidth=GetWidth();
   Reconfig();
}

StatusLine::~StatusLine()
{
   /* Don't leave a title behind. */
   WriteTitle("", fd);
}

void StatusLine::Clear(bool title_also)
{
   char newstr[sizeof(shown)];

   newstr[0]=0;
   update(newstr);
   update_delayed=false;
   update_timer.SetMilliSeconds(20);

   if(title_also)
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

   if(update_timer.Stopped())
   {
      update(newstr);
      update_delayed=false;
      return;
   }

   if(!strcmp(to_be_shown,newstr))
      return;

   /* not yet */
   strcpy(to_be_shown,newstr);
   update_delayed=true;
}

const char *StatusLine::to_status_line = get_string_term_cap("tsl", "ts");
const char *StatusLine::from_status_line = get_string_term_cap("fsl", "fs");

void StatusLine::WriteTitle(const char *s, int fd) const
{
   if(!ResMgr::QueryBool("cmd:set-term-status", getenv("TERM")))
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

   const char *status_format = ResMgr::Query("cmd:term-status", getenv("TERM"));

   char *disp;

   /* If we have no format, and we have both tsl and fsl, use them: */
   if((!status_format || !*status_format) && to_status_line && from_status_line)
      disp = xasprintf("%s%s%s", to_status_line, s, from_status_line);
   else
      disp = Subst(status_format, subst);

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

   if(next_update_title_only)
   {
      next_update_title_only=false;
      return;
   }

   int w=GetWidth();
   int mbflags=MBSW_ACCEPT_INVALID|MBSW_ACCEPT_UNPRINTABLE;
   char *end=newstr;
   int len=strlen(newstr);
   int wpos=0;
   while(len>0)
   {
      int ch_len=mblen(end,len);
      if(ch_len<1)
	 ch_len=1;
      int ch_width=mbsnwidth(end,ch_len,mbflags);
      if(wpos+ch_width>w-1)
	 break;
      end+=ch_len;
      len-=ch_len;
      wpos+=ch_width;
      if(wpos>=w-1)
	 break;
   }

   // FIXME: this assumes that multibyte chars cannot include ' '.
   while(end>newstr && end[-1]==' ')
   {
      end--;
      wpos--;  // FIXME: assumption - space width is 1
   }

   *end=0;

   if(!strcmp(shown,newstr))
      return;

   int dif=strlen(shown)-strlen(newstr)+2;

   strcpy(shown,newstr);

   while(dif-->0 && wpos<w-1)
   {
      *end++=' ';
      wpos++;  // FIXME: assumption - space width is 1
   }

   *end=0;

   if(end==newstr)
      return;

   *end++='\r';
   *end=0;

   write(fd,"\r",1);
   write(fd,newstr,end-newstr);

   update_timer.SetResource("cmd:status-interval",0);
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
   if(update_timer.Stopped())
   {
      update(to_be_shown);
      update_delayed=false;
      return STALL;
   }
   return STALL;
}
