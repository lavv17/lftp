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

   ParsedURL src_url(src,true);
   FileCopyPeerFA *src_peer=0;
   if(src_url.proto==0)
   {
      src_peer=new FileCopyPeerFA(session,src,FA::RETRIEVE);
      src_peer->DontReuseSession();
   }
   else
      src_peer=new FileCopyPeerFA(&src_url,FA::RETRIEVE);

   FileCopyPeerFDStream *dst_peer=0;
   if(for_each)
   {
      dst_peer=new FileCopyPeerFDStream(new OutputFilter(for_each,global),FileCopyPeer::PUT);
      dst_peer->DontCreateFgData();
   }
   else
   {
      dst_peer=new FileCopyPeerFDStream(global,FileCopyPeer::PUT);
      dst_peer->DontDeleteStream();
   }

   if(ascii)
   {
      src_peer->Ascii();
      dst_peer->Ascii();
   }

   FileCopy *copier=new FileCopy(src_peer,dst_peer,false);
   copier->DontCopyDate();
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
   no_status=global->usesfd(1);
}
