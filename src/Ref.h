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
   T *get_non_const() { return ptr; }

   template<class C> const Ref<C>& Cast() const
      { void(static_cast<C*>(ptr)); return *(const Ref<C>*)this; }

   static const Ref<T> null;
};

template<typename T> const Ref<T> Ref<T>::null;

#endif
