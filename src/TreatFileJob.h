/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef TREATFILEJOB_H
#define TREATFILEJOB_H

#include "Job.h"

class StatusLine;
class ArgV;

class TreatFileJob : public SessionJob
{
protected:
   ArgV	 *args;
   const char *curr;
   const char *first;
   const char *op;
   int	 failed,file_count;
   bool	 quiet;
   bool  done;

   virtual void	TreatCurrent() = 0;

public:
   int	 Do();
   int	 Done() { return done; }
   int	 ExitCode() { return failed; }

   void	 PrintStatus(int);
   void	 ShowRunStatus(StatusLine *);

   TreatFileJob(FileAccess *session,ArgV *a);
   ~TreatFileJob();

   void	 BeQuiet() { quiet=true; }

   void	 AddFile(const char *f);
};

#endif // TREATFILEJOB_H
