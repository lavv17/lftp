/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef RATELIMIT_H
#define RATELIMIT_H

#include "TimeDate.h"

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
   static int total_xfer_number;
   static bool total_reconfig_needed;
   static void ReconfigTotal();
   static BytesPool total[2];
   BytesPool one[2];

public:
   RateLimit(const char *closure);
   ~RateLimit();

   enum dir_t { GET=0, PUT=1 };

   int BytesAllowed(dir_t how);
   int BytesAllowedToGet() { return BytesAllowed(GET); }
   int BytesAllowedToPut() { return BytesAllowed(PUT); }
   void BytesUsed(int,dir_t);
   void BytesGot(int b) { BytesUsed(b,GET); }
   void BytesPut(int b) { BytesUsed(b,PUT); }

   void Reconfig(const char *name,const char *c);
};

#endif // RATELIMIT_H
