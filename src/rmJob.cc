/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "rmJob.h"
#include "plural.h"

rmJob::rmJob(FileAccess *s,ArgV *a) : SessionJob(s)
{
   done=false;
   quiet=false;
   failed=file_count=0;

   args=a;
   args->rewind();

   mode=FA::REMOVE;

   curr=first=0;
   args->rewind();
}

rmJob::~rmJob()
{
   delete args;
   args=0;
}

int rmJob::Do()
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
   if(session->IsClosed())
   {
      session->Open(curr,mode);
   }
   int res=session->Done();
   if(res==FA::IN_PROGRESS)
      return STALL;
   if(res<0)
   {
      failed++;
      if(!quiet)
	 fprintf(stderr,"%s: %s\n",args->a0(),session->StrError(res));
   }
   file_count++;
   session->Close();
   curr=args->getnext();
   return MOVED;
}

void  rmJob::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(Done() || !curr)
      return;
   printf("\t`%s' [%s]\n",curr,session->CurrentStatus());
}

void  rmJob::ShowRunStatus(StatusLine *s)
{
   if(Done() || !curr)
      s->Show("");
   else
      s->Show("%s `%s' [%s]",args->getarg(0),curr,session->CurrentStatus());
}

void  rmJob::SayFinal()
{
   if(failed==file_count)
      return;
   const char *op=args->a0();
   if(file_count==1)
      printf(_("%s ok, `%s' removed\n"),op,first);
   else if(failed)
   {
      if(mode==FA::REMOVE)
	 printf(_("%s failed for %d of %d directories\n"),op,failed,file_count);
      else
	 printf(_("%s failed for %d of %d files\n"),op,failed,file_count);
   }
   else
   {
      if(mode==FA::REMOVE)
	 printf(plural("%s ok, %d file$|s$ removed\n",file_count),
	    op,file_count);
      else
	 printf(plural("%s ok, %d director$y|ies$ removed\n",file_count),
	    op,file_count);
   }
}
