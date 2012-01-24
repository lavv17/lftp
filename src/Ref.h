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

/* $Id: Ref.h,v 1.8 2008/11/27 05:56:26 lav Exp $ */

#ifndef REF_H
#define REF_H

template<typename T> class Ref
{
   Ref<T>(const Ref<T>&);  // disable cloning
   void operator=(const Ref<T>&);   // and assignment

protected:
   T *ptr;

public:
   Ref<T>() { ptr=0; }
   Ref<T>(T *p) { ptr=p; }
   ~Ref<T>() { delete ptr; }
   void operator=(T *p) { delete ptr; ptr=p; }
   operator const T*() const { return ptr; }
   T *operator->() const { return ptr; }
   T *borrow() { return replace_value(ptr,(T*)0); }
   const T *get() const { return ptr; }
   T *get_non_const() const { return ptr; }

   template<class C> const Ref<C>& Cast() const
      { void(static_cast<C*>(ptr)); return *(const Ref<C>*)this; }

   static const Ref<T> null;

   void _set(T *p) { ptr=p; }
   void _clear() { ptr=0; }
   void unset() { *this=0; }
};

template<typename T> const Ref<T> Ref<T>::null;

#endif
