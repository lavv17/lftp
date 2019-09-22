/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <unistd.h>
#include "CopyJob.h"
#include "ArgV.h"
#include "plural.h"
#include "misc.h"
#include "url.h"

#define waiting_num waiting.count()

#define super Job

int CopyJob::Do()
{
   if(!c)
      return STALL;
   if(!fg_data)
      fg_data=c->GetFgData(fg);
   if(done)
      return STALL;
   if(c->Error())
   {
      const char *error=c->ErrorText();
      const char *name=GetDispName();
      if(!strstr(error,name) && op.ne(name))
	 error=xstring::cat(name,": ",error,NULL);
      if(!quiet)
	 eprintf("%s: %s\n",op.get(),error);
      done=true;
      return MOVED;
   }
   if(c->Done())
   {
      done=true;
      return MOVED;
   }
   if(!c->WriteAllowed() && c->WritePending())
   {
      if(no_status_on_write || clear_status_on_write)
	 ClearStatus();
      if(no_status_on_write)
	 NoStatus(); // disable status.

      c->AllowWrite();
      return MOVED;
   }
   return STALL;
}
int CopyJob::ExitCode()
{
   if(c->Error())
      return 1;
   return 0;
}

const char *CopyJob::SqueezeName(int w, bool base)
{
   if(base)
      return squeeze_file_name(basename_ptr(GetDispName()),w);
   return squeeze_file_name(GetDispName(),w);
}

// xgettext:c-format
static const char copy_status_format[]=N_("`%s' at %lld %s%s%s%s");
#define COPY_STATUS _(copy_status_format),name,\
      (long long)c->GetPos(),c->GetPercentDoneStr(),c->GetRateStr(),\
      c->GetETAStr(),c->GetStatus()

const char *CopyJob::Status(const StatusLine *s, bool base)
{
   if(c->Done() || c->Error())
      return "";

   const char *name=SqueezeName(s->GetWidthDelayed()-50, base);
   return xstring::format(COPY_STATUS);
}

void CopyJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(no_status)
      return;

   s->Show("%s", Status(s, false));
}
xstring& CopyJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   if(c->Done() || c->Error())
      return s;
   if(no_status)
      return s;

   s.append(prefix);
   const char *name=GetDispName();
   s.appendf(COPY_STATUS);
   s.append('\n');
   return s;
}

int CopyJob::AcceptSig(int sig)
{
   if(c==0 || GetProcGroup()==0)
   {
      if(sig==SIGINT || sig==SIGTERM)
	 return WANTDIE;
      return STALL;
   }
   c->Kill(sig);
   if(sig!=SIGCONT)
      c->Kill(SIGCONT);
   return MOVED;
}

void CopyJob::SetDispName()
{
   ParsedURL url(name,true);
   if(url.proto)
      dispname.set(url.path);
   else
      dispname.set(name);
}

CopyJob::CopyJob(FileCopy *c1,const char *name1,const char *op1)
   : c(c1), name(name1), op(op1), quiet(false)
{
   done=false;
   no_status=false;
   no_status_on_write=false;
   clear_status_on_write=false;
   SetDispName();
}


const char *CopyJob::FormatBytesTimeRate(off_t bytes,double time_spent)
{
   if(bytes<=0)
      return "";

   if(time_spent>=1)
   {
      xstring& msg=xstring::format(
	 plural("%lld $#ll#byte|bytes$ transferred in %ld $#l#second|seconds$",
		     (long long)bytes,long(time_spent+.5)),
		     (long long)bytes,long(time_spent+.5));
      double rate=bytes/time_spent;
      if(rate>=1)
	 msg.appendf(" (%s)",Speedometer::GetStrProper(rate).get());
      return msg;
   }
   return xstring::format(plural("%lld $#ll#byte|bytes$ transferred",
		     (long long)bytes),(long long)bytes);
}

void CopyJob::PrepareToDie()
{
   c=0;
   super::PrepareToDie();
}
CopyJob::~CopyJob()
{
}
#undef super

// CopyJobEnv
CopyJobEnv::CopyJobEnv(FileAccess *s,ArgV *a,bool cont1)
   : SessionJob(s), quiet(false)
{
   args=a;
   args->rewind();
   op=args?args->a0():"?";
   done=false;
   cp=0;
   errors=0;
   count=0;
   parallel=ResMgr::Query("xfer:parallel",0);
   bytes=0;
   time_spent=0;
   no_status=false;
   cont=cont1;
   ascii=false;
   xgetcwd_to(cwd);
}
CopyJobEnv::~CopyJobEnv()
{
   SetCopier(0,0);
}
int CopyJobEnv::Do()
{
   int m=STALL;
   if(done)
      return m;
   if(waiting_num<parallel)
   {
      NextFile();
      if(waiting_num==0)
      {
	 done=true;
	 m=MOVED;
      }
      else if(cp==0)
	 cp=(CopyJob*)waiting[0];
   }
   CopyJob *j=(CopyJob*)FindDoneAwaitedJob();	// we start only CopyJob's.
   if(j==0)
      return m;
   RemoveWaiting(j);
   if(j->GetLocal())
   {
      if(j->Error()) {
	 // in case of errors, move the backup to original location
	 j->GetLocal()->revert_backup();
      } else {
	 // now we can delete the old file, since there is a new one
	 j->GetLocal()->remove_backup();
      }
   }
   if(j->Error())
      errors++;
   count++;
   bytes+=j->GetBytesCount();
   if(cp==j)
      cp=0;
   Delete(j);
   if(waiting_num>0 && cp==0)
      cp=(CopyJob*)waiting[0];
   if(waiting.count()==0)
      time_spent+=now-transfer_start_ts;
   return MOVED;
}
void CopyJobEnv::AddCopier(FileCopy *c,const char *n)
{
   if(c==0)
      return;
   if(ascii)
      c->Ascii();
   cp=cj_new?cj_new->New(c,n,op):new CopyJob(c,n,op);
   cp->Quiet(quiet);
   if(waiting.count()==0)
      transfer_start_ts=now;
   AddWaiting(cp);
}
void CopyJobEnv::SetCopier(FileCopy *c,const char *n)
{
   while(waiting_num>0)
   {
      Job *j=waiting[0];
      RemoveWaiting(j);
      Delete(j);
   }
   cp=0;
   AddCopier(c,n);
}

xstring& CopyJobEnv::FormatFinalWithPrefix(xstring& s,const char *p)
{
   if(no_status || !isatty(1))
      return s;
   if(count==errors)
      return s;
   if(bytes)
      s.appendf("%s%s\n",p,CopyJob::FormatBytesTimeRate(bytes,time_spent));
   if(errors>0)
   {
      s.append(p);
      s.appendf(plural("Transfer of %d of %d $file|files$ failed\n",count),
	 errors,count);
   }
   else if(count>1)
   {
      s.append(p);
      s.appendf(plural("Total %d $file|files$ transferred\n",count),count);
   }
   return s;
}
xstring& CopyJobEnv::FormatStatus(xstring& s,int v,const char *prefix)
{
   SessionJob::FormatStatus(s,v,prefix);
   if(Done())
      FormatFinalWithPrefix(s,prefix);
   return s;
}

void CopyJobEnv::SayFinal()
{
   if(!quiet)
      printf("%s",FormatFinalWithPrefix(xstring::get_tmp(""),"").get());
}

int CopyJobEnv::AcceptSig(int sig)
{
   if(cp==0)
   {
      if(sig==SIGINT || sig==SIGTERM)
	 return WANTDIE;
      return STALL;
   }
   int total;
   if(sig==SIGINT || sig==SIGTERM)
      total=WANTDIE;
   else
      total=STALL;
   for(int i=0; i<waiting_num; i++)
   {
      Job *j=waiting[i];
      int res=j->AcceptSig(sig);
      if(res==WANTDIE)
      {
	 RemoveWaiting(j);
	 Delete(j);
	 if(cp==j)
	    cp=0;
      }
      else if(res==MOVED)
	 total=MOVED;
      else if(res==STALL)
      {
	 if(total==WANTDIE)
	    total=MOVED;
      }
   }
   if(waiting_num>0 && cp==0)
      cp=(CopyJob*)waiting[0];
   return total;
}

int CopyJobEnv::Done()
{
   return done;
}
