/*
 * lftp and utils
 *
 * Copyright (c) 2001-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "StringPool.h"
#include "xmalloc.h"

char **StringPool::strings;
int StringPool::n_strings;
int StringPool::allocated;

const char *StringPool::Get(const char *s)
{
   if(!s)
      return 0;

   int l=0;
   int u=n_strings;

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
   n_strings++;
   if(allocated<n_strings)
   {
      allocated+=16;
      strings=(char**)xrealloc(strings,sizeof(*strings)*allocated);
   }
   memmove(strings+u+1,strings+u,(n_strings-u-1)*sizeof(*strings));
   strings[u]=xstrdup(s);
   return strings[u];
}
