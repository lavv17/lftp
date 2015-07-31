/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef BOOKMARK_H
#define BOOKMARK_H

#include <sys/types.h>
#include "keyvalue.h"

class Bookmark : public KeyValueDB
{
   xstring bm_file;
   int bm_fd;
   time_t stamp;

   void Save();
   void Load();
   void Refresh();
   int Lock(int type) { return KeyValueDB::Lock(bm_fd,type); }
   void PreModify();
   void PostModify();
   void Close();
   void AutoSync();
public:
   void Add(const char *id,const char *value);
   void Remove(const char *id);
   const char *Lookup(const char *id);
   char *Format();
   char *FormatHidePasswords();
   void UserLoad() { Load(); Close(); }
   void UserSave();

   Bookmark();
   ~Bookmark();

   void Rewind()
      {
	 Refresh();
	 KeyValueDB::Rewind();
      }

   const xstring& GetFilePath() const { return bm_file; }
};

extern Bookmark lftp_bookmarks;

#endif //BOOKMARK_H
