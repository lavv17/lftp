/*
 * lftp and utils
 *
 * Copyright (c) 2002-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: lftp_tinfo.cc,v 1.7 2008/11/27 05:56:39 lav Exp $ */

#include <config.h>

extern "C" {
#if defined(HAVE_CURSES_H)
# include <curses.h>
# if defined(HAVE_TERM_H)
#  include <term.h>
# elif defined(HAVE_NCURSES_TERM_H)
#  include <ncurses/term.h>
# endif
#elif defined(HAVE_NCURSES_CURSES_H)
# include <ncurses/curses.h>
# if defined(HAVE_NCURSES_TERM_H)
#  include <ncurses/term.h>
# elif defined(HAVE_TERM_H)
#  include <term.h>
# endif
#elif defined(HAVE_TERMCAP_H)
# include <termcap.h>
#endif
}

#include "lftp_tinfo.h"

#if defined(HAVE_TIGETSTR)
static bool terminfo_ok = true;
static void init_terminfo()
{
   static bool initted = false;
   if(initted) return;
   initted = true;

   int errret=0;
   if(setupterm(NULL, 1, &errret) == ERR)
      terminfo_ok = false;
}
#endif

const char *get_string_term_cap(const char *terminfo_cap, const char *tcap_cap)
{
#if defined(HAVE_TIGETSTR)
   init_terminfo();
   if(terminfo_ok) {
      /* Cast to work around missing const def in some ncurses installations: */
      const char *ret = tigetstr(const_cast<char *>(terminfo_cap));
      if(ret && ret != (char *)-1) return ret;
   }
#elif defined(HAVE_TGETSTR)
   const char *ret = tgetstr(const_cast<char *>(tcap_cap), 0);
   if(ret && ret != (const char *)-1)
      return ret;
#endif

   return NULL;
}
