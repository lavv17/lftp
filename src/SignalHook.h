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

#ifndef SIGNALHOOK_H
#define SIGNALHOOK_H

#include <signal.h>

class SignalHook
{
   static int counts[256];
   static struct sigaction old_handlers[256];
   static bool old_saved[256];

   static void cnt_handler(int sig);
   static void set_signal(int sig,void (*handler)(int));
public:
   static void DoCount(int sig) { set_signal(sig,cnt_handler); }
   static int GetCount(int sig) { return counts[sig]; }
   static void ResetCount(int sig) { counts[sig]=0; }
   static void IncreaseCount(int sig) { counts[sig]++; }
   static void Handle(int sig,void (*h)(int)) { set_signal(sig,h); }
   static void Ignore(int sig)  { set_signal(sig,(void (*)(int))SIG_IGN); }
   static void Default(int sig) { set_signal(sig,(void (*)(int))SIG_DFL); }
   static void Restore(int sig);
   static void Block(int sig);
   static void Unblock(int sig);
   static void RestoreAll();
};

#endif//SIGNALHOOK_H
