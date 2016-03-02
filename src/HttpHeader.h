/*
 * lftp - file transfer program
 *
 * Copyright (c) 2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef HTTPHEADER_H
#define HTTPHEADER_H

#include "xmap.h"
#include "buffer.h"

class HttpHeader {
   xstring name;
   xstring value;
public:
   HttpHeader(const char *p_name) : name(p_name) {}
   void SetValue(const xstring& v) { value.set(v); }
   const char *GetName() const { return name; }
   const char *GetValue() const { return value; }

   static const xstring& extract_quoted_value(const char *value,const char **p_end=0);
   static xstring& append_quoted_value(xstring& s,const char *v);
};

#endif//HTTPHEADER_H
