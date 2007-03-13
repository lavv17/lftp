/*
 * lftp and utils
 *
 * Copyright (c) 2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include <string.h>
#include "xstring.h"

void xstring::get_space(size_t s)
{
   if(!buf)
      buf=(char*)xmalloc(size=s);
   else if(size<s)
      buf=(char*)realloc(buf,size=(s+31)&~31);
   else if(size>=128 && s<=size/2)
      buf=(char*)realloc(buf,size/=2);
}

void xstring::init(const char *s)
{
   if(!s)
   {
      buf=0;
      size=0;
      return;
   }
   buf=(char*)xmalloc(size=strlen(s)+1);
   memcpy(buf,s,size);
}

const char *xstring::set(const char *s)
{
   if(!s)
   {
      xfree(buf);
      buf=0;
      size=0;
      return 0;
   }
   size_t len=strlen(s)+1;
   get_space(len);
   return (char*)memcpy(buf,s,len);
}

const char *xstring::nset(const char *s,int len)
{
   get_space(len+1);
   memcpy(buf,s,len);
   buf[len]=0;
   return buf;
}

const char *xstring::set_allocated(char *s)
{
   size=strlen(s)+1;
   xfree(buf);
   return buf=s;
}

const char *xstring::append(const char *s)
{
   if(!s || !*s)
      return buf;
   if(!buf)
      return set(s);
   size_t len=strlen(buf);
   size_t s_len=strlen(s);
   size_t need=len+s_len+1;
   get_space(need);
   memcpy(buf+len,s,s_len+1);
   return buf;
}

size_t xstring::vstrlen(va_list va)
{
   size_t len=0;
   for(;;)
   {
      const char *s=va_arg(va,const char *);
      if(!s)
	 break;
      len+=strlen(s);
   }
   return len;
}

const char *xstring::vappend(va_list va)
{
   va_list va1;
   size_t len=xstrlen(buf);

   va_copy(va1,va);
   size_t need=len+vstrlen(va1)+1;
   va_end(va1);

   get_space(need);

   for(;;)
   {
      const char *s=va_arg(va,const char *);
      if(!s)
	 break;
      size_t s_len=strlen(s);
      memcpy(buf+len,s,s_len);
      len+=s_len;
   }

   buf[len]=0;
   return buf;
}

const char *xstring::vappend(...)
{
   va_list va;
   va_start(va,this);
   vappend(va);
   va_end(va);
   return buf;
}

const char *xstring::vset(...)
{
   truncate(0);
   va_list va;
   va_start(va,this);
   vappend(va);
   va_end(va);
   return buf;
}

void xstring::truncate(size_t n)
{
   if(n<size)
      buf[n]=0;
}
void xstring::truncate_at(char c)
{
   char *p=strchr(buf,c);
   if(p)
      *p=0;
}
