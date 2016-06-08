/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include "Job.h"
#include "misc.h"

xlist_head<Job> Job::all_jobs;
#define waiting_num waiting.count()

Job::Job()
   : all_jobs_node(this), children_jobs_node(this)
{
   all_jobs.add(all_jobs_node);
   parent=0;
   jobno=-1;
   fg=false;
}

void Job::SetParent(Job *j)
{
   if(children_jobs_node.listed())
      children_jobs_node.remove();
   parent=j;
   if(j)
      j->children_jobs.add(children_jobs_node);
}

void  Job::AllocJobno()
{
   jobno=0;
   xlist_for_each(Job,all_jobs,node,scan)
      if(scan!=this && scan->jobno>=jobno)
	 jobno=scan->jobno+1;
}

void Job::PrepareToDie()
{
   // reparent or kill children (hm, that's sadistic)
   xlist_for_each_safe(Job,children_jobs,child_node,child,next) {
      child_node->remove();
      if(child->jobno!=-1 && this->parent) {
	 child->parent=this->parent;
	 this->parent->children_jobs.add(child_node);
      } else {
	 child->parent=0;
	 child->DeleteLater();
      }
   }
   // if parent waits for the job, make it stop
   if(parent)
      parent->RemoveWaiting(this);
   fg_data=0;
   waiting.unset();
   if(children_jobs_node.listed())
      children_jobs_node.remove();
   all_jobs_node.remove();
}

Job::~Job()
{
   assert(!all_jobs_node.listed());
   assert(!children_jobs_node.listed());
}

Job *Job::FindJob(int n)
{
   xlist_for_each(Job,all_jobs,node,scan)
   {
      if(scan->jobno==n)
	 return scan;
   }
   return 0;
}

Job *Job::FindWhoWaitsFor(Job *j)
{
   xlist_for_each(Job,all_jobs,node,scan)
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
   xlist_for_each(Job,children_jobs,node,scan)
      AddWaiting(scan);
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
   if(j->AcceptSig(SIGTERM)!=WANTDIE)
      return;
   if(j->parent && j->parent->WaitsFor(j))
   {
      // someone waits for termination of this job, so
      // we have to simulate normal death...
      Job *r=new KilledJob();
      r->parent=j->parent;
      j->parent->children_jobs.add(r->children_jobs_node);
      j->children_jobs_node.remove();
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
   // Job::Kill may remove more than a single job from all_jobs list,
   // collect list of jobs beforehand.
   xarray<Job*> to_kill;
   xlist_for_each(Job,all_jobs,node,scan)
      if(scan->jobno>=0)
	 to_kill.append(scan);
   for(int i=0; i<to_kill.count(); i++)
      Kill(to_kill[i]);
   CollectGarbage();
}
void Job::Cleanup()
{
   xarray<Job*> to_kill;
   xlist_for_each(Job,all_jobs,node,scan)
      to_kill.append(scan);
   for(int i=0; i<to_kill.count(); i++)
      Kill(to_kill[i]);
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

int Job::NumberOfJobs()
{
   int count=0;
   xlist_for_each(Job,all_jobs,node,scan)
      if(!scan->Done())
	 count++;
   return count;
}
int Job::NumberOfChildrenJobs()
{
   int count=0;
   xlist_for_each(Job,children_jobs,node,scan)
      if(!scan->Done())
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

   xlist_for_each_safe(Job,all_jobs,node,scan,next) {
      arr.append(scan);
      node->remove();
   }
   arr.qsort(jobno_compare);

   int count=arr.count();
   while(count--)
      all_jobs.add(arr[count]->all_jobs_node);

   xlist_for_each(Job,all_jobs,node,scan_waiting)
      scan_waiting->waiting.qsort(jobno_compare);
}

static xstring print_buf("");
void Job::PrintJobTitle(int indent,const char *suffix)
{
   print_buf.truncate();
   printf("%s",FormatJobTitle(print_buf,indent,suffix).get());
}
void Job::ListOneJob(int verbose,int indent,const char *suffix)
{
   print_buf.truncate();
   printf("%s",FormatOneJob(print_buf,verbose,indent,suffix).get());
}
void Job::ListOneJobRecursively(int verbose,int indent)
{
   print_buf.truncate();
   printf("%s",FormatOneJobRecursively(print_buf,verbose,indent).get());
}
void Job::PrintStatus(int v,const char *prefix)
{
   print_buf.truncate();
   printf("%s",FormatStatus(print_buf,v,prefix).get());
}

void  Job::ListDoneJobs()
{
   SortJobs();

   FILE *f=stdout;
   xlist_for_each(Job,all_jobs,node,scan)
   {
      if(scan->jobno>=0 && (scan->parent==this || scan->parent==0)
         && scan->Done())
      {
	 fprintf(f,_("[%d] Done (%s)"),scan->jobno,scan->GetCmdLine().get());
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

xstring& Job::FormatJobTitle(xstring& s,int indent,const char *suffix)
{
   if(jobno<0 && !cmdline)
      return s;
   s.append_padding(indent,' ');
   if(jobno>=0)
      s.appendf("[%d] ",jobno);
   s.append(GetCmdLine());
   if(suffix) {
      s.append(' ');
      s.append(suffix);
   }
   if(waiting.count()>0) {
      size_t len=s.length();
      FormatShortStatus(s.append(" -- "));
      if(s.length()<=len+4)
	 s.truncate(len);
   }
   s.append('\n');
   return s;
}

xstring& Job::FormatOneJob(xstring& s,int verbose,int indent,const char *suffix)
{
   FormatJobTitle(s,indent,suffix);
   FormatStatus(s,verbose);
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]->jobno<0 && waiting[i]!=this && !waiting[i]->cmdline)
	 waiting[i]->FormatOneJob(s,verbose,indent+1,"");
   }
   return s;
}

xstring& Job::FormatOneJobRecursively(xstring& s,int verbose,int indent)
{
   FormatJobTitle(s,indent,"");
   FormatStatus(s,verbose);
   FormatJobs(s,verbose,indent+1);
   return s;
}

xstring& Job::FormatJobs(xstring& s,int verbose,int indent)
{
   if(indent==0)
      SortJobs();

   // list the foreground job first.
   for(int i=0; i<waiting_num; i++)
   {
      if(waiting[i]!=this && waiting[i]->parent==this)
	 waiting[i]->FormatOneJobRecursively(s,verbose,indent);
   }

   xlist_for_each(Job,children_jobs,node,scan)
      if(!scan->Done() && !this->WaitsFor(scan))
	 scan->FormatOneJobRecursively(s,verbose,indent);

   return s;
}

void  Job::BuryDoneJobs()
{
   xlist_for_each_safe(Job,all_jobs,node,scan,next)
   {
      if((scan->parent==this || scan->parent==0) && scan->jobno>=0
		  && scan->Done())
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


xstring& SessionJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   if(v<2 || !session)
      return s;
   const char *url=session->GetConnectURL();
   if(url && *url) {
      s.append(prefix);
      s.append(url);
      s.append('\n');
   }
   const char *last_dc=session->GetLastDisconnectCause();
   if(last_dc && !session->IsConnected()) {
      s.append(prefix);
      s.appendf("Last disconnect cause: %s\n",last_dc);
   }
   return s;
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
   xlist_for_each(Job,children_jobs,node,scan)
      if(scan->jobno>=0)
	 return scan;
   return 0;
}

void Job::lftpMovesToBackground_ToAll()
{
   xlist_for_each(Job,all_jobs,node,scan)
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
   IncRefCount();  // keep me in memory
   for(;;)
   {
      SMTask::Schedule();
      if(Deleted() || Done())
	 break;
      SMTask::Block();
   }
   DecRefCount();
}

off_t Job::GetBytesCount()
{
   off_t sum=0;
   for(int i=0; i<waiting.count(); i++)
      sum+=waiting[i]->GetBytesCount();
   return sum;
}
double Job::GetTimeSpent()
{
   return 0;
}
double Job::GetTransferRate()
{
   double sum=0;
   for(int i=0; i<waiting.count(); i++)
      sum+=waiting[i]->GetTransferRate();
   return sum;
}

xstring& Job::FormatShortStatus(xstring& s)
{
   double rate=GetTransferRate();
   if(rate>=1)
      s.append(Speedometer::GetStrProper(rate));
   return s;
}
