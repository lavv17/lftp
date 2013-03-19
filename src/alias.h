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

#ifndef ALIAS_H
#define ALIAS_H

#include "xstring.h"

class Alias
{
   Alias *next;
   xstring_c alias;
   xstring_c value;

   static Alias *base;

   Alias(const char *alias,const char *value,Alias *next)
      : next(next), alias(alias), value(value) {}

public:
   static const char *Find(const char *alias);

   static void Add(const char *alias,const char *value);
   static void Del(const char *alias);

   static char *Format();

   friend char *command_generator(const char *text,int state);
};

class TouchedAlias
{
   xstring_c alias;
   TouchedAlias *next;
public:
   TouchedAlias(const char *a,TouchedAlias *n)
      : alias(a), next(n) {}
   static void FreeChain(TouchedAlias *chain)
   {
      while(chain)
	 delete replace_value(chain,chain->next);
   }
   static bool IsTouched(const char *a,TouchedAlias *chain)
   {
      while(chain)
      {
	 if(chain->alias.eq(a))
	    return true;
	 chain=chain->next;
      }
      return false;
   }
};

#endif//ALIAS_H
