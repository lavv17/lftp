/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef BOOKMARK_H
#define BOOKMARK_H

#include <sys/types.h>
#include "keyvalue.h"

class Bookmark : public KeyValueDB
{
   char *bm_file;
   int bm_fd;
   time_t stamp;

   void Save();
   void Load();
   void Refresh();
   void Lock(int type) { KeyValueDB::Lock(bm_fd,type); }
   void PreModify();
   void PostModify();
   void Close();
public:
   void Add(const char *id,const char *value);
   void Remove(const char *id);
   const char *Lookup(const char *id);
   void List();
   char *Format();

   Bookmark();
   ~Bookmark();
};

#endif //BOOKMARK_H
