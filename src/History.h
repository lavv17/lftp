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

#ifndef HISTORY_H
#define HISTORY_H

#include "keyvalue.h"
#include "FileAccess.h"

class History : public KeyValueDB
{
   KeyValueDB *full;
   time_t stamp;

   xstring file;
   int fd;
   bool modified;

   void Load();
   void Close();
   void Refresh();

   const char *extract_url(const char *res);
   time_t extract_stamp(const char *res);

public:
   void Set(const FileAccess *s,const FileAccess::Path &p);
   void Set(const FileAccess *s) { Set(s,s->GetCwd()); }
   const char *Lookup(const FileAccess *s);
   void Save();
   History();
   ~History();

   void Rewind()
      {
	 Refresh();
	 KeyValueDB::Rewind();
      }
};

#endif //HISTORY_H
