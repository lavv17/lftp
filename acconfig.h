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

#ifndef CONFIG_H
#define CONFIG_H
@TOP@

#undef HAVE_HSTRERROR_DECL
#undef HAVE_H_ERRLIST_DECL
#undef HAVE_H_ERRNO_DECL
#undef HAVE_INET_ATON_DECL
#undef HAVE_RANDOM_DECL
#undef HAVE_RES_SEARCH_DECL
#undef HAVE_STRCASECMP_DECL
#undef HAVE_STRPTIME_DECL
#undef HAVE_SYS_ERRLIST_DECL
#undef HAVE_UNSETENV_DECL
#undef HAVE_VSNPRINTF_DECL

@BOTTOM@

#include <post-config.h>

#endif /* CONFIG_H */
