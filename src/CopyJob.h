/*
 * lftp and utils
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef COPYJOB_H
#define COPYJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "FileCopy.h"

class CopyJob : public Job
{
   FileCopy *c;
   bool done;
   char *name;
   bool print_run_status;

public:
   CopyJob(FileCopy *c1,const char *n);
   ~CopyJob();

   void NoStatus() { print_run_status=false; }

   int Do();
   int Done();
   int ExitCode();

   void ShowRunStatus(StatusLine *s);

   static CopyJob *NewGet(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewPut(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewEcho(const char *str,int len,FDStream *o);
   static CopyJob *NewEcho(const char *str,FDStream *o)
      { return NewEcho(str,strlen(str),o); }
};

#endif // COPYJOB_H
