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
   if(size<=0)
      return;
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
   if(put_size==0)
      return;
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
   target->SpaceAdd(store_size-z.avail_out);
   if(from_untranslated)
      Skip(put_size-z.avail_in);
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
   z_err = inflateReset2(&z, 16+MAX_WBITS);
}
