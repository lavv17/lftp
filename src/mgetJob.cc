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
#include <sys/types.h>
#include <sys/stat.h>
#include <glob.h>
#include "mgetJob.h"
#include "misc.h"
#include "ArgV.h"
#include "url.h"
#include "mkdirJob.h"

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
   if(mkdir_job)
   {
      printf("\tCreating remote directories\n");
      return;
   }
   if(rg)
   {
      SessionJob::PrintStatus(v);
      printf("\t%s\n",rg->Status());
      return;
   }
   GetJob::PrintStatus(v);
}

mgetJob::mgetJob(FileAccess *session,ArgV *a,bool c,bool md)
   : GetJob(session,new ArgV(a->a0()),c)
{
   rg=0;
   make_dirs=md;
   m_args=a;
   m_args->rewind();
   mkdir_job=0;
   mkdir_args=0;
   mkdir_base_arg=0;
}

void mgetJob::LocalGlob(const char *p)
{
   int i;
   glob_t pglob;
   glob(p,0,0,&pglob);
   if(pglob.gl_pathc==0)
   {
      fprintf(stderr,_("%s: no such files\n"),p);
      return;
   }
   for(i=0; i<(int)pglob.gl_pathc; i++)
   {
      char *local_name=pglob.gl_pathv[i];

      struct stat st;
      if(stat(local_name,&st)!=-1 && !S_ISREG(st.st_mode))
	 return;	// put only regular files

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
      args->Append(local_name);
      args->Append(remote_name);
   }
   globfree(&pglob);
}

int mgetJob::Do()
{
   int m=STALL;

   if(mkdir_job)
   {
      if(mkdir_job->Done())
      {
	 delete mkdir_job;
	 mkdir_job=0;
	 waiting=0;
      }
      else
	 return STALL;
   }

   if(!m_args)
      return GetJob::Do();

   if(!rg)
   {
   next:
      if(rg) { delete rg; rg=0; }
      char *p=m_args->getnext();
      if(!p)
      {
	 delete m_args;
	 m_args=0;
	 if(mkdir_args)
	 {
	    mkdir_job=new mkdirJob(Clone(),mkdir_args);
	    mkdir_job->BeQuiet();
	    waiting=mkdir_job;
	    waiting->parent=this;
	    waiting->cmdline=mkdir_args->Combine();
	    // don't delete mkdir_args; -- mkdirJob does it
	    mkdir_args=0;
	 }
	 return MOVED;
      }
      if(reverse && !url::is_url(p))
	 LocalGlob(p);
      else
	 rg=new GlobURL(session,p);
      m=MOVED;
   }

   if(!rg)
      return m;

   if(rg->Error())
   {
      fprintf(stderr,"rglob: %s\n",rg->ErrorText());
      goto next;
   }

   if(!rg->Done())
      return m;

   m=MOVED;

   char **files=rg->GetResult();
   if(!files)
   {
      fprintf(stderr,_("%s: no files found\n"),rg->glob->GetPattern());
      goto next;
   }
   while(*files)
   {
      char *nl=strrchr(*files,'/');
      char *local_name;
      if(!nl)
	 local_name=*files;
      else
      {
	 if(make_dirs)
	 {
	    *nl=0;
	    create_directories(*files);
	    *nl='/';
	    local_name=*files;
	 }
	 else
	    local_name=nl+1;
      }
      args->Append(*files);
      args->Append(local_name);
      files++;
   }
   goto next;
}

mgetJob::~mgetJob()
{
   if(rg)
      delete rg;
   if(m_args)
      delete m_args;
   if(mkdir_args)
      delete mkdir_args;
}
