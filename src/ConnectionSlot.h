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
      FileAccess *session;
      SlotValue(const char *n,FileAccess *s);
      SlotValue(const char *n,const char *v);
      ~SlotValue();
   };

   Pair *NewPair(const char *n,const char *v)
      {
	 return new SlotValue(n,v);
      }

   ~ConnectionSlot() {}

public:
   static ConnectionSlot::SlotValue *Find(const char *n);
   static FileAccess *FindSession(const char *n);
   static void Set(const char *n,FileAccess *s);
   static void SetCwd(const char *n,const char *cwd);
   static void Remove(const char *n);
   static char *Format();
   char *FormatThis();
   static void Cleanup();
};

#endif
