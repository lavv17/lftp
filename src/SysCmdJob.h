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

#ifndef SYSCMDJOB_H
#define SYSCMDJOB_H

#include "trio.h"
#include "Job.h"
#include "ProcWait.h"

class SysCmdJob : public Job
{
   char *cmd;
   ProcWait *w;
public:
   SysCmdJob(const char *new_cmd);
   ~SysCmdJob();
   int Do();
   int Done() { return(w && w->GetState()!=w->RUNNING); }
   int AcceptSig(int);
};

#endif//SYSCMDJOB_H
