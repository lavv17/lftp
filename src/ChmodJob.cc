/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <assert.h>
#include "ChmodJob.h"
#include "url.h"
#include "filemode.h"

void ChmodJob::Init()
{
   verbose=V_NONE;
   m=0;
   simple_mode=-1;
}

ChmodJob::ChmodJob(FileAccess *s,ArgV *a) : TreatFileJob(s,a)
{
   Init();
}

ChmodJob::ChmodJob(FileAccess *s,int mode,ArgV *a) : TreatFileJob(s,a)
{
   Init();
   simple_mode=mode;
}

ChmodJob::~ChmodJob()
{
   mode_free(m);
}

void ChmodJob::Recurse()
{
   set_maxdepth(-1);
}

int ChmodJob::GetMode(const FileInfo *fi) const
{
   if(simple_mode != -1)
      return simple_mode;

   return mode_adjust(fi->mode, m);
}

void ChmodJob::CurrentFinished(const char *d,const FileInfo *fi)
{
   const char *fmt;
   if(session->Done() < 0)
   {
      if(quiet)
	 return;
      fmt = _("failed to change mode of %s to %04lo (%s)\n");
   }
   else
      fmt = _("mode of %s changed to %04lo (%s)\n");


   unsigned mode = GetMode(fi);
   if(verbose == V_ALL || (verbose == V_CHANGES && mode != fi->mode))
   {
      char perms[11];               /* "-rwxrwxrwx" ls-style modes. */

      mode_string (mode, perms);
      perms[10] = '\0';             /* `mode_string' does not null terminate. */

      eprintf (fmt, fi->name, (unsigned long) mode, perms+1);
   }
}

void ChmodJob::SetMode(mode_change *newm)
{
   m=newm;
   /* request mode info only if we need it */
   if(RelativeMode(m))
      Need(FileInfo::MODE);

   /* one or the other */
   assert(simple_mode == -1);
}

void ChmodJob::SetVerbosity(verbosity v)
{
   verbose=v;

   /* need file mode to show changes */
   if(verbose == V_CHANGES)
      Need(FileInfo::MODE);
}

bool ChmodJob::RelativeMode(const mode_change *m) const
{
   while(m)
   {
      if(m->flags || m->op != '=')
	 return true;
      m=m->next;
   }
   return false;
}

void ChmodJob::TreatCurrent(const char *d,const FileInfo *fi)
{
   session->Chmod(fi->name,GetMode(fi));
}
