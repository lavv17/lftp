/* modechange.h -- definitions for file mode manipulation

   Copyright (C) 1989, 1990, 1997, 2003, 2004, 2005, 2006 Free
   Software Foundation, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

/* Masks for the `flags' field in a `struct mode_change'. */

#if ! defined MODECHANGE_H_
# define MODECHANGE_H_

# include <stdbool.h>
# include <sys/types.h>

/* Special operations flags.  */
enum
  {
    /* For the sentinel at the end of the mode changes array.  */
    MODE_DONE,

    /* The typical case.  */
    MODE_ORDINARY_CHANGE,

    /* In addition to the typical case, affect the execute bits if at
       least one execute bit is set already, or if the file is a
       directory.  */
    MODE_X_IF_ANY_X,

    /* Instead of the typical case, copy some existing permissions for
       u, g, or o onto the other two.  Which of u, g, or o is copied
       is determined by which bits are set in the `value' field.  */
    MODE_COPY_EXISTING
  };

/* Description of a mode change.  */
struct mode_change
{
  char op;			/* One of "=+-".  */
  char flag;			/* Special operations flag.  */
  mode_t affected;		/* Set for u, g, o, or a.  */
  mode_t value;			/* Bits to add/remove.  */
  mode_t mentioned;		/* Bits explicitly mentioned.  */
};

struct mode_change *mode_compile (const char *);
struct mode_change *mode_create_from_ref (const char *);
mode_t mode_adjust (mode_t, bool, mode_t, struct mode_change const *,
		    mode_t *);

#endif
