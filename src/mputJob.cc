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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <glob.h>
#include "mputJob.h"
#include "mkdirJob.h"

mputJob::mputJob(FileAccess *session,ArgV *args) : PutJob(session,new ArgV(args->a0()))
{
   make_dirs=false;
   args->rewind();
   op=args->a0();
   int opt;
   while((opt=args->getopt("+cd"))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('d'):
	 make_dirs=true;
	 break;
      case('?'):
      print_usage:
	 printf("Usage: %s [-c] [-d] pattern ...\n",args->getarg(0));
	 return;
      }
   }
   args->back();
   char *p=args->getnext();
   if(!p)
      goto print_usage;

   ArgV *mkdir_args=0;
   glob_t pglob;
   int i;

   int mkdir_base_arg=1;
   for(;;)
   {
      glob(p,0,0,&pglob);
      if(pglob.gl_pathc==0)
	 fprintf(stderr,"%s: no such files\n",p);
      for(i=0; i<(int)pglob.gl_pathc; i++)
      {
	 char *local_name=pglob.gl_pathv[i];

	 struct stat st;
	 if(stat(local_name,&st)!=-1 && !S_ISREG(st.st_mode))
	    continue;	// put only regular files

	 char *slash=strrchr(local_name,'/');
	 char *remote_name;
	 if(!slash)
	    remote_name=local_name;
	 else
	 {
	    if(make_dirs)
	    {
	       *slash=0;
	       if(!mkdir_args)
	       {
		  mkdir_args=new ArgV("mkdir");
		  mkdir_args->Append("-p");
		  mkdir_args->Append("--");
		  mkdir_base_arg=mkdir_args->count();
	       }
	       int j;
	       for(j=mkdir_base_arg; j<mkdir_args->count(); j++)
		  if(!strcmp(local_name,mkdir_args->getarg(j)))
		     break;
	       if(j==mkdir_args->count()) // don't try to create dir twice
		  mkdir_args->Append(local_name);
	       *slash='/';
	       remote_name=local_name;
	    }
	    else
	       remote_name=slash+1;
	 }
	 AddFile(local_name,remote_name);
      }
      globfree(&pglob);
      p=args->getnext();
      if(!p)
	 break;
   }
   if(mkdir_args)
   {
      mkdirJob *m=new mkdirJob(Clone(),mkdir_args);
      m->BeQuiet();
      waiting=m;
      waiting->parent=this;
      waiting->cmdline=mkdir_args->Combine();
      // don't delete mkdir_args; -- mkdirJob does it
   }
}

int mputJob::Do()
{
   if(waiting)
   {
      if(waiting->Done())
      {
	 delete waiting;
	 waiting=0;
      }
      else
	 return STALL;
   }
   return PutJob::Do();
}

void  mputJob::PrintStatus(int v)
{
   if(waiting)
   {
      printf("\tCreating remote directories\n");
      waiting->PrintStatus(v);
      return;
   }
   PutJob::PrintStatus(v);
}

void  mputJob::ShowRunStatus(StatusLine *s)
{
   if(waiting)
      waiting->ShowRunStatus(s);
   else
      PutJob::ShowRunStatus(s);
}
