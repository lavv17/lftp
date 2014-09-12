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
#include "log.h"
#include "ProtoLog.h"

bool ProtoLog::WillOutput(int level)
{
   return Log::global && Log::global->WillOutput(level);
}

void  ProtoLog::Log2(int level,xstring& str)
{
   if(!WillOutput(level))
      return;
   str.chomp('\n');
   str.chomp('\r');
   str.append('\n');
   Log::global->Write(level,str);
}
void  ProtoLog::Log3(int level,const char *prefix,const char *str0)
{
   if(!WillOutput(level))
      return;
   Log2(level,xstring::get_tmp(prefix).append(str0));
}
void ProtoLog::LogError(int level,const char *fmt,...)
{
   if(!WillOutput(level))
      return;
   va_list v;
   va_start(v,fmt);
   Log2(level,xstring::get_tmp("**** ").vappendf(fmt,v));
   va_end(v);
}
void ProtoLog::LogNote(int level,const char *fmt,...)
{
   if(!WillOutput(level))
      return;
   va_list v;
   va_start(v,fmt);
   Log2(level,xstring::get_tmp("---- ").vappendf(fmt,v));
   va_end(v);
}
void ProtoLog::LogRecv(int level,const char *line)
{
   Log3(level,"<--- ",line);
}
void ProtoLog::LogSend(int level,const char *line)
{
   Log3(level,"---> ",line);
}
