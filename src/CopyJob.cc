/*
 * lftp and utils
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "CopyJob.h"

int CopyJob::Do()
{
   if(done)
      return STALL;
   if(c->Error())
   {
      eprintf("%s\n",c->ErrorText());
      done=true;
      return MOVED;
   }
   if(c->Done())
   {
      done=true;
      return MOVED;
   }
   return STALL;
}
int CopyJob::Done()
{
   return done;
}
int CopyJob::ExitCode()
{
   if(c->Error())
      return 1;
   return 0;
}

void CopyJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;
   if(c->Done() || c->Error())
      return;

   s->Show(_("`%s' at %lu %s%s%s%s"),name,c->GetPos(),
      c->GetPercentDoneStr(),c->GetRateStr(),
      c->GetETAStr(),c->GetStatus());
}

CopyJob::CopyJob(FileCopy *c1,const char *name1)
{
   c=c1;
   name=xstrdup(name1);
   done=false;
   print_run_status=true;
}
CopyJob::~CopyJob()
{
   if(c) delete c;
   xfree(name);
}
