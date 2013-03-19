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
#include "rmJob.h"
#include "plural.h"
#include "url.h"
#include "misc.h"

rmJob::rmJob(FileAccess *s,ArgV *a) : TreatFileJob(s,a)
{
   mode=FA::REMOVE;
   recurse=false;
   depth_first=true;
}

void rmJob::Recurse()
{
   set_maxdepth(-1);
   Need(FileInfo::TYPE);
   recurse=true;
}

void  rmJob::SayFinal()
{
   if(failed==file_count)
      return;
   if(file_count==1)
      printf(_("%s ok, `%s' removed\n"),op,first->name.get());
   else if(failed)
   {
      if(mode==FA::REMOVE_DIR)
	 printf(plural("%s failed for %d of %d director$y|ies$\n",file_count),
	    op,failed,file_count);
      else
	 printf(plural("%s failed for %d of %d file$|s$\n",file_count),
	    op,failed,file_count);
   }
   else
   {
      if(mode==FA::REMOVE_DIR)
	 printf(plural("%s ok, %d director$y|ies$ removed\n",file_count),
	    op,file_count);
      else
	 printf(plural("%s ok, %d file$|s$ removed\n",file_count),
	    op,file_count);
   }
}

void rmJob::TreatCurrent(const char *d, const FileInfo *fi)
{
   /* If we're recursing and this is a directory, rmdir it.  (If we're
    * not recursing, just send an rm and let it fail.) */
   if(recurse && (fi->defined&fi->TYPE) && (fi->filetype==fi->DIRECTORY))
      session->Open(fi->name,FA::REMOVE_DIR);
   else
      session->Open(fi->name,mode);
}
