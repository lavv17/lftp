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

#include <errno.h>
#include "mkdirJob.h"
#include "plural.h"
#include "url.h"
#include "misc.h"

#define super SessionJob
#define orig_session super::session

mkdirJob::mkdirJob(FileAccess *s,ArgV *a)
   : super(s), args(a), session(orig_session)
{
   quiet=false;
   failed=file_count=0;

   args->rewind();
   const char *op=args->getarg(0);

   curr=first=0;
   opt_p=false;

   int opt;
   while((opt=args->getopt("pf"))!=EOF)
   {
      switch(opt)
      {
      case 'p':
	 opt_p=true;
	 break;
      case 'f':
	 quiet=true;
	 break;
      default:
	 return;
      }
   }
   args->back();
   first=curr=args->getnext();
   if(curr==0)
   {
      fprintf(stderr,"Usage: %s [-p] [-f] paths...\n",op);
      return;
   }
}

int mkdirJob::Do()
{
   if(Done())
      return STALL;
   if(session->IsClosed())
   {
      ParsedURL u(curr,true);
      if(u.proto)
      {
	 session=my_session=FileAccess::New(&u);
	 session->SetPriority(fg?1:0);
	 session->Mkdir(u.path,opt_p);
      }
      else
      {
	 session=orig_session;
	 session->Mkdir(curr,opt_p);
      }
   }

   int res=session->Done();
   if(res==FA::DO_AGAIN || res==FA::IN_PROGRESS)
      return STALL;
   if(res<0)
   {
      failed++;
      if(!quiet)
	 fprintf(stderr,"%s: %s\n",args->getarg(0),session->StrError(res));
   }
   file_count++;
   session->Close();
   curr=args->getnext();
   return MOVED;
}

xstring& mkdirJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   SessionJob::FormatStatus(s,v,prefix);
   if(Done())
      return s;

   return s.appendf("%s`%s' [%s]\n",prefix,curr,session->CurrentStatus());
}

void  mkdirJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Done())
      return;

   s->Show("%s `%s' [%s]",args->getarg(0),
      squeeze_file_name(curr,s->GetWidthDelayed()-40),
      session->CurrentStatus());
}

void  mkdirJob::SayFinal()
{
   if(failed==file_count)
      return;
   const char *op=args->getarg(0);
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
   if(orig_session!=session)
      orig_session->SetPriority(1);
}
void mkdirJob::Bg()
{
   if(orig_session!=session)
      orig_session->SetPriority(0);
   super::Bg();
}
