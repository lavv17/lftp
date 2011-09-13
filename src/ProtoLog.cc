/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2011 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id: ProtoLog.cc,v 1.1 2009/07/17 12:50:46 lav Exp $ */

#include <config.h>
#include "log.h"
#include "ProtoLog.h"

void  ProtoLog::Log2(int level,xstring& str)
{
   str.chomp('\n');
   str.chomp('\r');
   str.append('\n');
   Log::global->Write(level,str);
}
void  ProtoLog::Log3(int level,const char *prefix,const char *str0)
{
   xstring &str=xstring::get_tmp(prefix);
   str.append(str0);
   Log2(level,str);
}
void ProtoLog::LogError(int level,const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);
   xstring &str=xstring::get_tmp("**** ");
   str.vappendf(fmt,v);
   Log2(level,str);
   va_end(v);
}
void ProtoLog::LogNote(int level,const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);
   xstring &str=xstring::get_tmp("---- ");
   str.vappendf(fmt,v);
   Log2(level,str);
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
