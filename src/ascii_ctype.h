/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#ifndef ASCII_CTYPE_H
#define ASCII_CTYPE_H

// We need these functions because network protocols should not depend
// on current locale.

static inline bool is_ascii_digit(char ch)
{
   return ch>='0' && ch<='9';
}

static inline bool is_ascii_space(char ch)
{
   return ch==' ' || ch=='\t' || ch=='\n' || ch=='\r' || ch=='\014';
}

static inline bool is_ascii_lower(char ch)
{
   return ch>='a' && ch<='z';
}

static inline bool is_ascii_upper(char ch)
{
   return ch>='A' && ch<='Z';
}

static inline bool is_ascii_alpha(char ch)
{
   return is_ascii_lower(ch) || is_ascii_upper(ch);
}

static inline bool is_ascii_alnum(char ch)
{
   return is_ascii_alpha(ch) || is_ascii_digit(ch);
}

#endif//ASCII_CTYPE_H
