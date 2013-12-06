/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef XHEAP_H
#define XHEAP_H 1

// min-heap implementation with random remove

#include <assert.h>
#include "xarray.h"

#define costly_assert(x) /*assert(x)*/

template<class T>
class xheap
{
public:
   class node
   {
      T *obj;
      int heap_index;
      friend class xheap<T>;
   public:
      node(T *t) : obj(t), heap_index(0) {}
   };

private:
   xarray<node*> heap;

   int count() const { return heap.count(); }
   node*& ptr(int i) { return heap[i-1]; }
   T& elem(int i) { return *(ptr(i)->obj); }
   void swap(int a,int b) {
      ptr(a)=replace_value(ptr(b),ptr(a));
      ptr(a)->heap_index=a;
      ptr(b)->heap_index=b;
   }
   void siftup(int i) {
      while(i>1 && elem(i)<elem(i/2)) {
	 swap(i,i/2);
	 i/=2;
      }
   }
   void siftdown(int i) {
      while(i<=count()/2) {
	 int i2=i*2;
	 // choose smallest of heirs
	 if(i2<count() && elem(i2+1)<elem(i2))
	    i2++;
	 if(elem(i)<elem(i2))
	    break;
	 swap(i,i2);
	 i=i2;
      }
   }
   bool is_heap(int a,int b) {
      while(a*2<=b) {
	 if(elem(a*2)<elem(a))
	    return false;
	 a++;
      }
      return true;
   }
   void chop() {
      ptr(count())->heap_index=0;
      heap.chop();
   }
   void fix(int i) {
      siftdown(i);
      siftup(i);
   }
   void remove(int i) {
      if(i==count()) {
	 chop();
	 return;
      }
      assert(i>0 && i<count());
      swap(i,count());
      chop();
      fix(i);
      costly_assert(is_heap(1,count()));
   }
public:
   void add(node& n) {
      if(n.heap_index) {
	 int i=n.heap_index;
	 assert(i>0 && i<=count());
	 assert(ptr(i)==&n);
	 return;
      }
      heap.append(&n);
      siftup(n.heap_index=count());
      costly_assert(is_heap(1,count()));
   }
   void fix(node& n) {
      fix(n.heap_index);
   }
   T *get_min() {
      return count()>0?ptr(1)->obj:0;
   }
   T *pop_min() {
      T *m=get_min();
      if(!m)
	 return 0;
      remove(1);
      return m;
   }
   void remove(node& x) {
      if(!x.heap_index)
	 return;
      assert(ptr(x.heap_index)==&x);
      remove(x.heap_index);
      assert(!x.heap_index);
   }
};

#endif//XHEAP_H
