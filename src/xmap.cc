/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <assert.h>
#include "xmap.h"
#include <stdio.h>

void _xmap::new_map()
{
   map.get_space(hash_size,1);
   map.set_length(hash_size);
   for(int i=0; i<hash_size; i++)
      map[i]=0;
}
_xmap::_xmap(int vs)
   : value_size(vs)
{
   entry_count=0;
   hash_size=1;
   new_map();
}
void _xmap::_empty()
{
   for(int i=0; i<hash_size; i++) {
      while(map[i])
	 _remove(&map[i]);
   }
   assert(entry_count==0);
}
_xmap::~_xmap()
{
   _empty();
}

int _xmap::make_hash(const xstring& s) const
{
   if(hash_size==1)
      return 0;
   unsigned hash=0x12345678;
   for(unsigned i=0; i<s.length(); i++) {
      hash+=(hash<<5)+s[i];
   }
   hash+=(hash<<5)+s.length();
   hash%=hash_size;
   return hash;
}

_xmap::entry **_xmap::_lookup(const xstring& key)
{
   int hash=make_hash(key);
   entry **ep=&map[hash];
   entry *e=*ep;
   while(e) {
      if(e->key.eq(key))
	 return ep;
      ep=&e->next;
      e=*ep;
   }
   return ep;
}

_xmap::entry *_xmap::_lookup_c(const xstring& key) const
{
   for(entry *e=map[make_hash(key)]; e; e=e->next) {
      if(e->key.eq(key))
	 return e;
   }
   return 0;
}

void _xmap::rebuild_map()
{
   static const int primes[]={
      17,67,257,1031,4099,16411,65537,262147,1048583,4194319,16777259,
      67108879,268435459,1073741827
   };
   hash_size=entry_count*2;
   // a prime is better, find it.
   for(unsigned pi=0; pi<sizeof(primes)/sizeof(primes[0]); pi++) {
      if(hash_size<primes[pi]) {
	 hash_size=primes[pi];
	 break;
      }
   }
   xarray_p<_xmap::entry> old_map;
   old_map.move_here(map);
   new_map();
   for(int i=0; i<old_map.length(); i++) {
      entry *e=old_map[i];
      old_map[i]=0;
      while(e) {
	 entry *next=e->next;
	 int hash=make_hash(e->key);
	 e->next=map[hash];
	 map[hash]=e;
	 e=next;
      }
   }
}

_xmap::entry *_xmap::_add(const xstring& key)
{
   entry **ep=_lookup(key);
   if(*ep==0) {
      entry *n=(entry*)xmalloc(sizeof(entry)+value_size);
      memset(n,0,sizeof(entry)+value_size);
      n->next=0;
      n->key.set(key);
      *ep=n;
      entry_count++;
      if(entry_count>hash_size*2)
	 rebuild_map();
      return n;
   }
   return *ep;
}
void _xmap::_remove(entry **ep)
{
   if(!ep || !*ep)
      return;
   entry *e=*ep;
   e->key.unset();
   *ep=e->next;
   xfree(e);
   entry_count--;
}

_xmap::entry *_xmap::_each_begin()
{
   each_entry=0;
   each_hash=-1;
   return _each_next();
}
_xmap::entry *_xmap::_each_next()
{
   while(each_hash<hash_size) {
      entry *e=each_entry;
      if(e) {
	 last_entry=e;
	 each_entry=e->next;
	 return e;
      }
      each_entry=map[++each_hash];
   }
   last_entry=0;
   return 0;
}

void _xmap::_move_here(_xmap &o)
{
   value_size=o.value_size;
   hash_size=o.hash_size;
   entry_count=o.entry_count;
   map.move_here(o.map);
   o.hash_size=1;
   o.entry_count=0;
   o.new_map();
}
