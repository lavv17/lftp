/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>

#include <errno.h>
#include "TreatFileJob.h"
#include "StatusLine.h"
#include "ArgV.h"

TreatFileJob::TreatFileJob(FileAccess *s,ArgV *a) : SessionJob(s)
{
   done=false;
   quiet=false;
   failed=file_count=0;
   url_session=0;

   args=a;

   curr=first=0;
   args->rewind();

   op=args->a0();
}

TreatFileJob::~TreatFileJob()
{
   delete args;
   Reuse();
}

int TreatFileJob::Do()
{
   if(Done())
      return STALL;
   if(!curr)
   {
      curr=args->getnext();
      if(!curr)
      {
	 done=true;
	 return MOVED;
      }
      if(!first)
	 first=curr;
   }
   if(Session()->IsClosed())
      TreatCurrent();
   int res=Session()->Done();
   if(res==FA::IN_PROGRESS)
      return STALL;
   if(res<0)
   {
      failed++;
      if(!quiet)
	 fprintf(stderr,"%s: %s\n",op,Session()->StrError(res));
   }
   file_count++;
   Session()->Close();
   Reuse();
   curr=0;
   return MOVED;
}

void  TreatFileJob::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(Done() || !curr)
      return;
   printf("\t`%s' [%s]\n",curr,Session()->CurrentStatus());
}

void  TreatFileJob::ShowRunStatus(StatusLine *s)
{
   if(curr && !Done())
      s->Show("%s `%s' [%s]",op,curr,Session()->CurrentStatus());
}

void  TreatFileJob::AddFile(const char *f)
{
   args->Append(f);
}
