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

/* Usage notes:
 *
 * Set AllowPostpone to true if sending large amounts of data.  Check the
 * result of each Put and Format call to see if a write was postponed.
 * If disabled, writes will always succeed.
 *
 * This is useful for jobs with a lot of output, like "cat". This can be
 * set selectively, where convenient.  For example, a job which outputs a
 * line of formatted text, followed by the contents of a file, can send
 * the first line with AllowPostpone off, then the file with it on.
 *
 * Call PreFilter() to add a filter to the beginning of the chain; these
 * filters are initialized only once for all data.  For example,
 * PreFilter("wc -l")
 *
 */

/*
 * Implementation notes:
 * Background things we can't get around:
 * We must buffer (via FileCopy) output to a filter, since it might block.
 *
 * We must buffer the output from the filter to an output FileCopyPeer (ie.
 * a URL), for the same reason.
 *
 * So, we're stuck with having up to two FileCopy's.  (One to send, one to filter.)
 *
 * In some cases, we only need one: if the output is an FD, the filter can
 * hook up directly and we can forget about that stage.
 *
 * In the case where we're outputting to a URL, we set up a FileCopy from a
 * pipe to the URL, and then pretend we're just outputting to an FD (the
 * pipe.)
 *
 * to it and pretend we're just outputting to a file; this simplifies things
 * significantly.  This means in the simple case of having no filters at
 * all, writing to a URL or file, we send the data an extra time through
 * a FileCopy and a pipe.  That's a bit inefficient, but that's
 * "cat file1 > file2"; that's normally done with "get file1 -o file2", so
 * this shouldn't happen often.
 *
 * It's very important that if the output is stdout, any filters point directly
 * at it, not through an extra copy: a pager, for example, will expect the output
 * to be a TTY.
 *
 */
#include <config.h>

#include "OutputJob.h"
#include "ArgV.h"
#include "FileCopy.h"
#include "CopyJob.h"
#include "url.h"
#include "misc.h"
#include "StatusLine.h"
#include "LocalAccess.h"

#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <fcntl.h>

#define super Job

void OutputJob::InitCopy()
{
   if(error)
      return;

   if(initialized)
      return;

   initialized=true;

   if(filter)
   {
      /* Create the global filter: */
      OutputFilter *global = new OutputFilter(filter, output_fd);
      global->DeleteSecondaryStream();
      output_fd=global;
   }

   /* Use a FileCopy to buffer our output to the filter: */
   FileCopyPeerFDStream *out = new FileCopyPeerFDStream(output_fd, FileCopyPeer::PUT);
   out->DontDeleteStream();

   FileCopy *input_fc = FileCopy::New(new FileCopyPeer(FileCopyPeer::GET), out, false);

   if(!fail_if_broken)
      input_fc->DontFailIfBroken();

   char *buf = xasprintf(_("%s (filter)"), a0);
   input=new CopyJob(input_fc, buf, filter?filter:a0);
   xfree(buf);

   if(!output)
      output=input;

   input->SetParentFg(this);
   InputPeer()->SetDate(NO_DATE);
   InputPeer()->SetSize(NO_SIZE);
   input->GetCopy()->DontCopyDate();
   input->NoStatus();

   if(input != output)
   {
      output->SetParentFg(this);
      OutputPeer()->SetDate(NO_DATE);
      OutputPeer()->SetSize(NO_SIZE);
      output->GetCopy()->DontCopyDate();
      output->NoStatus();
   }

   if(is_stdout)
   {
      output->ClearStatusOnWrite();
      output->GetCopy()->LineBuffered();
   }

   Timeout(0);
}

void OutputJob::Init(const char *_a0)
{
   input=output=0;
   filter=0;
   initialized=false;
   error=false;
   no_status=false;
   eof=false;
   a0=xstrdup(_a0);
   last.Set(0,0);
   is_stdout=false;
   fail_if_broken=true;
   output_fd=0;
}

/* Local (fd) output. */
OutputJob::OutputJob(FDStream *output_, const char *a0):
   inter(1)
{
   Init(a0);

   output_fd=output_;

   if(!output_fd)
      output_fd=new FDStream(1,"<stdout>");
   else
      // some legitimate uses produce broken pipe condition (cat|head)
      // TODO: once actual piping uses OutputJob, set this only when
      // really doing a pipe, so cat>file can produce broken pipe
      fail_if_broken=false;

   is_stdout=output_fd->usesfd(1);

   /* We don't output status when outputting locally. */
   no_status=true;

   /* Make sure that if the output is going to fail, it fails early, so
    * the parent doesn't start anything expensive (like begin downloading
    * a file.) */
   if(output_fd->getfd() == -1)
   {
      eprintf("%s: %s\n", a0, output_fd->error_text);
      error=true;
   }
}

OutputJob::OutputJob(const char *path, const char *a0, FileAccess *fa):
   inter(1)
{
   Init(a0);

   /* Set up a pipe sending data at the peer, so we can act like the FDStream
    * constructor. */
   int filter_pipe[2];

   if(pipe(filter_pipe) == -1) {
      /* FIXME: This can be retryable. */
      eprintf("%s: %s\n", a0, strerror(errno));
      error=true;
      /* This won't actually be written to, since error is set, but we must set
       * it to something. */
      output_fd=new FDStream(1, "<stdout>");
      return;
   }

   bool reuse = false;
   if(!fa)
   {
      fa = new LocalAccess;
      reuse = true;
   }

   FileCopyPeerFA *dst_peer = FileCopyPeerFA::New(fa, path, FA::STORE, reuse);

   /* Status only for remote outputs. */
   if(!strcmp(dst_peer->GetProto(), "file"))
      no_status=true;

   fcntl(filter_pipe[0],F_SETFL,O_NONBLOCK);
   fcntl(filter_pipe[1],F_SETFL,O_NONBLOCK);

   /* The output of the pipe (0) goes to the output FileCopy. */
   FDStream *pipe_output = new FDStream(filter_pipe[0],"<filter-out>");

   FileCopy *output_fc=FileCopy::New(new FileCopyPeerFDStream(pipe_output, FileCopyPeer::GET), dst_peer,false);
   output=new CopyJob(output_fc, path, a0);

   output_fd=new FDStream(filter_pipe[1],"<filter-in>");

   pipe_output->CloseFD();
   output_fd->CloseFD();
}

OutputJob::~OutputJob()
{
   Bg();
   AcceptSig(SIGTERM);

   Delete(input);
   if(input != output)
      Delete(output);
   delete output_fd;

   xfree(a0);
   xfree(filter);
}

void OutputJob::Reconfig(const char *r)
{
   if(!r || !strcmp(r,"cmd:status-interval"))
   {
      inter=TimeInterval((const char*)ResMgr::Query("cmd:status-interval",0));
   }
}

bool OutputJob::ShowStatusLine()
{
   /* If our output file is gone, or isn't stdout, we don't care, */
   if(!output || !is_stdout)
      return true;

   /* If we're filtered, we never display at all.  (We don't know anything about
    * the filter's output; the only time we do is when we're outputting to a URL
    * or a file, and that doesn't apply here.) */
   if(IsFiltered())
      return false;

   /* If we're not line buffered, display only if the output CopyJob says to. */
   if(!output->GetCopy()->IsLineBuffered())
      return output->HasStatus();

   /* We're line buffered, so we can output a status line without stomping
    * on a partially output line.
    *
    * Don't display the statusline if the we've output something within the
    * last status interval, so if we're currently bursting output we won't
    * flicker status for no reason.  (Actually, we should be concerned about
    * the last time the output peer has sent something...) */
   if(now - last < inter)
      return false;

   last = now;

   /* Stop the output again, so the FileCopy will clear the StatusLine
    * when there's more data. */
   output->GetCopy()->AllowWrite(false);

   return true;
}

const char *OutputJob::Status(const StatusLine *s)
{
   if(no_status)
      return "";

   /* Never show anything if we havn't even received any data yet; it won't
    * start connecting until then, so it's not interesting. */
   if(!initialized)
      return "";

   /* Use the status from the output CopyJob.  It'll be the one that's connecting
    * to a host, if applicable. */
   return output->Status(s,true);
}

void OutputJob::PutEOF()
{
   /* Make sure we've sent at least one (empty) block.  This ensures
    * that we always start the input->output code path. */
   Put("", 0);

   if(InputPeer())
      InputPeer()->PutEOF();
   eof=true;
}

/* add a filter to the beginning of the list */
void OutputJob::PreFilter(const char *newfilter)
{
   if(!filter)
   {
      SetFilter(newfilter);
      return;
   }

   char *newstr = xasprintf("%s | %s", newfilter, filter);
   SetFilter(newstr);
   printf("new: '%s'\n", newstr);
   xfree(newstr);
}

void OutputJob::SetFilter(const char *newfilter)
{
   xfree(filter);
   filter=xstrdup(newfilter);
}

int OutputJob::GetWidth() const
{
   if(IsFiltered() || output_fd->getfd() != 1)
      return -1;
   return fd_width(1);
}

bool OutputJob::IsTTY() const
{
   if(IsFiltered() || output_fd->getfd() != 1)
      return false;
   return isatty(1);
}

/* Get the input FileCopyPeer; this is the buffer we write to. */
FileCopyPeer *OutputJob::InputPeer() const
{
   return input? input->GetGet():0;
}

/* Get the output FileCopyPeer (the FileCopyPeer that's doing the final output). */
FileCopyPeer *OutputJob::OutputPeer() const
{
   return output? output->GetPut():0;
}

/* We're done if the output is finished, or on error. */
int OutputJob::Done()
{
   if(Error())
      return true;

   /* We're always done if the output breaks, regardless of whether
    * we treat it as an error or not. */
   if(output_fd->broken())
      return true;

   if(!initialized)
      return false;

   if(input && !input->Done())
     return false;
   if(output && !output->Done())
     return false;
   if(output_fd && !output_fd->Done())
     return false;

      return true;
   return false;
   if(eof && input && input->Done() && output && output->Done())
//   if(eof && output && output->Done())
   {
	   printf("xxxxxx\n");
      return true;
   }

   return false;
}

int OutputJob::Do()
{
   if(!fg_data && output_fd && output_fd->GetProcGroup())
   {
      fg_data=new FgData(output_fd->GetProcGroup(),fg);
      return MOVED;
   }

   return STALL;
}

/* Don't register errors until they're actually printed by
 * the sub-job (ie. it's also Done()). */
bool OutputJob::Error()
{
   if(error)
      return true;
   if(input && input->Error() && input->Done())
      error=true;
   if(output && input != output && output->Error() && output->Done())
      error=true;
   if(fail_if_broken && output_fd->broken())
      error=true;
   return error;
}

void OutputJob::Fg()
{
   super::Fg();
   if(input)
      input->Fg();
   if(output && input != output)
      output->Fg();
}

void OutputJob::Bg()
{
   if(output && input != output)
      output->Bg();
   if(input)
      input->Bg();
   super::Bg();
}

void OutputJob::Suspend()
{
   if(input)
      input->Suspend();
   if(output && input != output)
      output->Suspend();
   super::Suspend();
}

void OutputJob::Resume()
{
   if(input)
      input->Resume();
   if(output && input != output)
      output->Resume();
   super::Resume();
}

bool OutputJob::Full()
{
   if(input == 0)
      return false;

   /* It'd be nicer to just check copy->GetGet()->IsSuspended(), since
    * the FileCopy will suspend the Get end if the Put end gets filled.
    * However, it won't do that until it actually tries to send something. */
   int size = 0;
   if(input->GetPut())
      size += input->GetPut()->Buffered();
   if(input->GetGet())
      size += input->GetGet()->Buffered();
   if(input != output)
   {
      if(output->GetPut())
	 size += output->GetPut()->Buffered();
      if(output->GetGet())
	 size += output->GetGet()->Buffered();
   }

   return size >= 0x10000;
}

/* We'll actually go over the buffer limit here; that's OK; it's not a
 * strict value.  (It's not convenient to prevent that completely with
 * Format(), either.) */
void OutputJob::Put(const char *buf,int size)
{
   InitCopy();
   if(!InputPeer())
      return;

   last.SetToCurrentTime();

   int oldpos = InputPeer()->GetPos();
   InputPeer()->Put(buf, size);
   InputPeer()->SetPos(oldpos);
}

void OutputJob::Format(const char *f,...)
{
   InitCopy();
   if(!InputPeer())
      return;

   int oldpos = InputPeer()->GetPos();

   va_list v;
   va_start(v,f);
   InputPeer()->vFormat(f, v);
   va_end(v);

   InputPeer()->SetPos(oldpos);
}

/* Propagate signals down to our child processes. */
int OutputJob::AcceptSig(int sig)
{
   int m=MOVED;
   if(sig == SIGTERM || sig == SIGINT)
      m=WANTDIE;

   /* If we have an input copier right now, it'll contain the top filter
    * (which is linked to all other filters), so send it the signal. */
   if(input)
      m=input->AcceptSig(sig);
   /* Otherwise, the only filters we have running are in output_fd. */
   else
      output_fd->Kill(sig);
   if(sig!=SIGCONT)
      AcceptSig(SIGCONT);
   return m;
}
