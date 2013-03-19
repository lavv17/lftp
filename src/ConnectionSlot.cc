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

#include <config.h>
#include "FileAccess.h"
#include "ConnectionSlot.h"

ConnectionSlot ConnectionSlot::lftp_slots;

ConnectionSlot::SlotValue::SlotValue(const char *n,const FileAccess *s)
   : KeyValueDB::Pair(n,s->GetConnectURL())
{
   session=s->Clone();
}
ConnectionSlot::SlotValue::SlotValue(const char *n,const char *v)
   : KeyValueDB::Pair(n,v)
{
   session=FileAccess::New(v);
}
ConnectionSlot::SlotValue *ConnectionSlot::Find(const char *n)
{
   KeyValueDB::Pair **slot=lftp_slots.LookupPair(n);
   return slot?static_cast<SlotValue*>(*slot):0;
}
void ConnectionSlot::Set(const char *n,const FileAccess *fa)
{
   const char *url=fa->GetConnectURL();
   if(!url || !*url)
   {
      lftp_slots.KeyValueDB::Remove(n);
      return;
   }
   ConnectionSlot::SlotValue *s=Find(n);
   if(!s)
   {
      lftp_slots.AddPair(new SlotValue(n,fa));
      return;
   }
   if(!s->session->SameLocationAs(fa))
   {
      s->SetValue(url);
      s->session=fa->Clone();
   }
}
void ConnectionSlot::SetCwd(const char *n,const FileAccess::Path &cwd)
{
   ConnectionSlot::SlotValue *s=Find(n);
   if(!s || !s->session)
      return;
   s->session->SetCwd(cwd);
   s->SetValue(s->session->GetConnectURL());
}
const FileAccess *ConnectionSlot::FindSession(const char *n)
{
   ConnectionSlot::SlotValue *s=Find(n);
   if(s)
      return s->session;
   return 0;
}
char *ConnectionSlot::Format()
{
   return lftp_slots.FormatThis();
}
char *ConnectionSlot::FormatThis()
{
   return KeyValueDB::Format();
}
void ConnectionSlot::Cleanup()
{
   lftp_slots.Empty();
}
ConnectionSlot::ConnectionSlot() : KeyValueDB() {}
ConnectionSlot::~ConnectionSlot() {}
