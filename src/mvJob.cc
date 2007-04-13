/*
 * lftp and utils
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "trio.h"
#include "mvJob.h"
#include "misc.h"

mvJob::mvJob(FileAccess *session,const char *from,const char *to) : SessionJob(session)
{
   failed=0;
   if(last_char(to)=='/')
   {
      const char *from_bn=basename_ptr(from);
      char *to1=alloca_strdup2(to,strlen(from_bn));
      strcat(to1,from_bn);
      to=to1;
   }
   session->Rename(from,to);
}

int mvJob::Do()
{
   if(Done())
      return STALL;

   int res=session->Done();
   if(res==FA::IN_PROGRESS)
      return STALL;
   if(res==FA::OK)
   {
      session->Close();
      return MOVED;
   }
   if(res==FA::DO_AGAIN)
      return STALL;
   fprintf(stderr,"%s\n",session->StrError(res));
   failed=1;
   session->Close();
   return MOVED;
}

void  mvJob::PrintStatus(int v,const char *prefix)
{
   SessionJob::PrintStatus(v,prefix);
   if(Done())
      return;
   printf("%s[%s]\n",prefix,session->CurrentStatus());
}

void  mvJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(!Done())
      s->Show("[%s]",session->CurrentStatus());
}

void  mvJob::SayFinal()
{
   if(failed)
      return;
   // xgettext:c-format
   printf(_("rename successful\n"));
}
