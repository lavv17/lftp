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

#ifndef SIGNALHOOK_H
#define SIGNALHOOK_H

#include <signal.h>

typedef void (*signal_handler)(int);

class SignalHook
{
   static int *counts;
   static struct sigaction *old_handlers;
   static bool *old_saved;

   static void cnt_handler(int sig);
   static void set_signal(int sig,signal_handler handler);
public:
   static void DoCount(int sig) { set_signal(sig,&SignalHook::cnt_handler); }
   static int GetCount(int sig) { return counts[sig]; }
   static void ResetCount(int sig) { counts[sig]=0; }
   static void IncreaseCount(int sig) { counts[sig]++; }
   static void Handle(int sig,void (*h)(int)) { set_signal(sig,h); }
   static void Ignore(int sig)  { set_signal(sig,(signal_handler)SIG_IGN); }
   static void Default(int sig) { set_signal(sig,(signal_handler)SIG_DFL); }
   static void Restore(int sig);
   static void Block(int sig);
   static void Unblock(int sig);
   static void RestoreAll();

   static void ClassInit();
   static void Cleanup();
};

#endif//SIGNALHOOK_H
