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

#ifndef LSCACHE_H
#define LSCACHE_H

#include <time.h>
#include "FileAccess.h"

class LsCache
{
   time_t   timestamp;
   char	 *data;
   int	 data_len;

   char	 *arg;
   FileAccess *loc;
   int	 mode;

   LsCache *next;
   static LsCache *chain;
   static bool use;
   static long sizelimit;
   static time_t ttl;

   friend class ExpireHelper : public SMTask
   {
   public:
      LsCache *expiring;
      int Do();
      ExpireHelper() { expiring=0; }
   };
   static ExpireHelper expire_helper;

   static void CheckSize();

public:
   static void Add(FileAccess *p_loc,const char *a,int m,char *d,int l);
   static int Find(FileAccess *p_loc,const char *a,int m,char **d,int *l);

   static void List();
   static void Flush();
   static void On() { use=true; }
   static void Off() { use=false; }
   static void SetSizeLimit(long l) { sizelimit=l; }
   static void SetExpire(time_t t) { ttl=t; }

   ~LsCache();

   int Do();
};

#endif//LSCACHE_H
