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

#ifndef SSH_ACCESS_H
#define SSH_ACCESS_H

#include "NetAccess.h"
#include "PtyShell.h"

class SSH_Access : public NetAccess
{
protected:
   SMTaskRef<IOBuffer> pty_send_buf;
   SMTaskRef<IOBuffer> pty_recv_buf;
   SMTaskRef<IOBuffer> send_buf;
   SMTaskRef<IOBuffer> recv_buf;
   Ref<PtyShell> ssh;
   int password_sent;
   const char *greeting;
   bool received_greeting;
   bool hostname_valid;

   xstring last_ssh_message;
   time_t last_ssh_message_time;

   void MoveConnectionHere(SSH_Access *o);
   void DisconnectLL();

   void MakePtyBuffers();
   int HandleSSHMessage();
   void LogSSHMessage();   /* it's called after the greeting is received
			    * (or internally from HandleSSHMessage). */

   SSH_Access(const char *g) :
      password_sent(0),
      greeting(g), received_greeting(false),
      hostname_valid(false),
      last_ssh_message_time(0) {}

   SSH_Access(const SSH_Access *o) : NetAccess(o),
      password_sent(0),
      greeting(o->greeting), received_greeting(false),
      hostname_valid(o->hostname_valid),
      last_ssh_message_time(0) {}
};

#endif
