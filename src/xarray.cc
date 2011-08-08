/*
 * lftp and utils
 *
 * Copyright (c) 2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id: xarray.cc,v 1.7 2009/03/20 10:22:38 lav Exp $ */

#include <config.h>
#include <assert.h>
#include "xarray.h"

void xarray0::get_space(size_t s,size_t g)
{
   if(!buf)
      buf=xmalloc(element_size*(size=s+keep_extra));
   else if(size<s+keep_extra)
      buf=realloc(buf,element_size*(size=(s|(g-1))+keep_extra));
   else if(size>=g*8 && s+keep_extra<=size/2)
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
void *xarray0::_append()
{
   get_space(len+1);
   return get_ptr(len++);
}
void xarray0::_remove(int i,int j)
{
   assert(i<j && i>=0 && j<=len);
   if(j<len)
      memmove(get_ptr(i),get_ptr(j),element_size*(len-j));
   len-=(j-i);
}

void *xarray0::_borrow()
{
   size=len=0;
   return replace_value(buf,(void*)0);
}
void xarray0::move_here(xarray0& o)
{
   xfree(buf);
   size=o.size;
   len=o.len;
   buf=o._borrow();
}

bool xarray0::_bsearch(const void *n,qsort_cmp_t cmp,int *pos)
{
   int lo=0;
   int hi=len;
   while(lo<hi) {
      int m=(lo+hi)/2;
      int r=cmp(n,get_ptr(m));
      if(r<0)
	 hi=m;
      else if(r>0)
	 lo=m+1;
      else {
	 *pos=m;
	 return true;
      }
   }
   *pos=lo;
   return false;
}

void *xarray0::_insert_ordered(const void *n,qsort_cmp_t cmp)
{
   int pos;
   (void)_bsearch(n,cmp,&pos);
   return _insert(pos);
}
