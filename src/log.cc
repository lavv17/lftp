/*
 * lftp and utils
 *
 * Copyright (c) 1998-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <stdarg.h>
#include "trio.h"
#include "xmalloc.h"
#include "xstring.h"
#include "log.h"
#include "SMTask.h"

Log *Log::global=new Log;

void Log::Init()
{
   output=-1;
   need_close_output=false;
   tty_cb=0;
   enabled=false;
   level=0;
   tty=false;
   show_pid=true;
   show_time=true;
   show_context=true;
   at_line_start=true;
}

bool Log::WillOutput(int l)
{
   if(!enabled || l>level || output==-1)
      return false;
   if(tty)
   {
      pid_t pg=tcgetpgrp(output);
      if(pg!=(pid_t)-1 && pg!=getpgrp())
	 return false;
   }
   return true;
}

void Log::Write(int l,const char *s)
{
   if(!WillOutput(l))
      return;
   DoWrite(s);
}
void Log::DoWrite(const char *s)
{
   if(!s || !*s)
      return;
   if(at_line_start)
   {
      if(tty_cb && tty)
	 tty_cb();
      if(show_pid)
      {
	 char *pid=string_alloca(15);
	 pid[14]=0;
	 snprintf(pid,14,"[%ld] ",(long)getpid());
	 write(output,pid,strlen(pid));
      }
      if(show_time)
      {
	 time_t t=now;
	 char *ts=string_alloca(21);
	 strftime(ts,21,"%Y-%m-%d %H:%M:%S ",localtime(&t));
	 write(output,ts,20);
      }
      if(show_context)
      {
	 const char *ctx=current->GetLogContext();
	 if(ctx)
	 {
	    write(output,ctx,strlen(ctx));
	    write(output," ",1);
	 }
      }
   }
   int len=strlen(s);
   write(output,s,len);
   at_line_start=(s[len-1]=='\n');
}

int Log::Do()
{
   return STALL;
}

void Log::Format(int l,const char *f,...)
{
   if(!WillOutput(l))
      return;

   static char *buf=0;
   static int buf_alloc;
   va_list v;

   if(buf==0)
      buf=(char*)xmalloc(buf_alloc=1024);

   for(;;)
   {
      va_start(v,f);
      int res=vsnprintf(buf,buf_alloc,f,v);
      va_end(v);
      if(res>=0 && res<buf_alloc)
	 break;
      if(res==buf_alloc)
	 res*=2;
      if(res==-1)
	 res=buf_alloc*2;
      buf=(char*)xrealloc(buf,buf_alloc=res);
   }

   DoWrite(buf);
}

void Log::Cleanup()
{
   delete global;
}
Log::~Log()
{
   CloseOutput();
}
