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
#include "mkdirJob.h"

mkdirJob::mkdirJob(FileAccess *s,ArgV *a) : SessionJob(s)
{
   quiet=false;
   failed=file_count=0;

   args=a;
   args->rewind();
   char *op=args->getarg(0);

   curr=first=0;
   opt_p=false;

   int opt;
   while((opt=args->getopt("p"))!=EOF)
   {
      if(opt=='p')
	 opt_p=true;
      else
	 return;
   }
   args->back();
   first=curr=args->getnext();
   if(curr==0)
   {
      fprintf(stderr,"Usage: %s [-p] files...\n",op);
      return;
   }

}

mkdirJob::~mkdirJob()
{
   delete args;
   args=0;
}

int mkdirJob::Do()
{
   if(Done())
      return STALL;
   if(session->IsClosed())
      session->Mkdir(curr,opt_p);

   int res=session->Done();
   if(res==Ftp::DO_AGAIN || res==Ftp::IN_PROGRESS)
      return STALL;
   if(res<0)
   {
      failed++;
      if(!quiet)
	 fprintf(stderr,"%s: %s\n",args->getarg(0),session->StrError(res));
   }
   file_count++;
   session->Close();
   curr=args->getnext();
   return MOVED;
}

void  mkdirJob::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(Done())
      return;
   printf("\t`%s' [%s]\n",curr,session->CurrentStatus());
}

void  mkdirJob::ShowRunStatus(StatusLine *s)
{
   if(Done())
      s->Show("");
   else
      s->Show("%s `%s' [%s]",args->getarg(0),curr,session->CurrentStatus());
}

void  mkdirJob::SayFinal()
{
   if(failed==file_count)
      return;
   char *op=args->getarg(0);
   if(file_count==1)
      // A directory has just been created
      printf(_("%s ok, `%s' created\n"),op,first);
   else if(failed)
      printf(_("%s failed for %d of %d directories\n"),op,failed,file_count);
   else
      printf(_("%s ok, %d directories created\n"),op,file_count);
}
