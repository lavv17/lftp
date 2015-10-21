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

#ifndef PROCWAIT_H
#define PROCWAIT_H

#include <sys/types.h>
#include <signal.h>
#include "SMTask.h"
#include "xmap.h"

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
   static xmap<ProcWait*> all_proc;
   static const xstring& proc_key(pid_t p); // make key for xmap

   const pid_t pid;
   State status;
   int	 term_info;
   int	 saved_errno;
   bool  auto_die;

   bool  handle_info(int info); // true if finished

   ~ProcWait();

public:
   int	 Do();
   State GetState() { return status; }
   int	 GetInfo() { return term_info; }
   int	 Kill(int sig=SIGTERM);

   void Auto() { auto_die=true; }

   ProcWait(pid_t p);

   static void Signal(bool yes);

   static void DeleteAll();
};

#endif /* PROCWAIT_H */
