/* Copyright (C) 2006 Free Software Foundation, Inc.
   Written by Derek Price & Paul Eggert.
   This file is part of gnulib.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU Lesser General Public License as published by
   the Free Software Foundation; either version 2.1, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program; if not, write to the Free Software Foundation,
   Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.  */

#ifndef INTTYPES_H
#define INTTYPES_H

/*
 * A wrapper for the ISO C 99 <inttypes.h>.
 * <http://www.opengroup.org/susv3xbd/inttypes.h.html>
 *
 * Currently, if the system <inttypes.h> is missing or not C99 compliant, then
 * this header may only provide the required <stdint.h> (which may be the
 * *almost* C99 compliant one from GNULIB) and prototypes for the strtoimax and
 * strtoumax functions.
 */

#if HAVE_INTTYPES_H
# if HAVE_INCLUDE_NEXT
#  include_next <inttypes.h>
# else
#  include ABSOLUTE_INTTYPES_H
# endif
#endif
#include <stdint.h>

#if !HAVE_DECL_STRTOIMAX
intmax_t strtoimax (const char *, char **, int);
#endif
#if !HAVE_DECL_STRTOUMAX
uintmax_t strtoumax (const char *, char **, int);
#endif

#endif /* INTTYPES_H */
