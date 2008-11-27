/*
 * lftp and utils
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

/* $Id$ */

#ifndef MGETJOB_H
#define MGETJOB_H

#include "GetJob.h"
#include "FileGlob.h"
#include "mkdirJob.h"

class mgetJob : public GetJob
{
   Ref<GlobURL> rg;
   JobRef<mkdirJob> mkdir_job;
   Ref<ArgV> mkdir_args;
   int mkdir_base_arg;
   Ref<ArgV> m_args;
   xstring_c output_dir;

   bool	 make_dirs;
   void	 make_directory(const char *d);

   void LocalGlob(const char *p);

public:
   int	 Do();
   void	 PrintStatus(int,const char *);
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);

   mgetJob(FileAccess *session,ArgV *args,bool c,bool md);
   ~mgetJob();

   void OutputDir(char *o) { output_dir.set_allocated(o); }
};

#endif // MGETJOB_H
