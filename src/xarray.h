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
#include "xmalloc.h"
#include "Ref.h"

class xarray0
{
protected:
   void *buf;

   size_t size;
   int len;
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

   int length() const { return len; }
   int count()  const { return len; }

   void _set_length(size_t n) { len=n; }
   void _nset(const void *s,int len);
   void _unset() { _nset(0,0); }
   void *_insert(int before);
   void *_append();
   void _remove(int i);
   void *_borrow() { size=len=0; return replace_value(buf,(void*)0); }

   operator bool() const { return buf!=0; }
};

template<typename T>
class xarray : public xarray0
{
public:
   xarray() : xarray0(sizeof(T)) {}
   T *get_non_const() { return static_cast<T*>(buf); }
   const T *get() const { return static_cast<const T*>(buf); }
   T& operator[](int i) { return get_non_const()[i]; }
   const T& operator[](int i) const { return get()[i]; }
   size_t get_element_size() const { return sizeof(T); }
   operator const T*() const { return get(); }
   void nset(const T *s,int len) { _nset(s,len); }
   void set(const xarray<T> &a) { nset(a.get(),a.count()); }
   void set_length(size_t n) { _set_length(n); }
   void unset() { _unset(); }
   void truncate() { set_length(0); }
   void insert(const T& n,int before) { *static_cast<T*>(_insert(before))=n; }
   void append(const T& n) { *static_cast<T*>(_append())=n; }
   void remove(int i) { _remove(i); }
   T *borrow() { return static_cast<T*>(_borrow()); }
};

template<typename T,typename RefT>
class _RefArray : public xarray0
{
   void dispose(int i) { get_non_const()[i]=0; }
   void dispose(int i,int j) { while(i<j) dispose(i++); }
   void clear(int i) { get_non_const()[i].ptr=0; }
   void clear(int i,int j) { while(i<j) clear(i++); }
public:
   _RefArray() : xarray0(sizeof(RefT)) {}
   ~_RefArray() { dispose(0,len); }
   RefT *get_non_const() { return static_cast<RefT*>(buf); }
   const RefT *get() const { return static_cast<const RefT*>(buf); }
   RefT& operator[](int i) { return get_non_const()[i]; }
   const RefT& operator[](int i) const { return get()[i]; }
   size_t get_element_size() const { return sizeof(RefT); }
   void set_length(size_t n) { dispose(n,len); clear(len,n); _set_length(n); }
   void unset() { dispose(0,len); _unset(); }
   void truncate() { set_length(0); }
   void insert(T *n,int before) { static_cast<RefT*>(_insert(before))->ptr=n; }
   void append(T *n) { static_cast<RefT*>(_append())->ptr=n; }
   void remove(int i) { dispose(i); _remove(i); }
   RefT *borrow() { return static_cast<RefT*>(_borrow()); }
};

template<typename T>
class RefArray : public _RefArray< T,Ref<T> > {
   RefArray& operator=(const RefArray&); // make assignment fail
   RefArray(const RefArray&);	       // disable cloning
public:
   RefArray() : _RefArray< T,Ref<T> >() {}
};

template<typename T>
class xarray_p : public xarray0
{
   void dispose(int i) { xfree(get_non_const()[i]); }
   void dispose(int i,int j) { while(i<j) dispose(i++); }
   void clear(int i) { get_non_const()[i]=0; }
   void clear(int i,int j) { while(i<j) clear(i++); }
public:
   xarray_p() : xarray0(sizeof(T*)) {}
   ~xarray_p() { dispose(0,len); }
   T **get_non_const() { return static_cast<T**>(buf); }
   T *const* get() const { return static_cast<T*const*>(buf); }
   T *&operator[](int i) { return get_non_const()[i]; }
   T *operator[](int i) const { return get()[i]; }
   size_t get_element_size() const { return sizeof(T*); }
   void nset(T *const*s,int len) { dispose(0,len); _nset(s,len); }
   void set(const xarray_p<T> &a) { nset(a.get(),a.count()); }
   void set_length(size_t n) { dispose(n,len); clear(len,n); _set_length(n); }
   void unset() { dispose(0,len); _unset(); }
   void truncate() { set_length(0); }
   void insert(T *n,int before) { *static_cast<T**>(_insert(before))=n; }
   void append(T *n) { *static_cast<T**>(_append())=n; }
   void remove(int i) { dispose(i); _remove(i); }
   T **borrow() { return static_cast<T**>(_borrow()); }
};

#endif // XARRAY_H
