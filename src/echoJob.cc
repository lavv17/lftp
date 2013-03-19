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

#include <config.h>

#include "echoJob.h"
#include "misc.h"

#define super Job

echoJob::echoJob(const char *buf, int len, OutputJob *_output)
   : output(_output)
{
   AddWaiting(_output);
   output->Put(buf, len);
   output->PutEOF();
}

echoJob::echoJob(const char *buf, OutputJob *_output)
   : output(_output)
{
   AddWaiting(_output);
   output->Put(buf);
   output->PutEOF();
}

echoJob::~echoJob()
{
}

int echoJob::Done()
{
   return output->Done();
}

int echoJob::ExitCode()
{
   /* if the output fails, we failed */
   return output->Error()? 1:0;
}

/* We have no interesting status for "jobs", so we have no PrintStatus
 * override.  (The output job will print the output status, if relevant.) */

void  echoJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Done())
      return;

   /* Never call output->ShowStatusLine unless we're really going
    * to display something. */
   const char *stat = output->Status(s);
   if(*stat && output->ShowStatusLine(s))
      s->Show("echo: %s", stat);
}

void echoJob::Fg()
{
   super::Fg();
   if(output)
      output->Fg();
}
void echoJob::Bg()
{
   if(output)
      output->Bg();
   super::Bg();
}
