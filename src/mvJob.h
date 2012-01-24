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

#ifndef MVJOB_H
#define MVJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "ArgV.h"
#include "trio.h"

class mvJob : public SessionJob
{
   xstring_c from;
   xstring to;
   FA::open_mode m;
   bool	 remove_target;
   bool	 failed;
   bool	 done;

public:
   int	 Do();
   int	 Done() { return done; }
   int	 ExitCode() { return failed; }

   xstring& FormatStatus(xstring&,int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);
   void	 SayFinal();

   void	 doOpen() const;
   const char *cmd() const { return m==FA::RENAME ? "mv" : "ln"; }

   mvJob(FileAccess *session,const char *f,const char *t,FA::open_mode m=FA::RENAME);
   void RemoveTargetFirst() { remove_target=true; }
};

#endif // MVJOB_H
