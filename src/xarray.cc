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
#include <assert.h>
#include "xarray.h"

void xarray0::get_space(size_t s,size_t g)
{
   if(!buf)
      buf=xmalloc(element_size*(size=s));
   else if(size<s+1)
      buf=realloc(buf,element_size*(size=(s|(g-1))+1));
   else if(size>=g*8 && s<size/2)
      buf=realloc(buf,element_size*(size/=2));
}

void xarray0::_nset(const void *s,int len)
{
   if(!s)
   {
      xfree(buf);
      init();
      return;
   }
   this->len=len;
   if(s==buf)
      return;
   if(s>buf && s<(char*)buf+size*element_size)
   {
      memmove(buf,s,len*element_size);
      return;
   }
   get_space(len);
   memcpy(buf,s,len*element_size);
}

void *xarray0::_insert(int before)
{
   assert(before>=0 && before<=len);
   get_space(len+1);
   if(before<len)
      memmove(get_ptr(before+1),get_ptr(before),element_size*(len-before));
   len++;
   return get_ptr(before);
}
void *xarray0::_append() {
   get_space(len+1);
   return get_ptr(len++);
}
void xarray0::_remove(int i) {
   assert(i>=0 && i<len);
   if(i<len-1)
      memmove(get_ptr(i),get_ptr(i+1),element_size*(len-i-1));
   len--;
}
