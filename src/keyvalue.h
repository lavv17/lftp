/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef KEYVALUE_H
#define KEYVALUE_H

#include "xmalloc.h"

class KeyValueDB
{
   class Pair
   {
   public:
      char *key;
      char *value;
      Pair *next;
      Pair(const char *k,const char *v,Pair *n)
	 {
	    key=xstrdup(k);
	    value=xstrdup(v);
	    next=n;
	 }
      ~Pair()
	 {
	    xfree(key);
	    xfree(value);
	 }
      int KeyCompare(const char *s)
	 {
	    return strcmp(s,key);
	 }
   };

   void Purge(Pair **p)
      {
	 Pair *to_free=*p;
	 *p=to_free->next;
	 delete to_free;
      }
   Pair **LookupPair(const char *key);

protected:
   int Lock(int fd,int type);

   Pair *chain;

public:
   void Add(const char *id,const char *value);
   void Remove(const char *id);
   const char *Lookup(const char *id);
   void Empty()
      {
	 while(chain)
	    Purge(&chain);
      }
   int Write(int fd);
   int Read(int fd);
   void Sort();
   char *Format(); // returns formatted contents (malloc'ed)

   KeyValueDB()
      {
	 chain=0;
      }
   ~KeyValueDB()
      {
	 Empty();
      }
};
#endif //KEYVALUE_H
