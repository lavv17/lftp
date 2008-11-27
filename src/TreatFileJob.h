/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef TREATFILEJOB_H
#define TREATFILEJOB_H

#include "Job.h"
#include "FindJob.h"

class StatusLine;
class ArgV;

class TreatFileJob : public FinderJob
{
protected:
   Ref<ArgV> args;
   const FileInfo *curr;
   Ref<FileInfo> first;
   int	 failed,file_count;

   virtual void	TreatCurrent(const char *d,const FileInfo *fi) = 0;
   virtual void CurrentFinished(const char *d,const FileInfo *fi) { }

   void Begin(const char *d);

   /* virtuals */
   void Finish();
   prf_res ProcessFile(const char *d,const FileInfo *fi);

public:
   void	 PrintStatus(int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);

   TreatFileJob(FileAccess *session,ArgV *a);
   virtual ~TreatFileJob();
};

#endif // TREATFILEJOB_H
