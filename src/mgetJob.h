/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef MGETJOB_H
#define MGETJOB_H

#include "GetJob.h"

class mgetJob : public GetJob
{
   GlobURL *rg;
   class mkdirJob *mkdir_job;
   ArgV *mkdir_args;
   int mkdir_base_arg;
   ArgV *m_args;
   char *output_dir;

   bool	 make_dirs;
   void	 make_directory(char *d);

   void LocalGlob(const char *p);

public:
   int	 Do();
   void	 PrintStatus(int);
   void	 ShowRunStatus(StatusLine *s);

   mgetJob(FileAccess *session,ArgV *args,bool c,bool md);
   ~mgetJob();

   void OutputDir(char *o) { output_dir=o; }
};

#endif // MGETJOB_H
