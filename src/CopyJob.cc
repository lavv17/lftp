/*
 * lftp and utils
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "CopyJob.h"
#include "ArgV.h"
#include "plural.h"
#include "misc.h"

int CopyJob::Do()
{
   if(!fg_data)
      fg_data=c->GetFgData(fg);
   if(done)
      return STALL;
   if(c->Error())
   {
      eprintf("%s: %s\n",op,c->ErrorText());
      done=true;
      return MOVED;
   }
   if(c->Done())
   {
      done=true;
      return MOVED;
   }
   return STALL;
}
int CopyJob::Done()
{
   return done;
}
int CopyJob::ExitCode()
{
   if(c->Error())
      return 1;
   return 0;
}

const char *CopyJob::SqueezeName(int w)
{
   return squeeze_file_name(name,w);
}

#define COPY_STATUS _("`%s' at %lu %s%s%s%s"),name,\
      c->GetPos(),c->GetPercentDoneStr(),c->GetRateStr(),\
      c->GetETAStr(),c->GetStatus()

void CopyJob::ShowRunStatus(StatusLine *s)
{
   if(no_status)
      return;
   if(c->Done() || c->Error())
      return;

   const char *name=SqueezeName(s->GetWidthDelayed()-50);
   s->Show(COPY_STATUS);
}
void CopyJob::PrintStatus(int v)
{
   if(c->Done() || c->Error())
      return;

   printf("\t");
   const char *name=GetName();
   printf(COPY_STATUS);
   printf("\n");
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

CopyJob::CopyJob(FileCopy *c1,const char *name1,const char *op1)
{
   c=c1;
   name=xstrdup(name1);
   op=xstrdup(op1);
   done=false;
   no_status=false;
}
CopyJob::~CopyJob()
{
   Delete(c);
   xfree(name);
   xfree(op);
}

CopyJob *CopyJob::NewEcho(const char *str,int len,FDStream *o,const char *op)
{
   if(o==0)
      o=new FDStream(1,"<stdout>");
   CopyJob *j=new CopyJob(FileCopy::New(
	 new FileCopyPeerString(str,len),
	 new FileCopyPeerFDStream(o,FileCopyPeer::PUT),
	 false
      ),o->name,op);
   if(o->usesfd(1))
      j->NoStatus();
   return j;
}

// CopyJobEnv
CopyJobEnv::CopyJobEnv(FileAccess *s,ArgV *a,bool cont1)
   : SessionJob(s)
{
   args=a;
   args->rewind();
   op=args?args->a0():"?";
   done=false;
   cp=0;
   errors=0;
   count=0;
   bytes=0;
   time_spent=0;
   no_status=false;
   cont=cont1;
   ascii=false;
   cwd=xgetcwd();
}
CopyJobEnv::~CopyJobEnv()
{
   SetCopier(0,0);
   if(args)
      delete args;
   xfree(cwd);
}
int CopyJobEnv::Do()
{
   if(done)
      return STALL;
   if(cp==0)
      goto next;
   if(cp->Error())
   {
      errors++;
      eprintf("%s: %s\n",op,cp->ErrorText());
   }
   if(cp->Error() || cp->Done())
   {
      count++;
      bytes+=cp->GetBytesCount();
      time_spent+=cp->GetTimeSpent();
      time_spent+=cp->GetTimeSpentMilli()/1000.0;
   next:
      NextFile();
      if(cp==0)
	 done=true;
      return MOVED;
   }
   return STALL;
}
void CopyJobEnv::SetCopier(FileCopy *c,const char *n)
{
   waiting=0;
   Delete(cp);
   cp=0;
   if(c==0)
      return;
   if(ascii)
      c->Ascii();
   cp=new CopyJob(c,n,op);
   cp->SetParentFg(this);
   waiting=cp;
}

void CopyJobEnv::SayFinalWithPrefix(const char *p)
{
   if(no_status)
      return;
   if(count==errors)
      return;
   if(bytes)
   {
      printf("%s",p);
      if(time_spent>=1)
      {
	 printf(plural("%ld $#l#byte|bytes$ transferred"
			" in %ld $#l#second|seconds$",
			bytes,long(time_spent+.5)),
			bytes,long(time_spent+.5));
	 double rate=bytes/time_spent;
	 if(rate>=1)
	    printf(" (%s)\n",Speedometer::GetStr(rate));
	 else
	    printf("\n");
      }
      else
      {
	 printf(plural("%ld $#l#byte|bytes$ transferred\n",
			bytes),bytes);
      }
   }
   if(errors>0)
   {
      printf("%s",p);
      printf(plural("Transfer of %d of %d $file|files$ failed\n",count),
	 errors,count);
   }
   else if(count>1)
   {
      printf("%s",p);
      printf(plural("Total %d $file|files$ transferred\n",count),count);
   }
}
void CopyJobEnv::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(Done())
      SayFinalWithPrefix("\t");
}

int CopyJobEnv::AcceptSig(int sig)
{
   if(cp==0)
   {
      if(sig==SIGINT || sig==SIGTERM)
	 return WANTDIE;
      return STALL;
   }
   return cp->AcceptSig(sig);
}

int CopyJobEnv::Done()
{
   return done;
}
