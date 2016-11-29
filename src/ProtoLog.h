/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef PROTOLOG_H
#define PROTOLOG_H

#include "xstring.h"

class ProtoLog
{
   static bool WillOutput(int level);

   struct Tags : public ResClient {
      const char *recv;
      const char *send;
      const char *note;
      const char *error;
      void Reconfig(const char *n) {
	 if(n && strncmp(n,"log:prefix-",11))
	    return;
	 recv=Query("log:prefix-recv",0);
	 send=Query("log:prefix-send",0);
	 note=Query("log:prefix-note",0);
	 error=Query("log:prefix-error",0);
      }
   };
   static Tags *tags;
   static void init_tags();

public:
   static void Log2(int level,xstring& str);
   static void Log3(int level,const char *prefix,const char *str);
   static void LogVF(int level,const char *prefix,const char *fmt,va_list v);
   static void LogError(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogNote(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogRecv(int level,const char *line);
   static void LogSend(int level,const char *line);
   static void LogRecvF(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogSendF(int level,const char *fmt,...) PRINTF_LIKE(2,3);
};

#endif//PROTOLOG_H
