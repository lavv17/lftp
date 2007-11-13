/*
 * lftp and utils
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef XSTRING_H
#define XSTRING_H

/* Declare string and memory handling routines.  Take care that an ANSI
   string.h and pre-ANSI memory.h might conflict, and that memory.h and
   strings.h conflict on some systems.  */

#if STDC_HEADERS || HAVE_STRING_H
# include <string.h>
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
#else
# include <strings.h>
# ifndef strchr
#  define strchr index
# endif
# ifndef strrchr
#  define strrchr rindex
# endif
# ifndef memcpy
#  define memcpy(Dst, Src, Num) bcopy (Src, Dst, Num)
# endif
# ifndef memcmp
#  define memcmp(Src1, Src2, Num) bcmp (Src1, Src2, Num)
# endif
#endif

#include <stdarg.h>

#if !HAVE_DECL_STRCASECMP
CDECL int strcasecmp(const char *s1,const char *s2);
CDECL int strncasecmp(const char *s1,const char *s2,size_t n);
#endif

#if !HAVE_DECL_VSNPRINTF
CDECL int vsnprintf(char *,size_t,const char *,va_list);
#endif
#if !HAVE_DECL_SNPRINTF
CDECL int snprintf(char *,size_t,const char *,...);
#endif

static inline int xstrcmp(const char *s1,const char *s2)
{
   if(s1==s2)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strcmp(s1,s2);
}
static inline int xstrncmp(const char *s1,const char *s2,size_t len)
{
   if(s1==s2 || len==0)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strncmp(s1,s2,len);
}
static inline int xstrcasecmp(const char *s1,const char *s2)
{
   if(s1==s2)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strcasecmp(s1,s2);
}
static inline size_t xstrlen(const char *s)
{
   if(s==0)
      return 0;
   return strlen(s);
}

#include <stdarg.h>
#include "xmalloc.h"

/* this is a small and fast dynamic string class */
/* mostly used as xstrdup/xfree replacement */

class xstring0 // base class
{
protected:
   char *buf;
   xstring0() {}
   xstring0(const xstring0&); // disable cloning
public:
   ~xstring0() { xfree(buf); }
   operator const char *() const { return buf; }
   const char *get() const { return buf; }
   char *get_non_const() { return buf; }

   void _clear() { buf=0; }
};

// compact variant
class xstring_c : public xstring0
{
   // make xstring_c = xstrdup() fail:
   xstring_c& operator=(char *);
   const char *operator=(const char *s);
   const char *operator=(const xstring_c& s);
   xstring_c(const xstring_c&); // disable cloning

public:
   xstring_c() { buf=0; }
   xstring_c(const char *s) { _set(s); }
   const char *set(const char *s) { return xstrset(buf,s); }
   const char *nset(const char *s,int n) { return xstrset(buf,s,n); }
   const char *set_allocated(char *s) { xfree(buf); return buf=s; }
   const char *vset(...) __attribute__((sentinel));
   void truncate(size_t n) { if(buf) buf[n]=0; }
   char *borrow() { return replace_value(buf,(char*)0); }
   bool eq(const char *s) { return !xstrcmp(buf,s); }
   bool ne(const char *s) { return !eq(s); }
   size_t length() const { return xstrlen(buf); }

   void unset() { xfree(buf); buf=0; }
   void _set(const char *s) { buf=xstrdup(s); }
};
class xstring_ca : public xstring_c
{
   xstring_ca& operator=(const xstring_ca& s); // disable assigning
   xstring_ca(const xstring_ca&); // disable cloning
public:
   xstring_ca(char *s) { buf=s; }
};

// full implementation
class xstring : public xstring0
{
   size_t size;
   size_t len;

   void init() { buf=0; size=len=0; }
   void init(const char *s,int l);
   void init(const char *s);

   // make xstring = xstrdup() fail:
   xstring& operator=(char *);
   const char *operator=(const char *s) { return set(s); }
   const char *operator=(const xstring& s) { return set(s.get()); }
   xstring(const xstring&); // disable cloning

public:
   xstring() { init(); }
   xstring(const char *s) { init(s); }

   // allocates s bytes, with preferred granularity g
   void get_space(size_t s,size_t g=32);

   size_t length() const { return len; }

   xstring& set(const xstring &s) { return nset(s,s.length()); }
   xstring& set(const char *s);
   xstring& nset(const char *s,int len);
   xstring& set_allocated(char *s);
   xstring& set_substr(int start,size_t sublen,const char *,size_t);
   xstring& set_substr(int start,size_t sublen,const char *);
   xstring& set_substr(int start,size_t sublen,const xstring &s) { return set_substr(start,sublen,s.get(),s.length()); }

   xstring& append(const char *s);
   xstring& append(char c);
   xstring& append(const char *s,size_t len);
   xstring& append(const xstring &s) { return append(s.get(),s.length()); }
   xstring& vappend(va_list);
   xstring& vappend(...) __attribute__((sentinel));
   xstring& vset(...) __attribute__((sentinel));
   xstring& vsetf(const char *fmt, va_list ap) { truncate(0); return vappendf(fmt,ap); }
   xstring& setf(const char *fmt,...) PRINTF_LIKE(2,3);
   xstring& vappendf(const char *fmt, va_list ap);
   xstring& appendf(const char *fmt,...) PRINTF_LIKE(2,3);
   static xstring& get_tmp();
   static xstring& get_tmp(const char *s) { return get_tmp().set(s); }
   static xstring& get_tmp(const char *s,int n) { return get_tmp().nset(s,n); }
   static xstring& vformat(const char *fmt,va_list ap) { return get_tmp().vsetf(fmt,ap); }
   static xstring& format(const char *fmt,...) PRINTF_LIKE(1,2);
   static xstring& cat(const char *first,...) __attribute__((sentinel));
   static xstring& join(const char *sep,int n,...);

   void truncate(size_t n);
   void truncate_at(char c);
   /* set_length can be used to extend the string, e.g. after modification
      with get_space+get_non_const. */
   void set_length(size_t n) { if(buf) buf[len=n]=0; }
   char *borrow() { size=len=0; return replace_value(buf,(char*)0); }
   bool eq(const xstring&o) { return len==o.len && (buf==o.buf || (len>0 && !memcmp(buf,o.buf,len))); }
   bool ne(const xstring&o) { return !eq(o); }
   bool chomp(char c='\n');
   void rtrim(char c=' ');
   char last_char() { return len>0?buf[len-1]:0; }

   void _clear() { init(); }
   void _set(const char *s) { init(s); }
   void unset() { xfree(buf); _clear(); }
};

#endif//XSTRING_H
