/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef EDITJOB_H
#define EDITJOB_H

#include "Job.h"

class EditJob : public SessionJob
{
   xstring_c file;
   xstring_c temp_file;
   bool keep;
   JobRef<Job> get;
   JobRef<Job> editor;
   JobRef<Job> put;
   time_t mtime;
   int exit_code;
   bool done;

   int HandleJob(JobRef<Job>& j, bool fail=true);
   void Finish(int code);

public:
   EditJob(FileAccess *s,const char *f,const char *t)
      : SessionJob(s), file(f), temp_file(t), keep(false),
        mtime(0), exit_code(0), done(false) {}
   int Do();
   int Done() { return done; }
   int ExitCode() { return exit_code; }
   void KeepTempFile(bool k) { keep=k; }
};

#endif//EDITJOB_H
