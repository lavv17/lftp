/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#ifndef LOG_H
#define LOG_H

#include <unistd.h>
#include <stdlib.h>
#include <stdarg.h>
#include "SMTask.h"

class Log : public SMTask
{
   int output;
   bool need_close_output;
   bool tty;
   bool show_pid;
   bool show_time;
   bool show_context;
   bool at_line_start;
   typedef void (*tty_cb_t)();
   tty_cb_t tty_cb;

   void CloseOutput()
      {
	 if(need_close_output)
	    close(output);
	 output=-1;
	 need_close_output=false;
      }
   bool WillOutput(int l);

public:
   static SMTaskRef<Log> global;

   bool enabled;
   int level;

   void DoWrite(const char *str);
   void Write(int l,const char *str);
   void Format(int l,const char *fmt,...) PRINTF_LIKE(3,4);
   void vFormat(int l,const char *fmt,va_list v);

   void SetLevel(int l) { level=l; }
   void Enable()  { enabled=true;  }
   void Disable() { enabled=false; }

   void SetOutput(int o,bool need_close)
      {
	 CloseOutput();
	 output=o;
	 need_close_output=need_close;
	 if(output!=-1)
	    tty=isatty(output);
      }

   void SetCB(tty_cb_t cb) { tty_cb=cb; }

   bool IsTTY() { return tty; }

   void Init();
   Log() { Init(); }
   ~Log();

   int Do();

   void ShowPID(bool yes=true) { show_pid=yes; }
   void ShowTime(bool yes=true) { show_time=yes; }
   void ShowContext(bool yes=true) { show_context=yes; }
   void ShowNothing() { show_pid=show_time=show_context=false; }

   static void Cleanup();
};

#endif // LOG_H
