/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "url.h"

#define super CopyJobEnv

int CatJob::Done()
{
   return super::Done() && global==0;
}

int   CatJob::Do()
{
   if(!fg_data && global && global->GetProcGroup())
      fg_data=new FgData(global->GetProcGroup(),fg);
   if(Done())
      return STALL;
   if(super::Done())
   {
      if(global->Done())
      {
	 delete global;
	 global=0;
	 return MOVED;
      }
   }
   return super::Do();
}

void CatJob::NextFile()
{
   const char *src=args->getnext();

   if(src==0)
   {
      SetCopier(0,0);
      return;
   }

   FileCopyPeerFA *src_peer=FileCopyPeerFA::New(session,src,FA::RETRIEVE,false);

   FileCopyPeerFDStream *dst_peer=0;
   if(for_each)
   {
      OutputFilter *out=new OutputFilter(for_each,global);
      out->SetCwd(cwd);
      dst_peer=new FileCopyPeerFDStream(out,FileCopyPeer::PUT);
      dst_peer->DontCreateFgData();
   }
   else
   {
      dst_peer=new FileCopyPeerFDStream(global,FileCopyPeer::PUT);
      dst_peer->DontDeleteStream();
   }

   FileCopy *copier=FileCopy::New(src_peer,dst_peer,false);
   copier->DontCopyDate();
   if(ascii || (auto_ascii && (isatty(global->getfd()) || global->usesfd(1))))
   {
      if(global->usesfd(1))
	 copier->LineBuffered();
      copier->Ascii();
   }
   SetCopier(copier,src);

   if(no_status)
      cp->NoStatus();
}

CatJob::~CatJob()
{
   Bg();

   AcceptSig(SIGTERM);

   if(global)
      delete global;
};

int CatJob::AcceptSig(int sig)
{
   pid_t grp=0;
   if(cp)
      grp=cp->GetProcGroup();
   if(global && grp==0)
      grp=global->GetProcGroup();
   if(grp==0)
   {
      if(sig==SIGINT)
	 return WANTDIE;
      return STALL;
   }
   if(cp)
      cp->AcceptSig(sig);
   if(global && !cp)
      global->Kill(sig);
   if(sig!=SIGCONT)
      AcceptSig(SIGCONT);
   return MOVED;
}

CatJob::CatJob(FileAccess *new_session,FDStream *new_global,ArgV *new_args)
   : CopyJobEnv(new_session,new_args)
{
   global=new_global;
   for_each=0;
   ascii=false;
   auto_ascii=true;

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
   {
      for_each="zcat";
      Binary();
   }

   if(!global)
   {
      if(for_each)
	 global=new OutputFilter("cat"); // To ensure there is a single pgroup
      else
	 global=new FDStream(1,"<stdout>");
   }
   no_status=global->usesfd(1);
}
