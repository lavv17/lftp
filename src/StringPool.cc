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

#include <config.h>
#include "StringPool.h"
#include "xarray.h"

xarray_m<char> StringPool::strings;

const char *StringPool::Get(const char *s)
{
   if(!s)
      return 0;

   int l=0;
   int u=strings.count();

   while(l<u)
   {
      int m = (l + u) / 2;
      int cmp = strcmp(strings[m], s);

      if(cmp==0)  // found it.
	 return strings[m];

      if(cmp>0)
	 u=m;
      else
	 l=m+1;
   }

   // not found.
   // l==u points to first elementh greater than s or past end of array.
   strings.insert(xstrdup(s),u);
   return strings[u];
}
