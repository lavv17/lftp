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

#ifndef KEYVALUE_H
#define KEYVALUE_H

#include "xstring.h"

class StringMangler
{
   typedef const char *(*mangle_t)(const char *);
   mangle_t mangle;
public:
   const char *operator()(const char *s) { return mangle?mangle(s):s; }
   StringMangler(mangle_t m=0) { mangle=m; }
};

class KeyValueDB
{
public:
   class Pair
   {
   public:
      xstring_c key;
      xstring_c value;
      Pair *next;
      Pair(const char *k,const char *v)
	 : key(k), value(v), next(0) {}
      virtual ~Pair() {}
      int KeyCompare(const char *s) const
	 {
	    return strcmp(s,key);
	 }
      void SetValue(const char *v) { value.set(v); }
   };

protected:
   void Purge(Pair **p)
      {
	 Pair *to_free=*p;
	 if(current==to_free)
	    current=to_free->next;
	 *p=to_free->next;
	 delete to_free;
      }
   Pair **LookupPair(const char *key) const;
   void AddPair(Pair *p)
      {
	 p->next=chain;
	 chain=p;
      }
   virtual Pair *NewPair(const char *id,const char *value)
      {
	 return new Pair(id,value);
      }

   int Lock(int fd,int type);

   Pair *chain;

   Pair *current;

public:
   void Add(const char *id,const char *value);
   void Remove(const char *id);
   const char *Lookup(const char *id) const;
   void Empty()
      {
	 while(chain)
	    Purge(&chain);
      }
   int Write(int fd);
   int Read(int fd);
   void Sort();
   char *Format(StringMangler m=0); // returns formatted contents (malloc'ed)

   void Rewind()
      {
	 current=chain;
      }
   const char *CurrentKey() const
      {
	 if(!current)
	    return 0;
	 return current->key;
      }
   const char *CurrentValue() const
      {
	 if(!current)
	    return 0;
	 return current->value;
      }
   bool Next()
      {
	 if(current==0)
	    return false;
	 current=current->next;
	 return current!=0;
      }

   KeyValueDB()
      {
	 chain=0;
	 current=0;
      }
   virtual ~KeyValueDB()
      {
	 Empty();
      }

   static int KeyCompare(const Pair *a,const Pair *b)
      {
	 return strcmp(a->key,b->key);
      }
   static int VKeyCompare(const void *a,const void *b);
};
#endif //KEYVALUE_H
