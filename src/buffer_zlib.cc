/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "buffer_zlib.h"

void DataInflator::PutTranslated(Buffer *target,const char *put_buf,int size)
{
   bool from_untranslated=false;
   if(Size()>0)
   {
      Put(put_buf,size);
      Get(&put_buf,&size);
      from_untranslated=true;
   }
   // process all data we can, save the rest in the untranslated buffer
   while(size>0)
   {
      if(z_err==Z_STREAM_END)
      {
	 // assume the data after the compressed stream are not compressed.
	 target->Put(put_buf,size);
	 if(from_untranslated)
	    Skip(size);
	 return;
      }
      size_t put_size=size;
      int size_coeff=6;
      size_t store_size=size_coeff*put_size;
      char *store_space=target->GetSpace(store_size);
      char *store_buf=store_space;
      // do the inflation
      z.next_in=(Bytef*)put_buf;
      z.avail_in=put_size;
      z.next_out=(Bytef*)store_buf;
      z.avail_out=store_size;
      int ret = inflate(&z, Z_NO_FLUSH);
      assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
      switch (ret) {
      case Z_NEED_DICT:
	 ret = Z_DATA_ERROR;     /* and fall through */
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
	 z_err=ret;
	 target->SetError(xstring::cat("zlib inflate error: ",z.msg,NULL),true);
	 return;
      case Z_STREAM_END:
	 z_err=ret;
	 break;
      }
      int inflated_size=store_size-z.avail_out;
      int processed_size=put_size-z.avail_in;

      target->SpaceAdd(inflated_size);
      if(from_untranslated) {
	 Skip(processed_size);
	 Get(&put_buf,&size);
      } else {
	 put_buf+=processed_size;
	 size-=processed_size;
      }
      if(inflated_size==0) {
	 // could not inflate any data, save unprocessed data
	 if(!from_untranslated)
	    Put(put_buf,size);
	 return;
      }
   }
}

DataInflator::DataInflator()
{
   /* allocate inflate state */
   z.zalloc = Z_NULL;
   z.zfree = Z_NULL;
   z.opaque = Z_NULL;
   z.avail_in = 0;
   z.next_in = Z_NULL;
   z_err = inflateInit2(&z, 16+MAX_WBITS);
}
DataInflator::~DataInflator()
{
   (void)inflateEnd(&z);
}
void DataInflator::ResetTranslation()
{
   z_err = inflateReset(&z);
}
