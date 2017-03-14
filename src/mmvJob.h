/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef MMVJOB_H
#define MMVJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "ArgV.h"
#include "trio.h"

class mmvJob : public SessionJob
{
   xstring_c op;
   xqueue_m<char> wcd;
   xqueue_m<char> src;
   xstring dst_dir;
   xstring curr_dst;
   xstring curr_src;
   SMTaskRef<Glob> glob;
   FA::open_mode m;
   bool	 remove_target;
   int	 moved_count;
   int	 error_count;
   bool	 done;

   void	doOpen() const;
   bool isRemoving() const { return session->OpenMode()==FA::REMOVE; }

public:
   int	 Do();
   int	 Done() { return done; }
   int	 ExitCode() { return error_count>0; }

   xstring& FormatStatus(xstring&,int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);
   void	 SayFinal();

   void	 doOpen(const char *src) const;
   const char *cmd() const { return op; }

   mmvJob(FileAccess *session,const ArgV *args,const char *t,FA::open_mode m=FA::RENAME);
   void RemoveTargetFirst() { remove_target=true; }
};

#endif // MMVJOB_H
