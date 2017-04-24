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

#ifndef MGETJOB_H
#define MGETJOB_H

#include "GetJob.h"
#include "FileGlob.h"
#include "mkdirJob.h"

class mgetJob : public GetJob
{
   Ref<GlobURL> glob;
   xqueue_m<char> wcd;
   xstring_c output_dir;

   FileAccessRef local_session;

public:
   int	 Do();
   xstring& FormatStatus(xstring&,int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);

   mgetJob(FileAccess *session,const ArgV *args,bool c,bool md);
   ~mgetJob();

   void OutputDir(const char *o) { output_dir.set(o); }
};

#endif // MGETJOB_H
