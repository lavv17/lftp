/*
 * lftp and utils
 *
 * Copyright (c) 2002 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "FileAccess.h"
#include "ConnectionSlot.h"

ConnectionSlot ConnectionSlot::lftp_slots;

ConnectionSlot::SlotValue::SlotValue(const char *n,FileAccess *s)
   : KeyValueDB::Pair(n,s->GetConnectURL())
{
   session=s->Clone();
}
ConnectionSlot::SlotValue::SlotValue(const char *n,const char *v)
   : KeyValueDB::Pair(n,v)
{
   session=FileAccess::New(v);
}
ConnectionSlot::SlotValue::~SlotValue()
{
   SessionPool::Reuse(session);
}
ConnectionSlot::SlotValue *ConnectionSlot::Find(const char *n)
{
   SlotValue **slot=(SlotValue**)lftp_slots.LookupPair(n);
   return slot?*slot:0;
}
void ConnectionSlot::Set(const char *n,FileAccess *fa)
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
   s->SetValue(fa->GetConnectURL());
   SessionPool::Reuse(s->session);
   s->session=fa->Clone();
}
void ConnectionSlot::SetCwd(const char *n,const char *cwd)
{
   ConnectionSlot::SlotValue *s=Find(n);
   if(!s)
      return;
   FileAccess *fa=s->session;
   if(!fa)
      return;
   fa->SetCwd(cwd);
   s->SetValue(fa->GetConnectURL());
}
FileAccess *ConnectionSlot::FindSession(const char *n)
{
   ConnectionSlot::SlotValue *s=Find(n);
   return s?s->session:0;
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
