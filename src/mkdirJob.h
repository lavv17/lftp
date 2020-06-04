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

#ifndef MKDIRJOB_H
#define MKDIRJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "ArgV.h"
#include "trio.h"

class mkdirJob : public SessionJob
{
   Ref<ArgV> args;
   const char *curr,*first;
   FileAccessRef my_session;
   FileAccessRefC session;
   int	 failed,file_count;
   bool	 quiet;
   bool	 opt_p;

public:
   int	 Do();
   int	 Done() { return curr==0; }
   int	 ExitCode() { return failed!=0 && !quiet; }

   xstring& FormatStatus(xstring&,int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);
   void	 SayFinal();

   mkdirJob(FileAccess *session,ArgV *a);
   void PrepareToDie() { session->Close(); SessionJob::PrepareToDie(); }

   void	 BeQuiet() { quiet=true; }

   void Fg();
   void Bg();
};

#endif // MKDIRJOB_H
