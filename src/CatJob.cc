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
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include "xmalloc.h"

#include "CatJob.h"
#include "ArgV.h"
#include "Filter.h"

int   CatJob::Done()
{
   return curr==0 && filter_wait==0 && global==0;
}

void  CatJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;

   if(curr)
   {
      XferJob::ShowRunStatus(s);
   }
   else
   {
      s->Show(_("Waiting for filter to terminate"));
   }
}

void  CatJob::PrintStatus(int verbose)
{
   if(!session)
      return;
   if(!Done() && curr==0)
   {
      putchar('\t');
      puts(_("Waiting for filter to terminate"));
      return;
   }
   XferJob::PrintStatus(verbose);
}

int   CatJob::Do()
{
   RateDrain();

   int m=STALL;
   int res;

   if(filter_wait)
   {
      if(!filter_wait->Done())
	 return m;
      delete filter_wait;
      filter_wait=0;
      m=MOVED;
   }

   if(curr==0)
   {
      NextFile();
      if(curr)
	 m=MOVED;
   }

   if(curr==0)
   {
      if(global)
      {
	 filter_wait=global;
	 global=0;
	 m=MOVED;
      }
      return m;
   }

   // now we can get to data...
   if(in_buffer==0 && got_eof)
   {
      NextFile();
      m=MOVED;
      return m;
   }
   if(!got_eof)
   {
      if(session->IsClosed())
      {
	 offset=0;
	 m=MOVED;
	 session->Open(curr,FA::RETRIEVE,offset);
      }
      res=TryRead(session);
      if(res<0 && res!=FA::DO_AGAIN)
      {
	 NextFile();
	 return MOVED;
      }
      if(res>=0)
	 m=MOVED;
   }

   res=TryWrite(local);
   if(res<0)
   {
      NextFile();
      if(local==global) // global filter failed, cannot do anything.
      {
	 while(curr)
	 {
	    failed++;
	    NextFile();
	 }
      }
      return MOVED;
   }
   if(res>0)
      m=MOVED;

   return m;
}

void CatJob::NextFile()
{
   if(curr)
   {
      if(filter_wait)
      {
	 delete filter_wait;
	 filter_wait=0;
      }
      if(local!=global)
	 filter_wait=local;
      local=0;
   }

   if(!args)
   {
      XferJob::NextFile(0);
      return;
   }

   XferJob::NextFile(args->getnext());
   if(!curr)
      return;
   if(for_each)
      local=new OutputFilter(for_each,global);
   else
      local=global;
}

CatJob::~CatJob()
{
   Bg();

   AcceptSig(SIGTERM);

   if(local && local!=global)
      delete local;
   if(global)
      delete global;
   if(filter_wait)
      delete filter_wait;
   if(args)
      delete args;
};

int CatJob::AcceptSig(int sig)
{
   FDStream *s=0;
   if(local)
      s=local;
   else if(global)
      s=global;
   else if(filter_wait)
      s=filter_wait;
   if(!s || s->GetProcGroup()==0)
   {
      if(sig==SIGINT)
	 return WANTDIE;
      return STALL;
   }
   if(sig!=SIGINT)
      s->Kill(sig);
   if(sig!=SIGCONT)
      AcceptSig(SIGCONT);
   return MOVED;
}

void CatJob::Init()
{
   global=0;
   filter_wait=0;
   local=0;
   for_each=0;

   args=0;
   op="cat";

   print_run_status=false;
}

CatJob::CatJob(FileAccess *new_session,FDStream *new_global,ArgV *new_args)
   : XferJob(new_session)
{
   Init();

   global=new_global;

   args=new_args;
   args->rewind();
   op=args->a0();

   if(!strcmp(op,"more") || !strcmp(op,"zmore"))
   {
      if(!global)
      {
	 const char *pager=getenv("PAGER");
	 if(pager==NULL)
	    pager="more";
	 global=new OutputFilter(pager);
      }
   }
   if(!strcmp(op,"zcat") || !strcmp(op,"zmore"))
      for_each="zcat";

   if(!global)
   {
      if(for_each)
	 global=new OutputFilter("cat"); // To ensure there is a single pgroup
      else
	 global=new FDStream(1,"<stdout>");
   }

   print_run_status=!global->usesfd(1);
}

CatJob::CatJob(FDStream *g,char *data,int data_len) : XferJob(0)
{
   Init();
   global=g;
   if(!global)
      global=new FDStream(1,"<stdout>");
   buffer=data;
   in_buffer=data_len;
   got_eof=true;
   local=global;
   curr="<memory>";
}
