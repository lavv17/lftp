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

#ifndef CONNECTIONSLOT_H
#define CONNECTIONSLOT_H

#include "FileAccess.h"
#include "keyvalue.h"

class ConnectionSlot : public KeyValueDB
{
   static ConnectionSlot lftp_slots;

   class SlotValue : public KeyValueDB::Pair
   {
   public:
      FileAccessRef session;
      SlotValue(const char *n,const FileAccess *s);
      SlotValue(const char *n,const char *v);
   };

   Pair *NewPair(const char *n,const char *v)
      {
	 return new SlotValue(n,v);
      }

public:
   static ConnectionSlot::SlotValue *Find(const char *n);
   static const FileAccess *FindSession(const char *n);
   static void Set(const char *n,const FileAccess *s);
   static void SetCwd(const char *n,const FileAccess::Path &cwd);
   static void Remove(const char *n);
   static char *Format();
   char *FormatThis();
   static void Cleanup();
   ConnectionSlot();
   ~ConnectionSlot();
};

#endif
