/*
 * lftp and utils
 *
 * Copyright (c) 2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef XARRAY_H
#define XARRAY_H 1

#include <sys/types.h>
#include <assert.h>
#include "xmalloc.h"
#include "Ref.h"

class xarray0
{
protected:
   void *buf;

   size_t size;
   size_t len;
   const size_t element_size;

   void init() { buf=0; size=len=0; }
   void *get_ptr(int n) { return static_cast<char*>(buf)+n*element_size; }

   xarray0& operator=(const xarray0&); // make assignment fail
   xarray0(const xarray0&);	       // disable cloning

public:
   xarray0(size_t e) : element_size(e) { init(); }
   ~xarray0() { xfree(buf); }

   // allocates s slots, with preferred granularity g
   void get_space(size_t s,size_t g=32);
   size_t get_element_size() { return element_size; }

   size_t length() const { return len; }
   size_t count() const { return len; }

   void _set_length(size_t n) { assert(n>=0 && n<=size); len=n; }
   void _nset(const void *s,int len);
   void _unset() { _nset(0,0); }
   void *_insert(int before);
   void *_append();
   void _remove(int i);
};

template<typename T>
class xarray : public xarray0
{
public:
   xarray() : xarray0(sizeof(T)) {}
   T *get_non_const() { return static_cast<T*>(buf); }
   const T *get() const { return static_cast<const T*>(buf); }
   T& operator[](int i) {
      assert(i>=0 && (size_t)i<len);
      return get_non_const()[i];
   }
   const T& operator[](int i) const {
      assert(i>=0 && (size_t)i<len);
      return get()[i];
   }
   size_t get_element_size() const { return sizeof(T); }
   operator const T*() const { return get(); }
   void nset(const T *s,int len) { _nset(s,len); }
   void set(const xarray<T> &a) { nset(a.get(),a.length()); }
   void set_length(size_t n) { _set_length(n); }
   void unset() { _unset(); }
   void truncate() { set_length(0); }
   void insert(const T& n,int before) { *static_cast<T*>(_insert(before))=n; }
   void append(const T& n) { *static_cast<T*>(_append())=n; }
   void remove(int i) { _remove(i); }
};

template<typename T>
class RefArray : public xarray0
{
   void dispose(int i) { delete get_non_const()[i].borrow(); }
   void dispose(int i,int j) { while(i<j) dispose(i++); }
   void clear(int i) { get_non_const()[i].ptr=0; }
   void clear(int i,int j) { while(i<j) clear(i++); }
public:
   RefArray() : xarray0(sizeof(Ref<T>)) {}
   Ref<T> *get_non_const() { return static_cast<Ref<T>*>(buf); }
   const Ref<T> *get() const { return static_cast<const Ref<T>*>(buf); }
   Ref<T>& operator[](int i) {
      assert(i>=0 && (size_t)i<len);
      return get_non_const()[i];
   }
   const Ref<T>& operator[](int i) const {
      assert(i>=0 && (size_t)i<len);
      return get()[i];
   }
   size_t get_element_size() const { return sizeof(Ref<T>); }
   void set_length(size_t n) { dispose(n,len); clear(len,n); _set_length(n); }
   void unset() { dispose(0,len); _unset(); }
   void truncate() { set_length(0); }
   void insert(T *n,int before) { static_cast<Ref<T>*>(_insert(before))->ptr=n; }
   void append(T *n) { static_cast<Ref<T>*>(_append())->ptr=n; }
   void remove(int i) { dispose(i); _remove(i); }
};

#endif // XARRAY_H
