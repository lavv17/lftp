/*
 * lftp and utils
 *
 * Copyright (c) 2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef OUTPUTJOB_H
#define OUTPUTJOB_H

#include "Job.h"
#include "FileCopy.h"
#include "CopyJob.h"
#include "TimeDate.h"

class StatusBar;

class OutputJob : public Job
{
   /* Main CopyJob: */
   CopyJob *input;

   /* CopyJob that sends to the output.  (output may be equal to input) */
   CopyJob *output;

   FDStream *output_fd;

   bool initialized;

   char *a0;
   char *filter;

   bool error;
   bool is_stdout;
   bool fail_if_broken;
   bool eof;

   /* if true, we never contribute to the parent job's status
    * (Status() == "") */
   bool no_status;

   Time last;
   TimeInterval inter;

   void Init(const char *a0);
   void InitCopy();

   void SetError(const char *e, ...) PRINTF_LIKE(2,3);

   /* Get the input FileCopyPeer */
   FileCopyPeer *InputPeer() const;

   /* Get the output FileCopyPeer (the FileCopyPeer that's doing the final output) */
   FileCopyPeer *OutputPeer() const;

public:
   OutputJob(FDStream *output, const char *a0);
   OutputJob(const char *path, const char *a0, FA *fa=0);
   ~OutputJob();

   /* Set the main filter: */
   void SetFilter(const char *filter);
   /* Prepend a filter before the main filter: */
   void PreFilter(const char *filter);

   void DontFailIfBroken(bool n=false) { fail_if_broken=n; }
   bool Error();

   int Done();
   int Do();

   void Put(const char *buf,int size);
   void Put(const char *buf) { return Put(buf,strlen(buf)); }
   void Format(const char *f,...) PRINTF_LIKE(2,3);
   void PutEOF();

   /* Return true if our buffers don't want any more input.  (They'll always
    * *accept* more input; this is optional.) */
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
   bool ShowStatusLine();

   const char *Status(const StatusLine *s);
   void Reconfig(const char *r);

   void Fg();
   void Bg();
   void Suspend();
   void Resume();
   int AcceptSig(int sig);
};

#endif
