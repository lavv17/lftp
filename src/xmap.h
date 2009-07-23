/*
 * lftp and utils
 *
 * Copyright (c) 2009 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#ifndef XMAP_H
#define XMAP_H 1

#include "xarray.h"

class _xmap
{
protected:
   struct entry
   {
      entry *next;
      xstring key;
   };
   int value_size;

   int hash_size;
   int make_hash(const xstring& s) const;

   xarray_p<_xmap::entry> map;
   void new_map();
   void rebuild_map();

   int entry_count;

   int each_hash;
   entry *each_entry;
   entry *last_entry; // the entry returned by last each_begin/next

public:
   _xmap(int vs);
   ~_xmap();
   void _add(const xstring& key,void *value);
   _xmap::entry **_lookup(const xstring& key);
   _xmap::entry *_lookup_c(const xstring& key) const;
   entry *_add(const xstring& key);
   void _remove(entry **ep);

   entry *_each_begin();
   entry *_each_next();
   const xstring *each_key() const { return &last_entry->key; }

   int count() { return entry_count; }

   void _move_here(_xmap &o);
};

template<class T> class xmap : public _xmap
{
   xmap& operator=(const xmap&); // make assignment fail
   xmap(const xmap&);		 // disable cloning
public:
   xmap() : _xmap(sizeof(T)) {}
   ~xmap() {}
   static T& payload(entry *e) {
      return *(T*)(e+1);
   }
   static T zero;
   T& lookup_Lv(const xstring& key) {
      entry **e=_lookup(key);
      if(e && *e)
	 return payload(*e);
      return zero;
   }
   const T& lookup(const xstring& key) const {
      entry *e=_lookup_c(key);
      if(e)
	 return payload(e);
      return zero;
   }
   const T& lookup(const char *key) const { return lookup(xstring::get_tmp(key)); }
   T& operator[](const xstring& key) { return lookup_Lv(key); }
   void remove(const xstring& key) { _remove(_lookup(key)); }
   void add(const xstring& key,T e0) { payload(_add(key))=e0; }
   const T& each_begin() { entry *e=_each_begin(); return e?payload(e):zero; }
   const T& each_next()  { entry *e=_each_next();  return e?payload(e):zero; }
   void move_here(xmap<T> &o) { _move_here(o); }
};

template<class T> T xmap<T>::zero;

template<class T> class xmap_p : public _xmap
{
public:
   xmap_p() : _xmap(sizeof(T*)) {}
   ~xmap_p() {
      for(entry *e=_each_begin(); e; e=_each_next())
	 xfree(payload(e));
   }
   T*& payload_Lv(entry *e) {
      return *(T**)(e+1);
   }
   T* payload(entry *e) const {
      return *(T**)(e+1);
   }
   T* lookup(const xstring& key) const {
      entry *e=_lookup_c(key);
      if(e)
	 return payload(e);
      return 0;
   }
   T* lookup(const char *key) const { return lookup(xstring::get_tmp(key)); }
   T* borrow(const xstring& key) {
      entry **e=_lookup(key);
      if(e && *e) {
	 T *p=payload(*e);
	 _remove(e);
	 return p;
      }
      return 0;
   }
   void remove(const xstring& key) {
      xfree(borrow(key));
   }
   void add(const xstring& key,T *e0) {
      entry *e=_add(key);
      xfree(payload(e));
      payload_Lv(e)=e0;
   }
   T *each_begin() { entry *e=_each_begin(); return e?payload(e):0; }
   T *each_next()  { entry *e=_each_next();  return e?payload(e):0; }
   void each_set(T *n) { payload_Lv(last_entry)=n; }
   void move_here(xmap_p<T> &o) { _move_here(o); }
};

#endif // XMAP_H
