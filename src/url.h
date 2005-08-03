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
   char	 *user;
   char	 *pass;
   char	 *host;
   char	 *port;
   char	 *path;

   char  *orig_url;

   ParsedURL(const char *url,bool proto_required=false,bool use_rfc1738=true);
   ~ParsedURL()
   {
      xfree(memory);
      xfree(orig_url);
   }

   // returns allocated memory
   char *Combine(const char *home=0,bool use_rfc1738=true);
};

# define URL_UNSAFE " <>\"%{}|\\^[]`"
# define URL_PATH_UNSAFE URL_UNSAFE"#;?"
# define URL_HOST_UNSAFE URL_UNSAFE":/"
# define URL_PORT_UNSAFE URL_UNSAFE"/"
# define URL_USER_UNSAFE URL_UNSAFE"/:@"
# define URL_PASS_UNSAFE URL_UNSAFE"/:@"
class url
{
public:
   char	 *proto;
   char  *user;
   char	 *pass;
   char  *host;
   char	 *port;
   char  *path;

   url(const char *u);
   url();
   ~url();

   void SetPath(const char *p,const char *q=URL_PATH_UNSAFE);
   char *Combine();

   // encode unsafe chars as %XY
   static char *encode_string(const char *,char *buf=0,const char *u=URL_UNSAFE);
   // reverse; done in-place.
   static void decode_string(char *);

   static bool is_url(const char *p)
      {
	 ParsedURL url(p,true);
	 return url.proto!=0;
      }

   static int path_index(const char *p);
   static bool dir_needs_trailing_slash(const char *proto);
};

#endif//URL_H
