/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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
   LastHeight=sz.ws_row;
   return(LastWidth=sz.ws_col);
#else /* !TIOCGWINSZ */
   return 80;
#endif
}

StatusLine::StatusLine(int new_fd)
{
   to_status_line = get_string_term_cap("tsl", "ts");
   from_status_line = get_string_term_cap("fsl", "fs");
   prev_line = get_string_term_cap("cuu1","up");

   fd=new_fd;
   update_delayed=false;
   next_update_title_only=false;
   strcpy(def_title,"");
   not_term=!isatty(fd);
   GetWidth();
}

StatusLine::~StatusLine()
{
   /* Don't leave a title behind. */
   WriteTitle("", fd);
}

void StatusLine::Clear(bool title_also)
{
   const char *empty="";
   update_timer.Stop();
   ShowN(&empty,1);
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

   char newstr[0x800];

   va_list v;
   va_start(v,f);
   vsnprintf(newstr,sizeof(newstr),f,v);
   va_end(v);
   newstr[sizeof(newstr)-1]=0;

   const char *s=newstr;
   ShowN(&s,1);
}

void StatusLine::ShowN(const char *const* newstr,int n)
{
   if(!update_delayed && shown.IsEqual(newstr,n))
      return;
   if(update_delayed && to_be_shown.IsEqual(newstr,n))
      return;

   if(!update_timer.Stopped())
   {
      /* not yet */
      to_be_shown.Assign(newstr,n);
      update_delayed=true;
   }
   else
   {
      update(newstr,n);
      update_delayed=false;
   }
}

const char *StatusLine::to_status_line;
const char *StatusLine::from_status_line;
const char *StatusLine::prev_line;

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

   xstring &disp=xstring::get_tmp();

   if(status_format && *status_format)
      SubstTo(disp, status_format, subst);
   else if(to_status_line && from_status_line)
      /* If we have no format, and we have both tsl and fsl, use them: */
      disp.vset(to_status_line, s, from_status_line, NULL);
   else
      return;

   write(fd, disp, disp.length());
}

void StatusLine::update(const char *const *newstr,int newstr_height)
{
   if(not_term)
      return;

   if(!in_foreground_pgrp())
      return;

   /* Don't write blank titles into the title; let Clear() do that. */
   if(newstr_height>0 && newstr[0][0]) WriteTitle(newstr[0], fd);

   if(next_update_title_only)
   {
      next_update_title_only=false;
      return;
   }

   int w=GetWidth();
   int mbflags=0;

   if(newstr_height>LastHeight)
      newstr_height=LastHeight;

   // clear old extra lines. Assume we are at beginning of last shown line.
   int j=shown.Count();
   if(!prev_line)    // if there is no way to go up, show a single line only.
      j=newstr_height=1;
   int i=j-newstr_height;
   char *spaces=string_alloca(w+1);
   memset(spaces,' ',w);
   spaces[w]=0;
   while(i-->0)
   {
      int tw=mbswidth(shown[--j],mbflags);
      write(fd,"\r",1);
      write(fd,spaces,tw);
      write(fd,"\r",1);
      write(fd,prev_line,strlen(prev_line));
   }
   // move to top of shown lines.
   while(--j>0)
      write(fd,prev_line,strlen(prev_line));

   int curr_line=0;
   while(curr_line<newstr_height)
   {
      const char *end=newstr[curr_line];
      int len=strlen(newstr[curr_line]);
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
      while(end>newstr[curr_line] && end[-1]==' ')
      {
	 end--;
	 wpos--;  // FIXME: assumption - space width is 1
      }

      if(end-newstr[curr_line]>0)
	 write(fd,newstr[curr_line],end-newstr[curr_line]);

      const char *shown_curr=(curr_line>=shown.Count()?"":shown[curr_line]);
      int dif=strlen(shown_curr)-(end-newstr[curr_line])+2;
      if(dif>(w-1)-wpos)
	 dif=(w-1)-wpos;
      if(dif>0)
	 write(fd,spaces,dif);
      write(fd,"\r",1);

      if(++curr_line<newstr_height)
	 write(fd,"\n",1);
   }

   shown.Assign(newstr,newstr_height);

   update_timer.SetResource("cmd:status-interval",0);
}

void StatusLine::WriteLine(const char *f,...)
{
   va_list v;
   va_start(v,f);
   xstring& newstr=xstring::vformat(f,v).append('\n');
   va_end(v);

   Clear();

   write(fd,newstr,newstr.length());
   update_delayed=false;
}

int StatusLine::Do()
{
   if(!update_delayed)
      return STALL;
   if(update_timer.Stopped())
   {
      update(to_be_shown.Set(),to_be_shown.Count());
      update_delayed=false;
      return STALL;
   }
   return STALL;
}
