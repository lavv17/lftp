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
#include <fcntl.h>
#include "mrmJob.h"

void mrmJob::ShowRunStatus(StatusLine *s)
{
   if(rg)
   {
      s->Show(_("Getting file list [%s]"),session->CurrentStatus());
      return;
   }
   rmJob::ShowRunStatus(s);
}
void mrmJob::PrintStatus(int v)
{
   if(rg)
   {
      SessionJob::PrintStatus(v);
      putchar('\t');
      printf(_("Getting file list [%s]"),session->CurrentStatus());
      putchar('\n');
      return;
   }
   rmJob::PrintStatus(v);
}

mrmJob::mrmJob(FileAccess *session,ArgV *args) : rmJob(session,new ArgV(args->a0()))
{
   rg=0;

   this->args=args;

   args->rewind();
   int opt;
   while((opt=args->getopt(""))!=EOF)
   {
      switch(opt)
      {
      case('?'):
      print_usage:
	 printf(_("Usage: %s <pattern> ...\n"),args->getarg(0));
	 return;
      }
   }
   args->back();
   char *p=args->getnext();
   if(!p)
      goto print_usage;

   rg=session->MakeGlob(p);
   while(rg->Do()==MOVED);
}

int mrmJob::Do()
{
   char **files;
   char **i;

   int m=STALL;

   if(!rg)
      return rmJob::Do();

   if(!rg->Done())
      return m;

   m=MOVED;

   if(rg->Error())
   {
      fprintf(stderr,"rglob: %s\n",rg->ErrorText());
      goto next;
   }

   files=rg->GetResult();
   if(!files)
   {
      fprintf(stderr,_("%s: no files found\n"),rg->GetPattern());
      goto next;
   }
   for(i=files; *i; i++)
      AddFile(*i);

next:
   delete rg;
   rg=0;

   char *p=args->getnext();
   if(!p)
   {
      delete args;
      args=0;
      return MOVED;
   }

   rg=session->MakeGlob(p);
   while(rg->Do()==MOVED);

   return m;
}

mrmJob::~mrmJob()
{
   if(rg)
      delete rg;
}
