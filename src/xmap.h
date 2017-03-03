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
   const xstring& each_key() const { return last_entry ? last_entry->key : xstring::null; }
   bool each_finished() const { return last_entry==0; }

   int count() const { return entry_count; }

   void _move_here(_xmap &o);
   void _empty();
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
      return add(key,zero);
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
   T& add(const xstring& key,T e0) { return payload(_add(key))=e0; }
   const T& each_begin() { entry *e=_each_begin(); return e?payload(e):zero; }
   const T& each_next()  { entry *e=_each_next();  return e?payload(e):zero; }
   const T& each_curr()  { return last_entry?payload(last_entry):zero; }
   void move_here(xmap<T> &o) { _move_here(o); }
   void empty() { _empty(); }
   bool exists(const xstring& key) const { return _lookup_c(key); }
};

template<class T> T xmap<T>::zero;

template<class T> class xmap_p : public _xmap
{
   void dispose(T *p) { delete p; }
public:
   xmap_p() : _xmap(sizeof(T*)) {}
   ~xmap_p() {
      for(entry *e=_each_begin(); e; e=_each_next())
	 dispose(payload(e));
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
      dispose(borrow(key));
   }
   void add(const xstring& key,T *e0) {
      entry *e=_add(key);
      dispose(payload(e));
      payload_Lv(e)=e0;
   }
   void add(const char *key,T *e0) { add(xstring::get_tmp(key),e0); }
   T *each_begin() { entry *e=_each_begin(); return e?payload(e):0; }
   T *each_next()  { entry *e=_each_next();  return e?payload(e):0; }
   T *each_curr()  { return last_entry?payload(last_entry):0; }
   void each_set(T *n) { payload_Lv(last_entry)=n; }
   void move_here(xmap_p<T> &o) { _move_here(o); }
   void empty() {
      for(int i=0; i<hash_size; i++) {
	 while(map[i]) {
	    dispose(payload(map[i]));
	    _remove(&map[i]);
	 }
      }
   }
   bool exists(const xstring& key) const { return _lookup_c(key); }
};

#endif // XMAP_H
