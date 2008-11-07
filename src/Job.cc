/*
 * lftp and utils
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include "Job.h"
#include "misc.h"

Job *Job::chain;
#define waiting_num waiting.count()

Job::Job()
{
   next=chain;
   chain=this;
   parent=0;
   jobno=-1;
   fg=false;
}

void  Job::AllocJobno()
{
   jobno=0;
   ListScan(Job,chain,next)
      if(scan!=this && scan->jobno>=jobno)
	 jobno=scan->jobno+1;
}

void Job::PrepareToDie()
{
   // reparent or kill children (hm, that's sadistic)
   ListScan(Job,chain,next)
   {
      if(scan->parent==this)
      {
	 if(scan->jobno!=-1 && this->parent)
	    scan->parent=this->parent;
	 else
	 {
	    scan->parent=0;
	    scan->DeleteLater();
	 }
      }
   }
   // if parent waits for the job, make it stop
   if(parent)
      parent->RemoveWaiting(this);
   fg_data=0;
   waiting.unset();
   ListDel(Job,chain,this,next);
}

Job::~Job()
{
}

Job *Job::FindJob(int n)
{
   ListScan(Job,chain,next)
   {
      if(scan->jobno==n)
	 return scan;
   }
   return 0;
}

Job *Job::FindWhoWaitsFor(Job *j)
{
   ListScan(Job,chain,next)
   {
      if(scan->WaitsFor(j))
	 return scan;
   }
   return 0;
}

bool Job::WaitsFor(Job *j)
{
   return waiting.search(j)>=0;
}

Job *Job::FindDoneAwaitedJob()
{
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]->Done())
	 return waiting[i];
   }
   return 0;
}

void Job::WaitForAllChildren()
{
   ListScan(Job,chain,next)
   {
      if(scan->parent==this)
	 AddWaiting(scan);
   }
}
void Job::AllWaitingFg()
{
   for(int i=0; i<waiting_num; i++)
      waiting[i]->Fg();
}

void Job::ReplaceWaiting(Job *from,Job *to)
{
   int i=waiting.search(from);
   if(i>=0)
      waiting[i]=to;
}

void Job::AddWaiting(Job *j)
{
   if(j==0 || this->WaitsFor(j))
      return;
   assert(FindWhoWaitsFor(j)==0);
   j->SetParentFg(this);
   waiting.append(j);
}
void Job::RemoveWaiting(const Job *j)
{
   int i=waiting.search(const_cast<Job*>(j));
   if(i>=0)
      waiting.remove(i);
}

class KilledJob : public Job
{
public:
   int	 Do() { return STALL; }
   int	 Done() { return 1; }
   int	 ExitCode() { return 255; }
};

void Job::Kill(Job *j)
{
   if(j->parent && j->parent->WaitsFor(j))
   {
      // someone waits for termination of this job, so
      // we have to simulate normal death...
      Job *r=new KilledJob();
      r->parent=j->parent;
      r->cmdline.set(j->cmdline);
      r->waiting.move_here(j->waiting);
      j->parent->ReplaceWaiting(j,r);
   }
   assert(FindWhoWaitsFor(j)==0);
   j->DeleteLater();
}

void Job::Kill(int n)
{
   Job *j=FindJob(n);
   if(j)
      Kill(j);
}

void Job::KillAll()
{
   ListScan(Job,chain,next)
      if(scan->jobno>=0)
	 Job::Kill(scan);
   CollectGarbage();
}
void Job::Cleanup()
{
   ListScan(Job,chain,next)
      Job::Kill(scan);
   CollectGarbage();
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
   ListScan(Job,chain,next)
      if(!scan->running && !scan->Done())
	 count++;
   return count;
}

static int jobno_compare(Job *const*a,Job *const*b)
{
   return (*a)->jobno-(*b)->jobno;
}

void  Job::SortJobs()
{
   xarray<Job*> arr;

   ListScan(Job,chain,next)
      arr.append(scan);
   arr.qsort(jobno_compare);

   chain=0;
   int count=arr.count();
   while(count--)
      ListAdd(Job,chain,arr[count],next);

   ListScan(Job,chain,next)
      scan->waiting.qsort(jobno_compare);
}

void Job::PrintJobTitle(int indent,const char *suffix)
{
   if(jobno<0 && !cmdline)
      return;
   printf("%*s",indent,"");
   if(jobno>=0)
      printf("[%d] ",jobno);
   printf("%s",cmdline?cmdline.get():"?");
   if(suffix)
      printf(" %s",suffix);
   printf("\n");
}

void Job::ListOneJob(int verbose,int indent,const char *suffix)
{
   PrintJobTitle(indent,suffix);
   PrintStatus(verbose);
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]->jobno<0 && waiting[i]!=this && !waiting[i]->cmdline)
	 waiting[i]->ListOneJob(verbose,indent+1);
   }
}

void Job::ListOneJobRecursively(int verbose,int indent)
{
   PrintJobTitle(indent);
   PrintStatus(verbose);
   ListJobs(verbose,indent+1);
}

void  Job::ListJobs(int verbose,int indent)
{
   if(indent==0)
      SortJobs();

   // list the foreground job first.
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]!=this && waiting[i]->parent==this)
	 waiting[i]->ListOneJobRecursively(verbose,indent);
   }

   ListScan(Job,chain,next)
      if(scan->parent==this && !scan->Done() && !this->WaitsFor(scan))
	 scan->ListOneJobRecursively(verbose,indent);
}

void  Job::ListDoneJobs()
{
   SortJobs();

   FILE *f=stdout;
   ListScan(Job,chain,next)
   {
      if(scan->jobno>=0 && (scan->parent==this || scan->parent==0)
         && !scan->deleting && scan->Done())
      {
	 fprintf(f,_("[%d] Done (%s)"),scan->jobno,
	    scan->cmdline?scan->cmdline.get():"?");
	 const char *this_url=this->GetConnectURL();
	 this_url=alloca_strdup(this_url); // save it from overwriting.
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
   ListScan(Job,chain,next)
   {
      if((scan->parent==this || scan->parent==0) && scan->jobno>=0
		  && !scan->deleting && scan->Done())
	 scan->DeleteLater();
   }
   CollectGarbage();
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


void SessionJob::PrintStatus(int v,const char *prefix)
{
   if(v<2 || !session)
      return;
   const char *url=session->GetConnectURL();
   if(url && *url)
      printf("%s%s\n",prefix,url);
}

void SessionJob::Fg()
{
   if(session)
      session->SetPriority(1);
   Job::Fg();
}
void SessionJob::Bg()
{
   Job::Bg();
   if(session)
      session->SetPriority(0);
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
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]!=this)
	 waiting[i]->Bg();
   }
   if(fg_data)
      fg_data->Bg();
}
void Job::Fg()
{
   Resume();
   if(fg)
      return;
   fg=true;
   if(fg_data)
      fg_data->Fg();
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]!=this)
	 waiting[i]->Fg();
   }
}

int Job::AcceptSig(int s)
{
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]==this)
	 continue;
      if(waiting[i]->AcceptSig(s)==WANTDIE)
      {
	 while(waiting[i]->waiting_num>0)
	 {
	    Job *new_waiting=waiting[i]->waiting[0];
	    waiting[i]->RemoveWaiting(new_waiting);
	    AddWaiting(new_waiting);
	 }
	 Job *j=waiting[i];
	 RemoveWaiting(j);
	 Delete(j);
	 i--;
      }
   }
   return WANTDIE;
}

void Job::ShowRunStatus(const SMTaskRef<StatusLine>& sl)
{
   if(waiting_num==0)
      return;
   Job *j=waiting[0];
   if(waiting_num>1)
   {
      j=waiting[(now/3)%waiting_num];
      current->TimeoutS(3);
   }
   if(j!=this)
      j->ShowRunStatus(sl);
}

Job *Job::FindAnyChild()
{
   ListScan(Job,chain,next)
      if(scan->parent==this && scan->jobno>=0)
      	 return scan;
   return 0;
}

void Job::lftpMovesToBackground_ToAll()
{
   ListScan(Job,chain,next)
      scan->lftpMovesToBackground();
}

bool Job::CheckForWaitLoop(Job *parent)
{
   if(parent==this)
      return true;
   for(int i=0; i<waiting_num; i++)
      if(waiting[i]->CheckForWaitLoop(parent))
	 return true;
   return false;
}

void Job::WaitDone()
{
   ref_count++;	  // keep me in memory
   for(;;)
   {
      SMTask::Schedule();
      if(deleting || Done())
	 break;
      SMTask::Block();
   }
   ref_count--;
}
