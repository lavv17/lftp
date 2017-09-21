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

#ifndef RATELIMIT_H
#define RATELIMIT_H

#include "TimeDate.h"
#include "buffer.h"

class RateLimit
{
public:
   class BytesPool
   {
      friend class RateLimit;

      int pool;
      int rate;
      int pool_max;
      Time t;

      void AdjustTime();
      void Reset();
      void Used(int);
   };

private:
   static xmap_p<RateLimit> *total;

   enum level_e { PER_CONN, PER_HOST, TOTAL } level;
   RateLimit *parent;
   int xfer_number;
   BytesPool pool[2];

   void init(level_e lvl,const char *closure);
   RateLimit(level_e lvl,const char *closure) { init(lvl,closure); }

   void AddXfer(int add);

public:
   RateLimit(const char *closure) { init(PER_CONN,closure); }
   ~RateLimit();

   enum dir_t { GET=0, PUT=1 };

   int BytesAllowed(dir_t how);
   int BytesAllowedToGet() { return BytesAllowed(GET); }
   int BytesAllowedToPut() { return BytesAllowed(PUT); }
   void BytesUsed(int,dir_t);
   void BytesGot(int b) { BytesUsed(b,GET); }
   void BytesPut(int b) { BytesUsed(b,PUT); }
   bool Relaxed(dir_t dir);
   void Reset();

   void Reconfig(const char *name,const char *c);

   int LimitBufferSize(int size,dir_t d) const;
   void SetBufferSize(IOBuffer *buf,int size) const;
   void SetBufferSize(SMTaskRef<IOBuffer>& buf,int size) const { SetBufferSize(buf.get_non_const(),size); }

   static void ClassCleanup();
};

#endif // RATELIMIT_H
