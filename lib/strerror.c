/* A replacement version of strerror

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#if defined (HAVE_SYS_ERRLIST) && !defined (HAVE_SYS_ERRLIST_DECL)
extern int sys_nerrs;
extern char *sys_errlist[];
#endif

/* Return a string describing the system error code ERR.  The returned value
   may be in a static buffer (and in any case shouldn't be written to).  */
char *
strerror (int err)
{
#ifdef HAVE_SYS_ERRLIST
  if (err >= 0 && err < sys_nerrs && sys_errlist[err])
    return sys_errlist[err];
  else
#endif
    {
      static char buf[100];
      sprintf (buf, "Error %d", err);
      return buf;
    }
}
