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

#ifndef BUFFER_ZLIB_H
#define BUFFER_ZLIB_H

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

class DataDeflator : public DataTranslator
{
   z_stream z;
   int z_err;
public:
   DataDeflator(int level=Z_DEFAULT_COMPRESSION);
   ~DataDeflator();
   void PutTranslated(Buffer *dst,const char *buf,int size);
   void ResetTranslation();
};

#endif //BUFFER_ZLIB_H
