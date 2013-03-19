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

#ifndef ASCII_CTYPE_H
#define ASCII_CTYPE_H

// We need these functions because network protocols should not depend
// on current locale.

#include "c-ctype.h"

#define is_ascii_digit(c)  c_isdigit((c))
#define is_ascii_xdigit(c) c_isxdigit((c))
#define is_ascii_space(c)  c_isspace((c))
#define is_ascii_lower(c)  c_islower((c))
#define is_ascii_upper(c)  c_isupper((c))
#define is_ascii_alpha(c)  c_isalpha((c))
#define is_ascii_alnum(c)  c_isalnum((c))
#define to_ascii_lower(c)  c_tolower((c))
#define to_ascii_upper(c)  c_toupper((c))

#endif//ASCII_CTYPE_H
