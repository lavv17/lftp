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

#ifndef URL_H
#define URL_H

#include "xmalloc.h"

class ParsedURL
{
   char	 *memory;
public:
   char	 *proto;
   char  *user;
   char	 *pass;
   char  *host;
   char	 *port;
   char  *path;

   ParsedURL(const char *url);
   ~ParsedURL()
   {
      xfree(memory);
   }
};

class url
{
public:
# define URL_UNSAFE " <>\"%{}|\\^[]`\033"
   // encode unsafe chars as %XY
   static char *encode_string(const char *,char *buf=0,const char *u=URL_UNSAFE);
   // reverse; done in-place.
   static void decode_string(char *);
};

#endif//URL_H
