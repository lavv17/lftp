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
#include "ArgV.h"
#include "url.h"

void mgetJob::ShowRunStatus(StatusLine *s)
{
   if(rg)
   {
      s->Show(rg->glob->Status());
      return;
   }
   GetJob::ShowRunStatus(s);
}
void mgetJob::PrintStatus(int v)
{
   if(rg)
   {
      SessionJob::PrintStatus(v);
      printf("\t%s\n",rg->glob->Status());
      return;
   }
   GetJob::PrintStatus(v);
}

mgetJob::mgetJob(FileAccess *session,ArgV *a) : GetJob(session,new ArgV(a->a0()))
{
   rg=0;
   make_dirs=false;
   m_args=a;
   m_args->rewind();
}

int mgetJob::Do()
{
   int m=STALL;

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
	 return MOVED;
      }
      rg=new GlobURL(session,p);
      m=MOVED;
   }

   if(rg->glob->Error())
   {
      fprintf(stderr,"rglob: %s\n",rg->glob->ErrorText());
      goto next;
   }

   if(!rg->glob->Done())
      return m;

   m=MOVED;

   char **files=rg->glob->GetResult();
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
}
