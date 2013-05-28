#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <zlib.h>
#include "buffer.h"

class DataInflator : public DataTranslator
{
   z_stream z;
   int z_err;
public:
   DataInflator();
   ~DataInflator();
   void PutTranslated(Buffer *dst,const char *buf,int size);
   void ResetTranslation();
};
