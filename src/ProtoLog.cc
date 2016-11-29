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

#include <config.h>
#include "log.h"
#include "ProtoLog.h"

ProtoLog::Tags *ProtoLog::tags;

void ProtoLog::init_tags()
{
   if(!tags)
      tags=new Tags();
   if(!tags->recv)
      tags->Reconfig(0);
}

bool ProtoLog::WillOutput(int level)
{
   return Log::global && Log::global->WillOutput(level);
}

void ProtoLog::Log2(int level,xstring& str)
{
   if(!WillOutput(level))
      return;
   str.chomp('\n');
   str.chomp('\r');
   str.append('\n');
   Log::global->Write(level,str);
}
void ProtoLog::Log3(int level,const char *prefix,const char *str0)
{
   if(!WillOutput(level))
      return;
   Log2(level,xstring::get_tmp(prefix).append(str0));
}
void ProtoLog::LogVF(int level,const char *prefix,const char *fmt,va_list v)
{
   if(!WillOutput(level))
      return;
   Log2(level,xstring::get_tmp(prefix).vappendf(fmt,v));
}
void ProtoLog::LogError(int level,const char *fmt,...)
{
   if(!WillOutput(level))
      return;
   init_tags();
   va_list v;
   va_start(v,fmt);
   LogVF(level,tags->error,fmt,v);
   va_end(v);
}
void ProtoLog::LogNote(int level,const char *fmt,...)
{
   if(!WillOutput(level))
      return;
   init_tags();
   va_list v;
   va_start(v,fmt);
   LogVF(level,tags->note,fmt,v);
   va_end(v);
}
void ProtoLog::LogRecv(int level,const char *line)
{
   init_tags();
   Log3(level,tags->recv,line);
}
void ProtoLog::LogSend(int level,const char *line)
{
   init_tags();
   Log3(level,tags->send,line);
}
void ProtoLog::LogRecvF(int level,const char *fmt,...)
{
   init_tags();
   va_list v;
   va_start(v,fmt);
   LogVF(level,tags->recv,fmt,v);
   va_end(v);
}
void ProtoLog::LogSendF(int level,const char *fmt,...)
{
   init_tags();
   va_list v;
   va_start(v,fmt);
   LogVF(level,tags->send,fmt,v);
   va_end(v);
}
