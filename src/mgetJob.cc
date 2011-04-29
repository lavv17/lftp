/*
 * lftp and utils
 *
 * Copyright (c) 1996-2010 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
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
#include "mgetJob.h"
#include "misc.h"
#include "ArgV.h"
#include "url.h"
#include "mkdirJob.h"

CDECL_BEGIN
#include <glob.h>
CDECL_END

void mgetJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(rg)
   {
      s->Show(rg->Status());
      return;
   }
   GetJob::ShowRunStatus(s);
}
xstring& mgetJob::FormatStatus(xstring& buf,int v,const char *prefix)
{
   if(mkdir_job)
      return buf.append("\tCreating remote directories\n");
   if(rg)
   {
      SessionJob::FormatStatus(buf,v,prefix);
      const char *s=rg->Status();
      if(!s || !s[0])
	 return buf;
      return buf.appendf("\t%s\n",s);
   }
   return GetJob::FormatStatus(buf,v,prefix);
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

void mgetJob::make_directory(const char *d_c)
{
   if(!make_dirs)
      return;
   char *d=alloca_strdup(d_c);
   char *slash=strrchr(d,'/');
   if(!slash || slash==d)
      return;

   *slash=0;

   const char *dir_name_c=output_file_name(d,0,!reverse,output_dir,make_dirs);
   char *dir_name;
   if(dir_name_c==0 || dir_name_c[0]==0)
      return;
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
	    return;
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
}

void mgetJob::LocalGlob(const char *p)
{
   int i;
   glob_t pglob;
   glob(p,0,0,&pglob);
   if(pglob.gl_pathc==0)
   {
      fprintf(stderr,_("%s: %s: no files found\n"),op,p);
      count++;
      errors++;
      goto leave;
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
leave:
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
      if(rg)
	 rg=0;
      const char *p=m_args->getnext();
      if(!p)
      {
	 m_args=0;
	 if(mkdir_args)
	 {
	    xstring_ca cmdline(mkdir_args->Combine());
	    mkdir_job=new mkdirJob(Clone(),mkdir_args.borrow());
	    mkdir_job->cmdline.set_allocated(cmdline.borrow());
	    mkdir_job->BeQuiet();
	    AddWaiting(mkdir_job);
	 }
	 return MOVED;
      }
      if(reverse && !url::is_url(p))
	 LocalGlob(expand_home_relative(p));
      else
	 rg=new GlobURL(session,p,GlobURL::FILES_ONLY);
      m=MOVED;
   }

   if(!rg)
      return m;

   if(rg->Error())
   {
      fprintf(stderr,"%s: %s: %s\n",op,rg->GetPattern(),rg->ErrorText());
      count++;
      errors++;
      goto next;
   }

   if(!rg->Done())
      return m;

   m=MOVED;

   FileSet *files=rg->GetResult();
   if(files->get_fnum()==0)
   {
      fprintf(stderr,_("%s: %s: no files found\n"),op,rg->GetPattern());
      count++;
      errors++;
      goto next;
   }
   do {
      const char *src=files->curr()->name;
      args->Append(src);
      make_directory(src);
      args->Append(output_file_name(src,0,!reverse,output_dir,make_dirs));
   } while(files->next());
   goto next;
}

mgetJob::~mgetJob()
{
}
