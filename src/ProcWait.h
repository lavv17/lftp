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

/* $Id$ */

#ifndef PROCWAIT_H
#define PROCWAIT_H

#include <sys/types.h>
#include <signal.h>
#include "SMTask.h"

class ProcWait : public SMTask
{
public:
   enum	State
   {
      TERMINATED,
      RUNNING,
      ERROR
   };

protected:
   static ProcWait *chain;
   ProcWait *next;

   pid_t pid;
   State status;
   int	 term_info;
   int	 saved_errno;
   bool  auto_die;

   bool  handle_info(int info); // true if finished

public:
   int	 Do();
   int	 GetState() { return status; }
   int	 GetErrno() { return saved_errno; }
   int	 GetInfo() { return term_info; }
   int	 Kill(int sig=SIGTERM);

   void Auto() { auto_die=true; }

   ProcWait(pid_t p);
   ~ProcWait();

   static void SIGCHLD_handler(int);
};

#endif /* PROCWAIT_H */
