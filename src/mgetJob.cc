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

/* $Id$ */

#include <config.h>
#include <fcntl.h>
#include "mgetJob.h"
#include "misc.h"

void mgetJob::ShowRunStatus(StatusLine *s)
{
   if(rg)
   {
      s->Show(rg->Status());
      return;
   }
   GetJob::ShowRunStatus(s);
}
void mgetJob::PrintStatus(int v)
{
   if(rg)
   {
      SessionJob::PrintStatus(v);
      printf("\t%s\n",rg->Status());
      return;
   }
   GetJob::PrintStatus(v);
}

mgetJob::mgetJob(FileAccess *session,ArgV *args) : GetJob(session,new ArgV(args->a0()))
{
   rg=0;
   make_dirs=false;

   this->args=args;

   args->rewind();
   int opt;
   while((opt=args->getopt("+cde"))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('d'):
	 make_dirs=true;
	 break;
      case('e'):
	 delete_files=true;
	 break;
      case('?'):
      print_usage:
	 printf(_("Usage: %s [-c] [-d] [-e] pattern ...\n"),args->getarg(0));
	 return;
      }
   }
   args->back();
   char *p=args->getnext();
   if(!p)
      goto print_usage;

   rg=session->MakeGlob(p);
   if(!rg)
      rg=new NoGlob(p);
   while(rg->Do()==MOVED);
}

int mgetJob::Do()
{
   char **files;
   char **i;

   int m=STALL;

   if(!rg)
      return GetJob::Do();

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
   {
      char *nl=strrchr(*i,'/');
      char *local_name;
      if(!nl)
	 local_name=*i;
      else
      {
	 if(make_dirs)
	 {
	    *nl=0;
	    create_directories(*i);
	    *nl='/';
	    local_name=*i;
	 }
	 else
	    local_name=nl+1;
      }
      AddFile(*i,local_name);
   }

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
   if(!rg)
      rg=new NoGlob(p);
   while(rg->Do()==MOVED);

   return MOVED;
}

mgetJob::~mgetJob()
{
   if(rg)
      delete rg;
}
