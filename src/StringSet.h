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

#ifndef STRINGSET_H
#define STRINGSET_H

#include "xmalloc.h"

class StringSet
{
   char **set;
   int set_size;
   int allocated;

   void Init() { set=0; allocated=set_size=0; }
   void Allocate(int);

public:
   StringSet() { Init(); }
   StringSet(const char *const *s,int n) { Init(); Assign(s,n); }
   StringSet(const StringSet &o) { Init(); Assign(o.set,o.set_size); }
   StringSet(const char *s) { Init(); Assign(&s,1); }
   ~StringSet() { Empty(); xfree(set); }

   void Empty();
   void Assign(const char *const *s,int n);
   void Assign(const char *s) { Assign(&s,1); }
   bool IsEqual(const char *const *s,int n) const;
   bool IsEqual(const StringSet &o) const { return IsEqual(o.set,o.set_size); }
   void Append(const char *);
   void AppendFormat(const char *,...) PRINTF_LIKE(2,3);
   void InsertBefore(int,const char *);
   void Replace(int,const char *);
   void Remove(int);

   const char *const *Set() const { return set; }
   char **SetNonConst() { return set; }
   int Count() const { return set_size; }
   const char *String(int i) const { return i>=0 && i<set_size ? set[i] : 0; }
   const char *operator[](int i) const { return String(i); }
};

#endif // STRINGSET_H
