/*
 * lftp and utils
 *
 * Copyright (c) 1996-2008 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef URL_H
#define URL_H

#include "xstring.h"

class ParsedURL
{
public:
   xstring_c proto;
   xstring_c user;
   xstring_c pass;
   xstring_c host;
   xstring_c port;
   xstring path;

   xstring_c orig_url;

   ParsedURL(const char *url,bool proto_required=false,bool use_rfc1738=true);
   void parse(const char *url,bool proto_required=false,bool use_rfc1738=true);
   ~ParsedURL();

   // returns allocated memory
   char *Combine(const char *home=0,bool use_rfc1738=true);
};

# define URL_UNSAFE " <>\"'%{}|\\^[]`"
# define URL_PATH_UNSAFE URL_UNSAFE"#;?&+"
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
   static xstring& encode(const char *s,int len,const char *unsafe);
   static xstring& encode(const xstring &s,const char *unsafe) { return encode(s,s.length(),unsafe); }
   static xstring& encode(const char *s,const char *unsafe) { return encode(s,strlen(s),unsafe); }
   // reverse; done in-place; returns length.
   static int decode_string(char *);
   static xstring& decode(const char *);

   static bool is_url(const char *p);
   static int path_index(const char *p);
   static const char *path_ptr(const char *p);
   static bool dir_needs_trailing_slash(const char *proto);
   static bool find_password_pos(const char *url,int *start,int *len);
   static const char *remove_password(const char *url);
   static const char *hide_password(const char *url);
};

#endif//URL_H
