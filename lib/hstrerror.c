/* A replacement version of hstrerror

   Copyright (C) 1996 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <netdb.h>

#if defined (HAVE_H_ERRLIST) && !defined (HAVE_H_ERRLIST_DECL)
extern int h_nerrs;
extern char *h_errlist[];
#endif

/* Return a string describing the host lookup error code HERR.  The returned
   value may be in a static buffer (and in any case shouldn't be written to).  */
const char *
hstrerror (herr)
  int herr;
{
#ifdef HAVE_H_ERRLIST
  if (herr >= 0 && herr < h_nerrs && h_errlist[herr])
    return h_errlist[herr];
  else
#endif
    {
      static char buf[100];
      sprintf (buf, "Host lookup error %d", herr);
      return buf;
    }
}
