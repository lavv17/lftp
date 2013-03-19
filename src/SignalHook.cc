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
#include "SignalHook.h"

int  *SignalHook::counts=0;
struct sigaction *SignalHook::old_handlers=0;
bool *SignalHook::old_saved=0;

void SignalHook::cnt_handler(int sig)
{
   counts[sig]++;
}

void SignalHook::set_signal(int sig,signal_handler handler)
{
   if(!old_saved[sig])
   {
      sigaction(sig,0,&old_handlers[sig]);
      if(sig==SIGINT && old_handlers[sig].sa_handler==(signal_handler)SIG_IGN)
	 return;
      old_saved[sig]=true;
   }
   struct sigaction act;
   act.sa_handler=handler;
   act.sa_flags=0;
   sigemptyset(&act.sa_mask);
   sigaction(sig,&act,0);
}

void SignalHook::Restore(int i)
{
   if(old_saved[i])
      sigaction(i,&old_handlers[i],0);
   SignalHook::Unblock(i);
}

void SignalHook::RestoreAll()
{
   for(int i=0; i<256; i++)
      Restore(i);
}

void SignalHook::Block(int sig)
{
   sigset_t s;
   sigemptyset(&s);
   sigaddset(&s,sig);
   sigprocmask(SIG_BLOCK,&s,0);
}
void SignalHook::Unblock(int sig)
{
   sigset_t s;
   sigemptyset(&s);
   sigaddset(&s,sig);
   sigprocmask(SIG_UNBLOCK,&s,0);
}

void SignalHook::ClassInit()
{
   if(counts)
      return;
   counts=new int[256];
   old_handlers=new struct sigaction[256];
   old_saved=new bool[256];
   for(int i=0; i<256; i++)
   {
      counts[i]=0;
      old_saved[i]=false;
   }
   Ignore(SIGPIPE);  // want to get EPIPE
#ifdef SIGXFSZ
   Ignore(SIGXFSZ);  // and EFBIG
#endif
}

void SignalHook::Cleanup()
{
   delete [] counts;
   delete [] old_handlers;
   delete [] old_saved;
}
