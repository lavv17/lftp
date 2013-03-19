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
#include "TreatFileJob.h"
#include "StatusLine.h"
#include "ArgV.h"
#include "misc.h"

TreatFileJob::TreatFileJob(FileAccess *s,ArgV *a)
   : FinderJob(s), args(a)
{
   quiet=false;
   failed=file_count=0;
   Need(FileInfo::NAME);

   curr=0;
   set_maxdepth(0);

   op=args->a0();
   Begin(a->getcurr());
}

/* process a new directory */
void TreatFileJob::Begin(const char *d)
{
   NextDir(d);
}

void TreatFileJob::Finish()
{
   /* next? */
   const char *d=args->getnext();
   if(d) {
      /* we have another argument */
      Begin(d);
      return;
   }
}

TreatFileJob::~TreatFileJob()
{
}

TreatFileJob::prf_res TreatFileJob::ProcessFile(const char *d,const FileInfo *fi)
{
   curr=fi;
   if(session->IsClosed())
   {
      if(!first)
	 first=new FileInfo(*fi);

      TreatCurrent(d,fi);
   }
   int res=session->Done();
   if(res==FA::IN_PROGRESS)
      return PRF_LATER;

   curr=0;
   file_count++;

   if(res<0)
   {
      failed++;
      if(!quiet)
         eprintf("%s: %s\n",op,session->StrError(res));
   }
   CurrentFinished(d,fi);

   session->Close();
   return res<0? PRF_ERR:PRF_OK;
}

xstring& TreatFileJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   SessionJob::FormatStatus(s,v,prefix);
   if(Done() || !curr)
      return s;
   return s.appendf("\t`%s' [%s]\n",curr->name.get(),session->CurrentStatus());
}

void  TreatFileJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(curr && !Done())
      s->Show("%s `%s' [%s]",op,curr->name.get(),session->CurrentStatus());
}
