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

#include <config.h>
#include "HttpHeader.h"

const xstring& HttpHeader::extract_quoted_value(const char *value,const char **p_end)
{
   static xstring value1;
   if(*value=='"')
   {
      value1.truncate();
      value++;
      while(*value && *value!='"')
      {
	 if(*value=='\\' && value[1])
	    value++;
	 value1.append(*value++);
      }
      if(p_end)
	 *p_end=(*value=='"'?value+1:value);
   }
   else
   {
      int end=strcspn(value,"()<>@,;:\\\"/[]?={} \t");
      value1.nset(value,end);
      if(p_end)
	 *p_end=value+end;
   }
   return value1;
}

xstring& HttpHeader::append_quoted_value(xstring& s,const char *v)
{
   s.append('"');
   while(*v) {
      if(*v=='\\' || *v=='"')
	 s.append('\\');
      s.append(*v++);
   }
   return s.append('"');
}
