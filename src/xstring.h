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
#include <unistd.h>

#define string_alloca(len)      ((char*)alloca((len)))

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

enum { URL_DECODE_PLUS=1, URL_ALLOW_8BIT=2 };

class xstring0 // base class
{
protected:
   char *buf;
   xstring0() {}
   xstring0(const xstring0&); // disable cloning

   int _url_decode(size_t len,int flags);
   int _hex_decode(size_t len);
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
   template<class STR> const char *move_here(STR& s) { return set_allocated(s.borrow()); }
   const char *vset(...) ATTRIBUTE_SENTINEL;
   void truncate(size_t n=0) { if(buf) buf[n]=0; }
   char *borrow() { return replace_value(buf,(char*)0); }
   bool begins_with(const char *s) const { return !strncmp(buf,s,strlen(s)); };
   bool prefixes(const char *s) const { return !strncmp(buf,s,length()); };
   bool eq(const char *s) const { return !xstrcmp(buf,s); }
   bool ne(const char *s) const { return !eq(s); }
   bool eq_nc(const char *s) const { return !xstrcasecmp(buf,s); }
   size_t length() const { return xstrlen(buf); }
   void set_length(size_t n) { if(buf) buf[n]=0; }
   char last_char() const { size_t len=length(); return len>0?buf[len-1]:0; }

   void unset() { xfree(buf); buf=0; }
   void _set(const char *s) { buf=xstrdup(s); }
   char **buf_ptr() { unset(); return &buf; }

   xstring_c& url_decode(int flags=0);

   static const xstring_c null;
};
class xstring_ca : public xstring_c
{
   xstring_ca& operator=(const xstring_ca& s); // disable assigning
   xstring_ca(const xstring_ca&); // disable cloning
public:
   xstring_ca(char *s) { buf=s; }
};

class xstring_clonable;
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
   const char *operator=(const xstring& s) { return set(s); }
   xstring(const xstring&); // disable cloning

public:
   xstring() { init(); }
   xstring(const char *s) { init(s); }
   xstring(const char *s,int l) { init(s,l); }

   // explicit cloning
   xstring(const xstring_clonable& c);
   const xstring_clonable& copy() const { return *(xstring_clonable*)(this); }

   void get_space(size_t s);
   void get_space2(size_t s,size_t g);
   void shrink_space();
   char *add_space(size_t s);
   void add_commit(int new_len) { len+=new_len; }

   size_t length() const { return len; }

   xstring& set(const xstring &s) { return nset(s,s.length()); }
   xstring& set(const char *s);
   xstring& nset(const char *s,int len);
   xstring& set_allocated(char *s);
   xstring& move_here(xstring&);
   xstring& move_here(xstring_c& s) { return set_allocated(s.borrow()); }
   void swap(xstring& o);

   xstring& set_substr(int start,size_t sublen,const char *,size_t);
   xstring& set_substr(int start,size_t sublen,const char *);
   xstring& set_substr(int start,size_t sublen,const xstring &s) { return set_substr(start,sublen,s.get(),s.length()); }
   xstring& prepend(const char *s,size_t len) { return set_substr(0,0,s,len); }
   xstring& prepend(const xstring &s) { return prepend(s.get(),s.length()); }
   xstring& prepend(char c) { return prepend(&c,1); }

   xstring& append(const char *s);
   xstring& append(char c);
   xstring& append(const char *s,size_t len);
   xstring& append(const xstring &s) { return append(s.get(),s.length()); }
   xstring& append_padding(int len,char ch);
   xstring& vappend(va_list);
   xstring& vappend(...) ATTRIBUTE_SENTINEL;
   xstring& vset(...) ATTRIBUTE_SENTINEL;
   xstring& vsetf(const char *fmt, va_list ap) { truncate(0); return vappendf(fmt,ap); }
   xstring& setf(const char *fmt,...) PRINTF_LIKE(2,3);
   xstring& vappendf(const char *fmt, va_list ap);
   xstring& appendf(const char *fmt,...) PRINTF_LIKE(2,3);
   static xstring& get_tmp();
   static xstring& get_tmp(const char *s) { return get_tmp().set(s); }
   static xstring& get_tmp(const char *s,int n) { return get_tmp().nset(s,n); }
   static char *tmp_buf(int n);
   static xstring& vformat(const char *fmt,va_list ap) { return get_tmp().vsetf(fmt,ap); }
   static xstring& format(const char *fmt,...) PRINTF_LIKE(1,2);
   static xstring& cat(const char *first,...) ATTRIBUTE_SENTINEL;
   static xstring& join(const char *sep,int n,...);

   void truncate() { set_length(0); }
   void truncate(size_t n);
   void truncate_at(char c);
   /* set_length can be used to extend the string, e.g. after modification
      with get_space+get_non_const. */
   void set_length(size_t n) { if(buf) buf[len=n]=0; }
   void set_length_no_z(size_t n) { len=n; }
   char *borrow() { size=len=0; return replace_value(buf,(char*)0); }
   bool begins_with(const char *o_buf,size_t o_len) const;
   bool begins_with(const char *s) const { return begins_with(s,strlen(s)); };
   bool ends_with(const char *o_buf,size_t o_len) const;
   bool ends_with(const char *s) const { return ends_with(s,strlen(s)); };
   bool prefixes(const char *s) const { return !strncmp(buf,s,length()); };
   bool eq(const char *o_buf,size_t o_len) const;
   bool eq(const char *s) const { return eq(s,strlen(s)); }
   bool eq(const xstring&o) const { return eq(o.get(),o.length()); }
   bool ne(const xstring&o) const { return !eq(o); }
   int cmp(const char *o_buf,size_t o_len) const;
   int cmp(const xstring&o) const { return cmp(o.get(),o.length()); }
   bool eq_nc(const char *o_buf,size_t o_len) const;
   bool eq_nc(const char *s) const { return eq_nc(s,strlen(s)); }
   bool eq_nc(const xstring&o) const { return eq_nc(o.get(),o.length()); }
   bool chomp(char c='\n');
   void rtrim(char c=' ');
   char last_char() const { return len>0?buf[len-1]:0; }
   unsigned skip_all(unsigned i,char c) const;
   int instr(char c) const;

   void _clear() { init(); }
   void _set(const char *s) { init(s); }
   void unset() { xfree(buf); _clear(); }

   bool is_binary() const;
   const char *dump_to(xstring &out) const;
   const char *dump() const;
   const char *hexdump_to(xstring &out) const;
   const char *hexdump() const;

   xstring& url_decode(int flags=0);
   xstring& append_url_encoded(const char *s,int len,const char *unsafe,unsigned flags=0);
   xstring& append_url_encoded(const char *s,const char *unsafe,unsigned flags=0) { return append_url_encoded(s,strlen(s),unsafe,flags); }
   xstring& append_url_encoded(const xstring& s,const char *unsafe,unsigned flags=0) { return append_url_encoded(s,s.length(),unsafe,flags); }

   xstring& append_quoted(const char *s,int len);
   xstring& append_quoted(const char *s) { return append_quoted(s,strlen(s)); }

   xstring& hex_decode();

   xstring& c_lc();
   xstring& c_ucfirst();

   static const xstring null;
};
class xstring_clonable : public xstring {};

static inline size_t strlen(const xstring& s) { return s.length(); }
static inline size_t xstrlen(const xstring& s) { return s.length(); }

#endif//XSTRING_H
