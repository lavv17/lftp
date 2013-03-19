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
#include "Cache.h"

void Cache::Trim()
{
   long sizelimit=res_max_size->Query(0);

   long size=0;
   CacheEntry **scan=&chain;
   while(scan[0])
   {
      if(scan[0]->Stopped())
	 delete replace_value(scan[0],scan[0]->next);
      else
      {
	 size+=scan[0]->EstimateSize();
	 scan=&scan[0]->next;
      }
   }
// printf("Cache::Trim size=%ld, limit=%ld\n",size,sizelimit);
   while(chain && size>sizelimit)
   {
      size-=chain->EstimateSize();
      delete replace_value(chain,chain->next);
   }
}
void Cache::Flush()
{
   while(chain)
      delete replace_value(chain,chain->next);
}
CacheEntry *Cache::IterateFirst()
{
   curr=&chain;
   return *curr;
}
CacheEntry *Cache::IterateNext()
{
   curr=&curr[0]->next;
   return *curr;
}
CacheEntry *Cache::IterateDelete()
{
   delete replace_value(curr[0],curr[0]->next);
   return *curr;
}
