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

#ifndef MVJOB_H
#define MVJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "ArgV.h"
#include <stdio.h>

class mvJob : public SessionJob
{
   int	 failed;
public:
   int	 Do();
   int	 Done() { return session->IsClosed(); }
   int	 ExitCode() { return failed; }

   void	 PrintStatus(int);
   void	 ShowRunStatus(StatusLine *);
   void	 SayFinal();

   mvJob(FileAccess *session,const char *from,const char *to);
};

#endif // MVJOB_H
