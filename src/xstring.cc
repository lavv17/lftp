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

/* $Id: xstring.cc,v 1.33 2011/05/10 05:52:34 lav Exp $ */

#include <config.h>
#include <string.h>
#include <mbswidth.h>
#include "xstring.h"
#include "trio.h"
#include "c-ctype.h"

int xstrcmp(const char *s1,const char *s2)
{
   if(s1==s2)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strcmp(s1,s2);
}
int xstrncmp(const char *s1,const char *s2,size_t len)
{
   if(s1==s2 || len==0)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strncmp(s1,s2,len);
}
int xstrcasecmp(const char *s1,const char *s2)
{
   if(s1==s2)
      return 0;
   if(s1==0 || s2==0)
      return 1;
   return strcasecmp(s1,s2);
}
size_t xstrlen(const char *s)
{
   if(s==0)
      return 0;
   return strlen(s);
}

void xstring::get_space(size_t s)
{
   get_space2(s,32);
}
void xstring::get_space2(size_t s,size_t g)
{
   if(!buf)
      buf=(char*)xmalloc(size=s+1);
   else if(size<s+1)
      buf=(char*)xrealloc(buf,size=(s|(g-1))+1);
   else if(size>=g*8 && s+1<=size/2)
      buf=(char*)xrealloc(buf,size/=2);
   buf[s]=0;
}
char *xstring::add_space(size_t s)
{
   if(size<=len+s)
      get_space(len+s);
   return get_non_const()+len;
}

void xstring::init(const char *s,int len)
{
   init();
   nset(s,len);
}
void xstring::init(const char *s)
{
   init();
   set(s);
}

xstring& xstring::nset(const char *s,int len)
{
   if(!s)
   {
      xfree(buf);
      init();
      return *this;
   }
   this->len=len;
   if(s==buf)
      return *this;
   if(s>buf && s<buf+size)
   {
      memmove(buf,s,len);
      get_space(len);
      return *this;
   }
   get_space(len);
   memcpy(buf,s,len);
   return *this;
}
xstring& xstring::set(const char *s)
{
   return nset(s,xstrlen(s));
}

xstring::xstring(const xstring_clonable& c)
{
   init();
   if(!c.buf)
      return;
   len=c.len;
   get_space(c.len);
   memcpy(buf,c.buf,c.len);
}

xstring& xstring::set_allocated(char *s)
{
   if(!s) {
      unset();
      return *this;
   }
   len=strlen(s);
   size=len+1;
   xfree(buf);
   buf=s;
   return *this;
}
xstring& xstring::move_here(xstring& o)
{
   if(!o) {
      unset();
      return *this;
   }
   len=o.len; o.len=0;
   size=o.size; o.size=0;
   xfree(buf);
   buf=o.buf; o.buf=0;
   return *this;
}

xstring& xstring::append(const char *s,size_t s_len)
{
   if(!s || s_len==0)
      return *this;
   get_space(len+s_len);
   memcpy(buf+len,s,s_len);
   len+=s_len;
   return *this;
}
xstring& xstring::append(const char *s)
{
   return append(s,strlen(s));
}
xstring& xstring::append(char c)
{
   get_space(len+1);
   buf[len++]=c;
   return *this;
}
xstring& xstring::append_padding(int len,char c)
{
   memset(add_space(len),c,len);
   add_commit(len);
   return *this;
}

bool xstring::begins_with(const char *o_buf,size_t o_len) const
{
   if(len<o_len)
      return false;
   if(buf==o_buf)
      return true;
   if(!buf || !o_buf)
      return false;
   if(o_len==0)
      return true;
   return !memcmp(buf,o_buf,o_len);
}
bool xstring::ends_with(const char *o_buf,size_t o_len) const
{
   if(len<o_len)
      return false;
   if(buf+len-o_len==o_buf)
      return true;
   if(!buf || !o_buf)
      return false;
   if(o_len==0)
      return true;
   return !memcmp(buf+len-o_len,o_buf,o_len);
}
bool xstring::eq(const char *o_buf,size_t o_len) const
{
   return len==o_len && begins_with(o_buf,o_len);
}

static size_t vstrlen(va_list va0)
{
   va_list va;
   VA_COPY(va,va0);
   size_t len=0;
   for(;;)
   {
      const char *s=va_arg(va,const char *);
      if(!s)
	 break;
      len+=strlen(s);
   }
   va_end(va);
   return len;
}
static void vstrcpy(char *buf,va_list va0)
{
   va_list va;
   VA_COPY(va,va0);
   for(;;)
   {
      const char *s=va_arg(va,const char *);
      if(!s)
	 break;
      size_t s_len=strlen(s);
      memcpy(buf,s,s_len);
      buf+=s_len;
   }
   *buf=0;
   va_end(va);
}

xstring& xstring::vappend(va_list va)
{
   size_t va_len=vstrlen(va);
   get_space(len+va_len);
   vstrcpy(buf+len,va);
   len+=va_len;
   return *this;;
}

xstring& xstring::vappend(...)
{
   va_list va;
   va_start(va,this);
   vappend(va);
   va_end(va);
   return *this;;
}

xstring& xstring::vset(...)
{
   truncate(0);
   va_list va;
   va_start(va,this);
   vappend(va);
   va_end(va);
   return *this;
}

void xstring::truncate(size_t n)
{
   if(n<len)
      set_length(n);
}
void xstring::truncate_at(char c)
{
   if(!buf)
      return;
   char *p=(char*)memchr(buf,c,len);
   if(p)
   {
      *p=0;
      len=p-buf;
   }
}

xstring& xstring::set_substr(int start,size_t sublen,const char *s,size_t s_len)
{
   if(start+sublen>len)
      sublen=len-start;
   if(sublen<s_len)
      get_space(len+s_len-sublen);
   if(sublen!=s_len)
      memmove(buf+start+s_len,buf+start+sublen,len-(start+sublen)+1);
   memcpy(buf+start,s,s_len);
   len+=s_len-sublen;
   return *this;
}
xstring& xstring::set_substr(int start,size_t sublen,const char *s)
{
   return set_substr(start,sublen,s,xstrlen(s));
}

bool xstring::chomp(char c)
{
   if(!len || buf[len-1]!=c)
      return false;
   buf[--len]=0;
   return true;
}
void xstring::rtrim(char c)
{
   while(chomp(c));
}
unsigned xstring::skip_all(unsigned i,char c) const
{
   while(i<len && buf[i]==c)
      i++;
   return i;
}
int xstring::instr(char c) const
{
   char *pos=(char*)memchr(buf,c,len);
   if(!pos)
      return -1;
   return pos-buf;
}

xstring& xstring::vappendf(const char *format, va_list ap)
{
   if(size-len<32 || size-len>512)
      get_space(len+strlen(format)+32);
   for(;;)
   {
      va_list tmp;
      VA_COPY(tmp,ap);
      size_t res=vsnprintf(buf+len, size-len, format, tmp);
      va_end(tmp);
      if(res>=0 && res<size-len)
      {
	 set_length(len+res);
	 return *this;
      }
      get_space(res>size-len ? len+res+1 : len+(size-len)*2);
   }
}
xstring& xstring::setf(const char *format, ...)
{
   va_list va;
   va_start(va, format);
   vsetf(format, va);
   va_end(va);
   return *this;
}
xstring& xstring::appendf(const char *format, ...)
{
   va_list va;
   va_start(va, format);
   vappendf(format, va);
   va_end(va);
   return *this;
}
// don't use it in nested loops
xstring& xstring::get_tmp()
{
   static xstring revolver[16];
   static int i;
   int next=(i+1)&15;
   xstring& tmp=revolver[i];
   // keep the oldest tmp clear to trigger NULL dereference
   tmp.move_here(revolver[next]);
   i=next;
   return tmp;
}
char *xstring::tmp_buf(int n)
{
   xstring& buf=get_tmp();
   buf.get_space(n-1);	// get_space adds a tail byte again
   return buf.get_non_const();
}
xstring& xstring::format(const char *fmt, ...)
{
   va_list va;
   va_start(va,fmt);
   xstring& res=vformat(fmt, va);
   va_end(va);
   return res;
}
xstring &xstring::cat(const char *first,...)
{
   va_list va;
   va_start(va,first);
   xstring& str=get_tmp(first);
   str.vappend(va);
   va_end(va);
   return str;
}
xstring &xstring::join(const char *sep,int n,...)
{
   va_list va;
   va_start(va,n);
   xstring& str=get_tmp();
   str.truncate(0);
   while(n-->0)
   {
      const char *a=va_arg(va,const char*);
      if(!a || !*a)
	 continue;
      if(str.length())
	 str.append(sep);
      str.append(a);
   }
   va_end(va);
   return str;
}

const char *xstring_c::vset(...)
{
   va_list va;
   va_start(va,this);
   size_t va_len=vstrlen(va);
   if(!buf || strlen(buf)<va_len)
      buf=(char*)xrealloc(buf,va_len+1);
   vstrcpy(buf,va);
   va_end(va);
   return buf;
}

bool xstring::is_binary() const
{
   unsigned bin_count=0;
   for(unsigned i=0; i<len; i++)
      bin_count+=((unsigned char)buf[i] < 32);
   return bin_count*32>len;
}
const char *xstring::hexdump_to(xstring& buf) const
{
   int len=length();
   const char *s=get();
   while(len-->0)
      buf.appendf("%02X",(unsigned char)*s++);
   return buf;
}
const char *xstring::hexdump() const
{
   return hexdump_to(get_tmp(""));
}
const char *xstring::dump() const
{
   return dump_to(get_tmp(""));
}
const char *xstring::dump_to(xstring& buf) const
{
   if(is_binary()) {
   binary:
      if(len<1024) {
	 buf.append("<binary:");
	 hexdump_to(buf);
	 buf.append('>');
      } else {
	 buf.appendf("<long binary, %d bytes>",(int)length());
      }
   } else {
      int old_buf_len=buf.length();
      int len=length();
      const char *s=get();
      size_t invalid=0;
      while(len>0) {
	 int ch_len=mblen(s,len);
	 int ch_width=-1;
	 if(ch_len<1) {
	    ch_len=1;
	 } else {
	    ch_width=mbsnwidth(s,ch_len,0);
	 }
	 if(ch_width>=0) {
	    buf.append(s,ch_len);
	 } else {
	    while(ch_len>0) {
	       buf.appendf("\\%03o",(unsigned char)*s++);
	       ch_len--;
	       len--;
	       invalid++;
	    }
	 }
	 s+=ch_len;
	 len-=ch_len;
      }
      if(invalid*32>length()) {
	 buf.truncate(old_buf_len);
	 goto binary;
      }
   }
   return buf;
}

int xstring0::_url_decode(size_t len)
{
   if(!buf)
      return 0;
   char *store=buf;
   const char *p=buf;
   int rest=len;
   while(rest>0)
   {
      if(rest>=3 && *p=='%' && c_isxdigit(p[1]) && c_isxdigit(p[2]))
      {
	 int n;
	 if(sscanf(p+1,"%2x",&n)==1)
	 {
	    *store++=n;
	    p+=3;
	    rest-=3;
	    continue;
	 }
      }
      else if(*p=='+')
      {
	 *store++=' ';
	 p++;
	 rest--;
	 continue;
      }
      *store++=*p++;
      rest--;
   }
   return store-buf;
}
int xstring0::_hex_decode(size_t len)
{
   if(!buf)
      return 0;
   char *store=buf;
   const char *p=store;
   int rest=len;
   while(rest>=2)
   {
      int n;
      if(!c_isxdigit(p[0]) || !c_isxdigit(p[1])
      || sscanf(p,"%2x",&n)!=1)
	 break;
      *store++=n;
      p+=2;
      rest-=2;
   }
   return store-buf;
}

xstring& xstring::url_decode()
{
   set_length(_url_decode(length()));
   return *this;
}
xstring_c& xstring_c::url_decode()
{
   set_length(_url_decode(length()));
   return *this;
}

xstring& xstring::hex_decode()
{
   set_length(_hex_decode(length()));
   return *this;
}

/* Encode the unsafe characters in a given string, producing %XX encoded string. */
#define need_quote(c) (c_iscntrl((c)) || !c_isascii((c)) || strchr(unsafe,(c)))
xstring& xstring::append_url_encoded(const char *s,int len,const char *unsafe)
{
   if(!s)
      return *this;
   add_space(len+len/4);
   while(len-->0)
   {
      char c=*s++;
      if (need_quote(c))
	 appendf("%%%02X",(unsigned char)c);
      else
	 append(c);
   }
   return *this;
}

xstring xstring::null;
