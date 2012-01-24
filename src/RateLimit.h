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

/* $Id: RateLimit.h,v 1.4 2009/07/28 11:17:38 lav Exp $ */

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
   bool Relaxed(dir_t dir);

   void Reconfig(const char *name,const char *c);
};

#endif // RATELIMIT_H
