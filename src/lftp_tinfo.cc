/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
#else
# error No header file for tigetstr or tgetstr found. Install ncurses-dev or termcap-dev.
#endif
}
#include <stdlib.h>
#include "lftp_tinfo.h"

static bool terminfo_ok = true;
static void init_terminfo()
{
   static bool initted = false;
   if(initted) return;
   initted = true;

#if defined(HAVE_TIGETSTR)
   int errret=0;
   if(setupterm(NULL, 1, &errret) == ERR)
      terminfo_ok = false;
#elif defined(HAVE_TGETSTR)
   static char buf[2048];
   const char *term=getenv("TERM");
   if(!term || tgetent(buf,term) == -1)
      terminfo_ok = false;
#endif
}

const char *get_string_term_cap(const char *terminfo_cap, const char *tcap_cap)
{
   init_terminfo();
   if(!terminfo_ok)
      return 0;

#if defined(HAVE_TIGETSTR)
   /* Cast to work around missing const def in some ncurses installations: */
   const char *ret = tigetstr(const_cast<char *>(terminfo_cap));
   if(ret && ret != (char *)-1) return ret;
#elif defined(HAVE_TGETSTR)
   const char *ret = tgetstr(const_cast<char *>(tcap_cap), 0);
   if(ret && ret != (const char *)-1)
      return ret;
#endif

   return NULL;
}
