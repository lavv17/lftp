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

#include <config.h>

#include <errno.h>
#include "mkdirJob.h"
#include "plural.h"
#include "url.h"
#include "misc.h"

#define super SessionJob

mkdirJob::mkdirJob(FileAccess *s,ArgV *a) : super(s)
{
   quiet=false;
   failed=file_count=0;

   args=a;
   args->rewind();
   char *op=args->getarg(0);

   curr=first=0;
   opt_p=false;

   url_session=0;

   int opt;
   while((opt=args->getopt("p"))!=EOF)
   {
      if(opt=='p')
	 opt_p=true;
      else
	 return;
   }
   args->back();
   first=curr=args->getnext();
   if(curr==0)
   {
      fprintf(stderr,"Usage: %s [-p] files...\n",op);
      return;
   }

}

mkdirJob::~mkdirJob()
{
   delete args;
   args=0;
   Reuse();
}

int mkdirJob::Do()
{
   if(Done())
      return STALL;
   if(Session()->IsClosed())
   {
      ParsedURL u(curr,true);
      if(u.proto)
	 url_session=FileAccess::New(&u);
      if(url_session)
	 url_session->Mkdir(u.path,opt_p);
      else
	 session->Mkdir(curr,opt_p);
   }

   int res=Session()->Done();
   if(res==FA::DO_AGAIN || res==FA::IN_PROGRESS)
      return STALL;
   if(res<0)
   {
      failed++;
      if(!quiet)
	 fprintf(stderr,"%s: %s\n",args->getarg(0),Session()->StrError(res));
   }
   file_count++;
   Session()->Close();
   if(url_session)
   {
      SessionPool::Reuse(url_session);
      url_session=0;
   }
   curr=args->getnext();
   return MOVED;
}

void  mkdirJob::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(Done())
      return;

   printf("\t`%s' [%s]\n",curr,Session()->CurrentStatus());
}

void  mkdirJob::ShowRunStatus(StatusLine *s)
{
   if(Done())
      return;

   s->Show("%s `%s' [%s]",args->getarg(0),
      squeeze_file_name(curr,s->GetWidthDelayed()-40),
      Session()->CurrentStatus());
}

void  mkdirJob::SayFinal()
{
   if(failed==file_count)
      return;
   char *op=args->getarg(0);
   if(file_count==1)
      // A directory has just been created
      printf(_("%s ok, `%s' created\n"),op,first);
   else if(failed)
      printf(plural("%s failed for %d of %d director$y|ies$\n",file_count),
	    op,failed,file_count);
   else
      printf(plural("%s ok, %d director$y|ies$ created\n",file_count),
	    op,file_count);
}

void mkdirJob::Fg()
{
   super::Fg();
   if(url_session)
      url_session->SetPriority(1);
}
void mkdirJob::Bg()
{
   if(url_session)
      url_session->SetPriority(0);
   super::Bg();
}
