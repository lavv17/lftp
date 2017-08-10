/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

   ParsedURL() {}
   ParsedURL(const char *url,bool proto_required=false,bool use_rfc1738=true);
   void parse(const char *url,bool proto_required=false,bool use_rfc1738=true);
   ~ParsedURL();

   xstring& CombineTo(xstring &buf,const char *home=0,bool use_rfc1738=true) const;
   const char *CombineTo(xstring_c &buf,const char *home=0,bool use_rfc1738=true) const {
      xstring tmp; tmp.move_here(buf);
      return buf.move_here(CombineTo(tmp,home,use_rfc1738));
   }
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
   static const xstring& encode(const char *s,int len,const char *unsafe,unsigned flags=0);
   static const xstring& encode(const xstring &s,const char *unsafe,unsigned flags=0) { return encode(s,s.length(),unsafe,flags); }
   static const xstring& encode(const char *s,const char *unsafe,unsigned flags=0) { return encode(s,strlen(s),unsafe,flags); }
   static const xstring& decode(const char *) __attribute__((warn_unused_result));

   static bool is_url(const char *p);
   static int path_index(const char *p);
   static const char *path_ptr(const char *p);
   static bool dir_needs_trailing_slash(const char *proto);
   static bool find_password_pos(const char *url,int *start,int *len);
   static const char *remove_password(const char *url);
   static const char *hide_password(const char *url);
};

#endif//URL_H
