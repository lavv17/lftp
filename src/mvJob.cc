/*
 * lftp and utils
 *
 * Copyright (c) 1996-2010 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <config.h>
#include <errno.h>
#include "trio.h"
#include "mvJob.h"
#include "misc.h"

mvJob::mvJob(FileAccess *session,const char *f,const char *t,FA::open_mode m1)
 : SessionJob(session), from(f), to(t), m(m1),
   remove_target(false), failed(false), done(false)
{
   if(to.last_char()=='/')
      to.append(basename_ptr(from));
   doOpen();
}

void mvJob::doOpen() const
{
   if(remove_target)
      session->Open(to,FA::REMOVE);
   else
      session->Open2(from,to,m);
}

int mvJob::Do()
{
   if(Done())
      return STALL;

   int res=session->Done();
   if(res==FA::IN_PROGRESS || res==FA::DO_AGAIN)
      return STALL;
   if(res!=FA::OK && !remove_target) {
      fprintf(stderr,"%s: %s\n",cmd(),session->StrError(res));
      done=failed=true;
   }
   session->Close();
   if(remove_target) {
      remove_target=false;
      doOpen();
   } else
      done=true;
   return MOVED;
}

void  mvJob::PrintStatus(int v,const char *prefix)
{
   SessionJob::PrintStatus(v,prefix);
   if(Done())
      return;
   if(remove_target)
      printf("%srm %s [%s]\n",prefix,to.get(),session->CurrentStatus());
   else
      printf("%s%s %s=>%s [%s]\n",prefix,cmd(),from.get(),to.get(),session->CurrentStatus());
}

void  mvJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Done())
      return;
   if(remove_target)
      s->Show("rm %s [%s]\n",to.get(),session->CurrentStatus());
   else
      s->Show("%s %s=>%s [%s]\n",cmd(),from.get(),to.get(),session->CurrentStatus());
}

void  mvJob::SayFinal()
{
   if(failed)
      return;
   if(m==FA::RENAME) {
      // xgettext:c-format
      printf(_("rename successful\n"));
   }
}
