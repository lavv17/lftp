/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "log.h"
#include "SMTask.h"

Log *Log::global=0;

void Log::Init()
{
   output=-1;
   need_close_output=false;
   sl=0;
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
   if(tty && tcgetpgrp(output)!=getpgrp())
      return;
   if(sl)
      sl->Show("");
   write(output,s,strlen(s));
   block+=NoWait();
}

Log::~Log()
{
   CloseOutput();
}
