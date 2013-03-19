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

#ifndef NETRC_H
#define NETRC_H

#include "xstring.h"

class NetRC
{
public:
   class Entry
   {
   public:
      xstring host;
      xstring user;
      xstring pass;
      xstring acct;

      Entry(const char *h=0,const char *u=0,const char *p=0,const char *a=0)
	 : host(h), user(u), pass(p), acct(a) {}
   };

   static Entry *LookupHost(const char *host,const char *user=0);
};

#endif//NETRC_H
