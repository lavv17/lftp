/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef ALIAS_H
#define ALIAS_H

#include "xmalloc.h"

class Alias
{
   Alias *next;
   char	 *alias;
   char	 *value;

   static Alias *base;

   Alias(const char *alias,const char *value,Alias *next);

public:
   ~Alias();

   static const char *Find(const char *alias);

   static void Add(const char *alias,const char *value);
   static void Del(const char *alias);

   static char *Format();

   friend char *command_generator(const char *text,int state);
};

class TouchedAlias
{
   char *alias;
   TouchedAlias *next;
public:
   TouchedAlias(const char *a,TouchedAlias *n)
   {
      alias=xstrdup(a);
      next=n;
   }
   ~TouchedAlias()
   {
      free(alias);
   }
   static void FreeChain(TouchedAlias *chain)
   {
      while(chain)
      {
	 TouchedAlias *next=chain->next;
	 delete chain;
	 chain=next;
      }
   }
   static bool IsTouched(const char *a,TouchedAlias *chain)
   {
      while(chain)
      {
	 if(!strcmp(chain->alias,a))
	    return true;
	 chain=chain->next;
      }
      return false;
   }
};

#endif//ALIAS_H
