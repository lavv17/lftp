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
#include <stdio.h>
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
}

void Log::Write(int l,const char *s)
{
   if(!enabled || l>level)
      return;
   if(output==-1)
      return;
   if(tty)
   {
      pid_t pg=tcgetpgrp(output);
      if(pg==(pid_t)-1)
	 tty=false;
      else if(pg!=getpgrp())
	 return;
   }
   if(tty_cb && tty)
      tty_cb();
   write(output,s,strlen(s));
}

int Log::Do()
{
   return STALL;
}

void Log::Format(int l,const char *f,...)
{
   static char *buf=0;
   static int buf_alloc;
   va_list v;
   va_start(v,f);

   if(buf==0)
      buf=(char*)xmalloc(buf_alloc=1024);

#ifdef HAVE_VSNPRINTF
   for(;;)
   {
      int res=vsnprintf(buf,buf_alloc,f,v);
      if(res>=0 && res<buf_alloc)
	 break;
      if(res==buf_alloc)
	 res*=2;
      if(res==-1)
	 res=buf_alloc*2;
      buf=(char*)xrealloc(buf,buf_alloc=res);
   }
#else
   vsprintf(buf,f,v);
#endif

   va_end(v);
   Write(l,buf);
}

Log::~Log()
{
   CloseOutput();
}
