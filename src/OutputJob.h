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

#ifndef OUTPUTJOB_H
#define OUTPUTJOB_H

#include "Job.h"
#include "FileCopy.h"
#include "CopyJob.h"
#include "Timer.h"

class StatusBar;

class OutputJob : public Job
{
   /* Main CopyJob: */
   CopyJob *input;

   /* CopyJob that sends to the output.  (output may be equal to input) */
   CopyJob *output;

   Ref<Buffer> tmp_buf;	// to store data while !initialized
   Ref<FDStream> output_fd;	// to initialize CopyJobs
   FileAccessRef fa;
   xstring_c fa_path;

   bool initialized;

   xstring_c a0;
   xstring_c filter;

   bool error;
   bool is_stdout;
   bool fail_if_broken;
   bool statusbar_redisplay;

   int width;
   bool is_a_tty;

   /* if true, we never contribute to the parent job's status
    * (Status() == "") */
   bool no_status;

   Timer update_timer;

   void Init(const char *a0);
   void InitCopy();

   /* Get the input FileCopyPeer */
   const SMTaskRef<FileCopyPeer>& InputPeer() const;

   /* Get the output FileCopyPeer (the FileCopyPeer that's doing the final output) */
   const SMTaskRef<FileCopyPeer>& OutputPeer() const;

public:
   OutputJob(FDStream *output, const char *a0);
   OutputJob(const char *path, const char *a0, FA *fa=0);
   void PrepareToDie();

   /* Set the main filter: */
   void SetFilter(const char *f) { filter.set(f); }
   /* Prepend a filter before the main filter: */
   void PreFilter(const char *f);

   void DontFailIfBroken(bool y=true) { fail_if_broken=!y; }
   bool Error();

   int Done();
   int Do();

   void Put(const char *buf,int size);
   void Put(const char *buf) { Put(buf,strlen(buf)); }
   void Put(const xstring& str) { Put(str.get(),str.length()); }
   void Format(const char *f,...) PRINTF_LIKE(2,3);
   void PutEOF();

   /* If sending large amounts of data, call this function and stop
    * sending if it returns true.  (This always accept more input;
    * this is optional.) */
   bool Full();

   /* Get properties of the output: */
   int GetWidth() const;
   bool IsTTY() const;
   /* Whether the ultimate destination is stdout: */
   bool IsStdout() const { return is_stdout; }
   /* Whether the output is filtered: */
   bool IsFiltered() const { return filter; }

   /* Call before showing a StatusLine on a job using this class.  If it
    * returns false, don't display it. */
   bool ShowStatusLine(const SMTaskRef<StatusLine>&);

   /* For commands that stream output from servers, redisplaying the
    * statusbar when output becomes idle can be annoying, especially
    * if the line is rate-limited. */
   void DontRedisplayStatusbar() { statusbar_redisplay=false; }

   const char *Status(const StatusLine *s);

   void Fg();
   void Bg();
   void SuspendInternal();
   void ResumeInternal();
   int AcceptSig(int sig);
};

#endif
