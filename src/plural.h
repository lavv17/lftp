/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef NUMERIC_H
#define NUMERIC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef FORMAT_ARG
# ifdef __GNUC__
#  define FORMAT_ARG(n) __attribute__((format_arg(n)))
# else
#  define FORMAT_ARG(n)
# endif
#endif

const char *plural(const char *format,...) FORMAT_ARG(1);

#ifdef __cplusplus
}
#endif

#endif /* NUMERIC_H */
