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

#ifndef XLIST_H
#define XLIST_H 1

// this list implementation is based on linux kernel idea of lists

#include <stddef.h>
#include <assert.h>

template<class T> class xlist
{
   xlist<T> *next;
   xlist<T> *prev;
   T *obj;

   static void _add(xlist<T> *node,xlist<T> *prev,xlist<T> *next) {
      next->prev=node;
      node->next=next;
      node->prev=prev;
      prev->next=node;
   }
   static void _del(xlist<T> *prev,xlist<T> *next) {
      next->prev=prev;
      prev->next=next;
   }

protected:
   xlist() : next(this), prev(this), obj(0) {}
public:
   xlist(T *t) : next(0), prev(0), obj(t) {}

   void add(xlist<T> *node) {
      assert(!node->next && !node->prev);
      _add(node,this,next);
   }
   void add(xlist<T>& node) { add(&node); }

   void add_tail(xlist<T> *node) {
      assert(!node->next && !node->prev);
      _add(node,prev,this);
   }
   void add_tail(xlist<T>& node) { add_tail(&node); }

   void remove() {
      _del(prev,next);
      next=prev=0;
   }

   xlist<T> *get_next() const { return next; }
   xlist<T> *get_prev() const { return prev; }
   T *get_obj() const { return obj; }
   T *next_obj() const { return next->obj; }

   int count() const {
      int count=0;
      for(xlist<T> *scan=next; scan!=this; scan=scan->next)
	 count++;
      return count;
   }
   bool listed() const {
      return next!=0;
   }
};

template<class T> class xlist_head : public xlist<T>
{
public:
   xlist_head() {}
   T *first_obj() const { return xlist<T>::next_obj(); }
};

#define xlist_for_each(TYPE,list_head,node,obj) \
   TYPE *obj; \
   for(xlist<TYPE> *node=(list_head).get_next(); obj=node->get_obj(), node!=&list_head; node=node->get_next())

#define xlist_for_each_safe(TYPE,list_head,node,obj,next) \
   TYPE *obj; \
   for(xlist<TYPE> *node=(list_head).get_next(), *next=node->get_next(); obj=node->get_obj(), node!=&list_head; node=next, next=node->get_next())

#endif//XLIST_H
