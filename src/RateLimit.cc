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
#include "RateLimit.h"
#include "ResMgr.h"
#include "SMTask.h"

// RateLimit class implementation.
int RateLimit::total_xfer_number;
RateLimit::BytesPool RateLimit::total[2];
bool RateLimit::total_reconfig_needed=true;

RateLimit::RateLimit(const char *c)
{
   if(total_xfer_number==0)
   {
      total[GET].Reset();
      total[PUT].Reset();
   }
   total_xfer_number++;
   Reconfig(0,c);
}
RateLimit::~RateLimit()
{
   total_xfer_number--;
}

#define LARGE 0x10000000
#define DEFAULT_MAX_COEFF 2
void RateLimit::BytesPool::AdjustTime()
{
   double dif=TimeDiff(SMTask::now,t);

   if(dif>0)
   {
      // prevent overflow
      if((LARGE-pool)/dif < rate)
	 pool = pool_max;
      else
	 pool += int(dif*rate+0.5);

      if(pool>pool_max)
	 pool=pool_max;

      t=SMTask::now;
   }
}

int RateLimit::BytesAllowed(dir_t dir)
{
   if(total_reconfig_needed)
      ReconfigTotal();

   if(one[dir].rate==0 && total[dir].rate==0) // unlimited
      return LARGE;

   one  [dir].AdjustTime();
   total[dir].AdjustTime();

   int ret=LARGE;
   if(total[dir].rate>0)
      ret=total[dir].pool/total_xfer_number;
   if(one[dir].rate>0 && ret>one[dir].pool)
      ret=one[dir].pool;
   return ret;
}

bool RateLimit::Relaxed(dir_t dir)
{
   if(total_reconfig_needed)
      ReconfigTotal();

   if(one[dir].rate==0 && total[dir].rate==0) // unlimited
      return true;
   one  [dir].AdjustTime();
   total[dir].AdjustTime();

   if(total[dir].rate>0 && total[dir].pool < total[dir].pool_max/2)
      return false;
   if(one[dir].rate>0 && one[dir].pool < one[dir].pool_max/2)
      return false;
   return true;
}

void RateLimit::BytesPool::Used(int bytes)
{
   if(pool<bytes)
      pool=0;
   else
      pool-=bytes;
}

void RateLimit::BytesUsed(int bytes,dir_t dir)
{
   total[dir].Used(bytes);
   one  [dir].Used(bytes);
}

void RateLimit::BytesPool::Reset()
{
   pool=rate;
   t=SMTask::now;
}
void RateLimit::Reconfig(const char *name,const char *c)
{
   if(name && strncmp(name,"net:limit-",10))
      return;
   ResMgr::Query("net:limit-rate",c).ToNumberPair(one[GET].rate,one[PUT].rate);
   ResMgr::Query("net:limit-max",c) .ToNumberPair(one[GET].pool_max,one[PUT].pool_max);
   if(one[GET].pool_max==0)
      one[GET].pool_max=one[GET].rate*DEFAULT_MAX_COEFF;
   if(one[PUT].pool_max==0)
      one[PUT].pool_max=one[PUT].rate*DEFAULT_MAX_COEFF;
   one[GET].Reset(); // to cut bytes_pool.
   one[PUT].Reset();

   if(name && !strncmp(name,"net:limit-total-",16))
      total_reconfig_needed=true;
}
void RateLimit::ReconfigTotal()
{
   ResMgr::Query("net:limit-total-rate",0).ToNumberPair(total[GET].rate,total[PUT].rate);
   ResMgr::Query("net:limit-total-max",0) .ToNumberPair(total[GET].pool_max,total[PUT].pool_max);
   if(total[GET].pool_max==0)
      total[GET].pool_max=total[GET].rate*DEFAULT_MAX_COEFF;
   if(total[PUT].pool_max==0)
      total[PUT].pool_max=total[PUT].rate*DEFAULT_MAX_COEFF;
   total[GET].Reset();
   total[PUT].Reset();
   total_reconfig_needed = false;
}
