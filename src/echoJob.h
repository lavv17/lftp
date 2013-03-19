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

#ifndef ECHOJOB_H
#define ECHOJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "OutputJob.h"

class echoJob : public Job
{
   JobRef<OutputJob> output;

public:
   int	 Do() { return STALL; }
   int	 Done();
   int	 ExitCode();

   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);

   echoJob(const char *buf, OutputJob *output);
   echoJob(const char *buf, int len, OutputJob *output);
   ~echoJob();

   void Fg();
   void Bg();
};

#endif // ECHOJOB_H
