#include <config.h>
#include "FileAccess.h"
#include "ConnectionSlot.h"

ConnectionSlot ConnectionSlot::lftp_slots;

ConnectionSlot::SlotValue::SlotValue(const char *n,FileAccess *s)
   : KeyValueDB::Pair(n,s->GetConnectURL())
{
   session=s;
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
   FileAccess *fa=FindSession(n);
   if(!fa)
      return;
   fa->SetCwd(cwd);
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
