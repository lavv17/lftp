/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "Job.h"
#include "xalloca.h"

Job *Job::chain;

Job::Job()
{
   next=chain;
   chain=this;
   cmdline=0;
   parent=0;
   waiting=0;
   jobno=-1;
   fg=false;
   fg_data=0;
}

void  Job::AllocJobno()
{
   jobno=0;
   for(Job *scan=chain; scan; scan=scan->next)
      if(scan!=this && scan->jobno>=jobno)
	 jobno=scan->jobno+1;
}

Job::~Job()
{
   // first, reparent or kill children (hm, that's sadistic)
   {
      for(Job *scan=chain; scan; )
      {
	 if(scan->parent==this)
	 {
	    if(scan->jobno!=-1 && this->parent)
	    {
	       scan->parent=this->parent;
	       scan=scan->next;
	    }
	    else
	    {
	       delete scan;
	       scan=chain;
	    }
	 }
	 else
	    scan=scan->next;
      }
   }
   // now, delete the job from the list
   {
      for(Job **scan=&chain; *scan; scan=&(*scan)->next)
      {
	 if(*scan==this)
	 {
	    *scan=next;
	    break;
	 }
      }
   }

   if(cmdline)
      free(cmdline);
   if(fg_data)
      delete fg_data;
}

Job *Job::FindJob(int n)
{
   for(Job *scan=chain; scan; scan=scan->next)
   {
      if(scan->jobno==n)
	 return scan;
   }
   return 0;
}

class KilledJob : public Job
{
public:
   int	 Do() { return STALL; }
   int	 Done() { return 1; }
   int	 ExitCode() { return 255; }
};

void Job::Kill(int n)
{
   Job *j=FindJob(n);
   if(j)
   {
      if(j->parent && j->parent->waiting==j)
      {
	 // someone waits for termination of this job, so
	 // we have to simulate normal death...
	 Job *r=new KilledJob();
	 r->parent=j->parent;
	 r->cmdline=j->cmdline;
	 j->cmdline=0;
	 j->parent->waiting=r;
      }
      delete j;
   }
}

void Job::KillAll()
{
   for(Job **scan=&chain; *scan; )
   {
      if((*scan)->jobno>=0)
	 delete *scan;
      else
	 scan=&(*scan)->next;
   }
}

void  Job::SendSig(int n,int sig)
{
   Job *j=FindJob(n);
   if(j)
   {
      int res=j->AcceptSig(sig);
      if(res==WANTDIE)
	 Kill(n);
   }
}

int   Job::NumberOfJobs()
{
   int count=0;
   for(Job *scan=chain; scan; scan=scan->next)
      if(!scan->Done())
	 count++;
   return count;
}

static int jobno_compare(const void *a,const void *b)
{
   Job *ja=*(Job*const*)a;
   Job *jb=*(Job*const*)b;
   return ja->jobno-jb->jobno;
}

void  Job::SortJobs()
{
   int count=0;
   Job *scan;
   for(scan=chain; scan; scan=scan->next)
      count++;

   if(count==0)
      return;

   Job **arr=(Job**)alloca(count*sizeof(*arr));
   count=0;
   for(scan=chain; scan; scan=scan->next)
      arr[count++]=scan;

   qsort(arr,count,sizeof(*arr),jobno_compare);

   chain=0;
   while(count--)
   {
      arr[count]->next=chain;
      chain=arr[count];
   }
}

void Job::ListOneJobRecursively(int verbose,int indent)
{
   if(jobno>=0 || cmdline)
   {
      printf("%*s",indent,"");
      if(jobno>=0)
	 printf("[%d] ",jobno);
      printf("%s\n",cmdline?cmdline:"?");
   }
   PrintStatus(verbose);
   ListJobs(verbose,indent+1);
}

void  Job::ListJobs(int verbose,int indent)
{
   if(indent==0)
      SortJobs();

   // list the foreground job first.
   if(waiting && waiting!=this && waiting->parent==this)
      waiting->ListOneJobRecursively(verbose,indent);

   for(Job *scan=chain; scan; scan=scan->next)
   {
      if(!scan->Done() && scan->parent==this && waiting!=scan)
	 scan->ListOneJobRecursively(verbose,indent);
   }
}

void  Job::ListDoneJobs()
{
   SortJobs();

   FILE *f=stdout;
   for(Job *scan=chain; scan; scan=scan->next)
   {
      if(scan->jobno>=0 && (scan->parent==this || scan->parent==0)
         && scan->Done())
      {
	 fprintf(f,_("[%d] Done (%s)"),scan->jobno,
	    scan->cmdline?scan->cmdline:"?");
	 const char *this_url=this->GetConnectURL();
	 const char *that_url=scan->GetConnectURL();
	 if(this_url && that_url && strcmp(this_url,that_url))
	    fprintf(f," (wd: %s)",that_url);
	 fprintf(f,"\n");
	 scan->PrintStatus(0);
      }
   }
}

void  Job::BuryDoneJobs()
{
   for(Job *scan=chain; scan; )
   {
      if((scan->parent==this || scan->parent==0) && scan->jobno>=0
		  && scan->Done())
      {
	 delete scan;
      	 scan=chain;
      }
      else
	 scan=scan->next;
   }
}

void Job::fprintf(FILE *file,const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);
   vfprintf(file,fmt,v);
   va_end(v);
}

void Job::printf(const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);
   vfprintf(stdout,fmt,v);
   va_end(v);
}

void Job::eprintf(const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);
   vfprintf(stderr,fmt,v);
   va_end(v);
}


FileAccess *SessionJob::Clone()
{
   FileAccess *tmp=session;
   session=session->Clone();
   return tmp;
}

SessionJob::SessionJob(FileAccess *f)
{
   session=f;
}

SessionJob::~SessionJob()
{
   if(session)
      Reuse(session);
   session=0;
}

void SessionJob::PrintStatus(int v)
{
   if(v<2 || !session)
      return;
   const char *url=session->GetConnectURL();
   if(url)
      printf("\t%s\n",url);
}

void Job::vfprintf(FILE *file,const char *fmt,va_list v)
{
   if(file!=stdout && file!=stderr)
   {
      ::vfprintf(file,fmt,v);
      return;
   }
   if(parent)
      parent->vfprintf(file,fmt,v);
   else
      top_vfprintf(file,fmt,v);
}
void Job::top_vfprintf(FILE *file,const char *fmt,va_list v)
{
   ::vfprintf(file,fmt,v);
}

void Job::perror(const char *f)
{
   if(f)
      eprintf("%s: %s\n",f,strerror(errno));
   else
      eprintf("%s\n",strerror(errno));
}

void Job::puts(const char *s)
{
   printf("%s\n",s);
}

void Job::Bg()
{
   if(!fg)
      return;
   fg=false;
   if(waiting && waiting!=this)
      waiting->Bg();
   if(fg_data)
      fg_data->Bg();
}
void Job::Fg()
{
   if(fg)
      return;
   fg=true;
   if(fg_data)
      fg_data->Fg();
   if(waiting && waiting!=this)
      waiting->Fg();
}

int Job::AcceptSig(int s)
{
   if(waiting && waiting!=this)
   {
      if(waiting->AcceptSig(s)==WANTDIE)
      {
	 Job *new_waiting=waiting->waiting;
	 waiting->waiting=0;
	 delete waiting;
	 waiting=new_waiting;
      }
   }
   return WANTDIE;
}

void Job::ShowRunStatus(StatusLine *sl)
{
   if(waiting)
      waiting->ShowRunStatus(sl);
}
