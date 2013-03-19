/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "trio.h"
#include <errno.h>
#include <unistd.h>

#include "CatJob.h"
#include "ArgV.h"
#include "FileCopyOutputJob.h"

#define super CopyJobEnv

int CatJob::ExitCode()
{
   return errors!=0 || output->Error();
}

int CatJob::Done()
{
   return super::Done() && output->Done();
}

int   CatJob::Do()
{
   int m=STALL;

   if(!done && output->Done())
   {
      done=true;
      m=MOVED;
   }

   return m||super::Do();
}

void CatJob::NextFile()
{
   const char *src=args->getnext();

   if(src==0)
   {
      SetCopier(0,0);
      output->PutEOF();
      return;
   }

   FileCopyPeerFA *src_peer=FileCopyPeerFA::New(session,src,FA::RETRIEVE);
   FileCopyPeer *dst_peer=new FileCopyPeerOutputJob(output);

   FileCopy *copier=FileCopy::New(src_peer,dst_peer,false);
   copier->DontCopyDate();

   if(ascii || (auto_ascii && output->IsTTY()))
   {
      if(output->IsStdout())
	 copier->LineBuffered();

      copier->Ascii();
   }

   SetCopier(copier,src);
}

CatJob::CatJob(FileAccess *new_session,OutputJob *_output,ArgV *new_args)
   : CopyJobEnv(new_session,new_args), output(_output)
{
   output->SetParentFg(this);
   ascii=false;
   auto_ascii=true;

   output->DontRedisplayStatusbar();

   if(!strcmp(op,"more") || !strcmp(op,"zmore") || !strcmp(op,"bzmore"))
   {
      const char *pager=getenv("PAGER");
      if(pager==NULL)
	 pager=DEFAULT_PAGER;
      output->PreFilter(pager);
   }
   if(!strcmp(op,"zcat") || !strcmp(op,"zmore"))
   {
      output->PreFilter("zcat");
      Binary();
   }

   if(!strcmp(op,"bzcat") || !strcmp(op,"bzmore"))
   {
      output->PreFilter("bzcat");
      Binary();
   }
}

void CatJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(cp && cp->HasStatus() && output->ShowStatusLine(s))
      cp->ShowRunStatus(s);
}
