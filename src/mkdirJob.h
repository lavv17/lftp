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

#ifndef MKDIRJOB_H
#define MKDIRJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "ArgV.h"
#include "trio.h"

class mkdirJob : public SessionJob
{
   ArgV	 *args;
   const char *curr,*first;
   FA	 *orig_session;
   int	 failed,file_count;
   bool	 quiet;
   bool	 opt_p;

public:
   int	 Do();
   int	 Done() { return curr==0; }
   int	 ExitCode() { return failed; }

   void	 PrintStatus(int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);
   void	 SayFinal();

   mkdirJob(FileAccess *session,ArgV *a);
   ~mkdirJob();

   void	 BeQuiet() { quiet=true; }

   void Fg();
   void Bg();
};

#endif // MKDIRJOB_H
