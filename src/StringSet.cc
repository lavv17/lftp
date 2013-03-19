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
#include "StringSet.h"
#include "misc.h"

#define set_size set.count()

StringSet::StringSet() {}
StringSet::StringSet(const char *const *s,int n) { Assign(s,n); }
StringSet::StringSet(const StringSet &o) { Assign(o.set.get(),o.set.count()); }
StringSet::StringSet(const char *s) { Assign(&s,1); }

bool StringSet::IsEqual(const char *const *set1,int n1) const
{
   if(set_size!=n1)
      return false;
   int i=0;
   while(i<n1)
   {
      if(strcmp(set[i],set1[i]))
	 return false;
      i++;
   }
   return true;
}
void StringSet::Assign(const char *const *set1,int n1)
{
   Empty();
   while(n1-->0)
      set.append(xstrdup(*set1++));
}
void StringSet::Append(const char *s)
{
   if(!s)
      return;
   set.append(xstrdup(s));
}
void StringSet::Replace(int i,const char *s)
{
   if(i==set_size)
      Append(s);
   else if(i>=0 && i<set_size)
   {
      xstrset(set[i],s);
      if(!s && i==set_size-1)
	 set.set_length(set.count()-1);
   }
}
void StringSet::InsertBefore(int i,const char *s)
{
   if(!s)
      return;
   set.insert(xstrdup(s),i);
}

void StringSet::AppendFormat(const char *f,...)
{
   va_list v;
   va_start(v,f);
   set.append(xstring::vformat(f,v).borrow());
   va_end(v);
}

void StringSet::MoveHere(StringSet &o)
{
   set.set(o.set);
   o.set.borrow();
}

char *StringSet::Pop(int i)
{
   if(i<0 || i>=set_size)
      return 0;
   char *s=set[i];
   set[i]=0;
   set.remove(i);
   return s;
}
