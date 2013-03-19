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

#ifndef SYSCMDJOB_H
#define SYSCMDJOB_H

#include "trio.h"
#include "Job.h"
#include "ProcWait.h"

class SysCmdJob : public Job
{
   xstring cmd;
   SMTaskRef<ProcWait> w;
   void PrepareToDie();
public:
   SysCmdJob(const char *new_cmd);
   ~SysCmdJob();
   int Do();
   int Done() { return(w && w->GetState()!=w->RUNNING); }
   int AcceptSig(int);
   int ExitCode() { return w?w->GetInfo()>>8:1; }
};

#endif//SYSCMDJOB_H
