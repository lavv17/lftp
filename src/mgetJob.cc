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
void mgetJob::PrintStatus(int v,const char *prefix)
{
   if(mkdir_job)
   {
      printf("\tCreating remote directories\n");
      return;
   }
   if(rg)
   {
      SessionJob::PrintStatus(v,prefix);
      const char *s=rg->Status();
      if(!s || !s[0])
	 return;
      printf("\t%s\n",s);
      return;
   }
   GetJob::PrintStatus(v,prefix);
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
   output_dir=0;
}

void mgetJob::make_directory(char *d)
{
   if(!make_dirs)
      return;
   char *slash=strrchr(d,'/');
   if(!slash || slash==d)
      return;

   *slash=0;

   const char *dir_name_c=output_file_name(d,0,!reverse,output_dir,make_dirs);
   char *dir_name;
   if(dir_name_c==0 || dir_name_c[0]==0)
      goto leave;
   dir_name=alloca_strdup(dir_name_c);
   if(reverse || url::is_url(dir_name))
   {
      if(mkdir_args)
      {
	 int j;
	 for(j=mkdir_base_arg; j<mkdir_args->count(); j++)
	    if(!strcmp(dir_name,mkdir_args->getarg(j)))
	       break;
	 if(j<mkdir_args->count()) // don't try to create dir twice
	    goto leave;
      }
      if(!mkdir_args)
      {
	 mkdir_args=new ArgV("mkdir");
	 mkdir_args->Append("-p");
	 mkdir_args->Append("--");
	 mkdir_base_arg=mkdir_args->count();
      }
      mkdir_args->Append(dir_name);
   }
   else // local
   {
      create_directories(dir_name);
   }
leave:
   *slash='/';
}

void mgetJob::LocalGlob(const char *p)
{
   int i;
   glob_t pglob;
   glob(p,0,0,&pglob);
   if(pglob.gl_pathc==0)
   {
      fprintf(stderr,_("%s: no files found\n"),p);
      return;
   }
   for(i=0; i<(int)pglob.gl_pathc; i++)
   {
      char *src=pglob.gl_pathv[i];

      struct stat st;
      if(stat(src,&st)!=-1 && !S_ISREG(st.st_mode))
	 continue;	// put only regular files

      args->Append(src);
      make_directory(src);
      args->Append(output_file_name(src,0,!reverse,output_dir,make_dirs));
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
	 RemoveWaiting(mkdir_job);
	 Delete(mkdir_job);
	 mkdir_job=0;
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
	    AddWaiting(mkdir_job);
	    mkdir_job->SetParentFg(this);
	    mkdir_job->cmdline=mkdir_args->Combine();
	    // don't delete mkdir_args; -- mkdirJob does it
	    mkdir_args=0;
	 }
	 return MOVED;
      }
      if(reverse && !url::is_url(p))
	 LocalGlob(expand_home_relative(p));
      else
      {
	 rg=new GlobURL(session,p);
	 rg->glob->FilesOnly();
      }
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

   FileSet *files=rg->GetResult();
   if(files->get_fnum()==0)
   {
      fprintf(stderr,_("%s: no files found\n"),rg->glob->GetPattern());
      goto next;
   }
   do {
      char *src=files->curr()->name;
      args->Append(src);
      make_directory(src);
      args->Append(output_file_name(src,0,!reverse,output_dir,make_dirs));
   } while(files->next());
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
   xfree(output_dir);
}
