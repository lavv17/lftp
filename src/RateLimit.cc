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

xmap_p<RateLimit> *RateLimit::total;

void RateLimit::AddXfer(int add)
{
   xfer_number+=add;
   assert(xfer_number>=0);
   if(parent)
      parent->AddXfer(add);
}

void RateLimit::init(level_e lvl,const char *c)
{
   level=lvl;
   xfer_number=(level==PER_CONN?1:0);
   parent=0;
   Reconfig(0,c);

   if(level==TOTAL) // has no parent
      return;

   level_e parent_level=level_e(level+1);
   if(parent_level==TOTAL)
      c=""; // no closure on top level
   xstring parent_key(c);

   if(!total)
      total=new xmap_p<RateLimit>();
   if(total->exists(parent_key)) {
      parent=total->lookup(parent_key);
      if(parent->xfer_number==0)
	 parent->Reconfig(0,c); // it was not used for a white, refresh config
   } else {
      parent=new RateLimit(parent_level,c);
      total->add(parent_key,parent);
   }
   parent->AddXfer(xfer_number);
}
RateLimit::~RateLimit()
{
   if(parent && xfer_number)
      parent->AddXfer(-xfer_number);
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
   int parent_allowed = parent?parent->BytesAllowed(dir):LARGE;

   if(pool[dir].rate==0) // unlimited
      return parent_allowed;

   pool[dir].AdjustTime();

   int allowed = pool[dir].pool/xfer_number;
   if(allowed>parent_allowed)
      allowed=parent_allowed;

   return allowed;
}

bool RateLimit::Relaxed(dir_t dir)
{
   bool parent_relaxed = parent?parent->Relaxed(dir):true;

   if(pool[dir].rate==0) // unlimited
      return parent_relaxed;

   pool[dir].AdjustTime();

   if(pool[dir].rate>0 && pool[dir].pool < pool[dir].pool_max/2)
      return false;
   return parent_relaxed;
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
   if(parent)
      parent->BytesUsed(bytes,dir);
   pool[dir].Used(bytes);
}

void RateLimit::Reset()
{
   pool[GET].Reset();
   pool[PUT].Reset();
}

void RateLimit::BytesPool::Reset()
{
   pool=rate;
   t=SMTask::now;
}
void RateLimit::Reconfig(const char *name,const char *c)
{
   if(name && strncmp(name,"net:limit-",10))
      return; // not relevant

   bool config_total=(!name || !strncmp(name,"net:limit-total-",16));

   const char *setting_rate="net:limit-rate";
   const char *setting_max="net:limit-max";

   if(level>PER_CONN)
   {
      if(!config_total)
	 return; // not relevant
      if(level==TOTAL)
	 c=0; // aggregates everything

      setting_rate="net:limit-total-rate";
      setting_max="net:limit-total-max";
   }

   ResMgr::Query(setting_rate,c).ToNumberPair(pool[GET].rate,pool[PUT].rate);
   ResMgr::Query(setting_max,c).ToNumberPair(pool[GET].pool_max,pool[PUT].pool_max);

   if(pool[GET].pool_max==0)
      pool[GET].pool_max=pool[GET].rate*DEFAULT_MAX_COEFF;
   if(pool[PUT].pool_max==0)
      pool[PUT].pool_max=pool[PUT].rate*DEFAULT_MAX_COEFF;
   Reset();

   if(config_total && parent)
      parent->Reconfig(name,c);
}

int RateLimit::LimitBufferSize(int size,dir_t d) const
{
   if(pool[d].rate!=0 && size>pool[d].pool_max)
      size=pool[d].pool_max;
   return size;
}
void RateLimit::SetBufferSize(IOBuffer *buf,int size) const
{
   dir_t d = (buf->GetDirection()==buf->GET ? GET : PUT);
   buf->SetMaxBuffered(LimitBufferSize(size,d));
}

void RateLimit::ClassCleanup()
{
   if(!total)
      return;
   for(RateLimit *t=total->each_begin(); t; t=total->each_next())
      t->parent=0;
   delete total; total=0;
}
