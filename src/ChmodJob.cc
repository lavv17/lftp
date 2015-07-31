/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <assert.h>
#include <unistd.h>
#include "ChmodJob.h"
#include "url.h"

CDECL_BEGIN
#include "filemode.h"
#include "modechange.h"
CDECL_END

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
   free(m);
}

void ChmodJob::Recurse()
{
   set_maxdepth(-1);
   Need(FileInfo::TYPE);
}

int ChmodJob::GetMode(const FileInfo *fi) const
{
   if(simple_mode != -1)
      return simple_mode;

   if(fi->defined&fi->MODE)
      return mode_adjust(fi->mode, false, 022, m, NULL);
   if(!RelativeMode(m))
      return mode_adjust(0, false, 022, m, NULL);

   return -1;
}

void ChmodJob::CurrentFinished(const char *d,const FileInfo *fi)
{
   const char *fmt;
   if(session->Done() < 0)
   {
      if(quiet)
	 return;
      fmt = _("Failed to change mode of `%s' to %04o (%s).\n");
   }
   else
      fmt = _("Mode of `%s' changed to %04o (%s).\n");

   int mode=GetMode(fi);
   if(mode==-1)
   {
      eprintf(_("Failed to change mode of `%s' because no old mode is available.\n"),fi->name.get());
      return;
   }
   if(verbose == V_ALL || (verbose == V_CHANGES
			 && (!(fi->defined&fi->mode) || mode != (int)fi->mode)))
   {
      char perms[12];               /* "-rwxrwxrwx " ls-style modes. */

      strmode (mode, perms);
      perms[10] = '\0';

      eprintf (fmt, fi->name.get(), (int) mode, perms+1);
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
   // relative mode change depends on original mode.
   return mode_adjust(07777, false, 0, m, NULL)
       != mode_adjust(00000, false, 0, m, NULL);
}

void ChmodJob::TreatCurrent(const char *d,const FileInfo *fi)
{
   int new_mode=GetMode(fi);
   if(new_mode!=-1)
      session->Chmod(fi->name,new_mode);
}
