/*
 * lftp and utils
 *
 * Copyright (c) 2004 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "StringSet.h"

bool StringSet::IsEqual(const char *const *set1,int n1) const
{
   if(set_size!=n1)
      return false;
   int i=0;
   while(i<set_size)
   {
      if(strcmp(set[i],set1[i]))
	 return false;
      i++;
   }
   return true;
}
void StringSet::Empty()
{
   while(set_size>0)
   {
      set_size--;
      xfree(set[set_size]);
      set[set_size]=0;
   }
}
void StringSet::Allocate(int req)
{
   // alloc at least one extra for trailing zero
   int size=((req+4)&~3);
   if(allocated<size)
   {
      set=(char**)xrealloc(set,sizeof(*set)*(allocated=size));
      while(req<size)
	 set[req++]=0;
   }
}
void StringSet::Assign(const char *const *set1,int n1)
{
   Empty();
   Allocate(n1);
   set_size=0;
   while(set_size<n1)
   {
      set[set_size]=xstrdup(set1[set_size]);
      set_size++;
   }
}
void StringSet::Append(const char *s)
{
   if(!s)
      return;
   Allocate(set_size+1);
   set[set_size++]=xstrdup(s);
}
void StringSet::Replace(int i,const char *s)
{
   if(i==set_size)
      Append(s);
   else if(i>=0 && i<set_size)
   {
      if(s)
      {
	 xfree(set[i]);
	 set[i]=xstrdup(s);
      }
      else if(i==set_size-1)
      {
	 xfree(set[--set_size]);
	 set[set_size]=0;
      }
   }
}
void StringSet::InsertBefore(int i,const char *s)
{
   if(!s)
      return;
   if(i==set_size)
      Append(s);
   else if(i>=0 && i<set_size)
   {
      Allocate(set_size+1);
      memmove(set+i+1,set+i,(set_size-i)*sizeof(*set));
      set_size++;
      set[i]=xstrdup(s);
   }
}
void StringSet::Remove(int i)
{
   if(i>=0 && i<set_size)
   {
      xfree(set[i]);
      memmove(set+i,set+i+1,(set_size-i)*sizeof(*set));
      set_size--;
   }
}
