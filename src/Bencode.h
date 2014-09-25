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

#ifndef BENCODE_H
#define BENCODE_H

#include "xmap.h"
#include "buffer.h"

class BeNode
{
   void PackDict(xstring &buf);
public:
   enum be_type_t {
      BE_STR,
      BE_INT,
      BE_LIST,
      BE_DICT
   } type;

   xstring str;
   xstring str_lc;
   xarray_p<BeNode> list;
   xmap_p<BeNode> dict;
   long long num;

   static BeNode *Parse(const char *s,int len,int *rest);

   BeNode(const xstring& s);
   BeNode(const char *s,int l);
   BeNode(const char *s);
   BeNode(xarray_p<BeNode> *l);
   BeNode(xmap_p<BeNode> *d);
   BeNode(long long);
   ~BeNode();

   BeNode *lookup(const char *key) {
      return dict.lookup(key);
   }
   BeNode *lookup(const char *key,be_type_t t) {
      BeNode *n=dict.lookup(key);
      if(n && n->type!=t)
	 n=0;
      return n;
   }
   const xstring& lookup_str(const char *key) const {
      BeNode *n=dict.lookup(key);
      if(!n || n->type!=BE_STR)
	 return xstring::null;
      return n->str;
   }
   long long lookup_int(const char *key) const {
      BeNode *n=dict.lookup(key);
      if(!n || n->type!=BE_INT)
	 return 0;
      return n->num;
   }

   int ComputeLength();
   const xstring& Pack();
   void Pack(xstring &buf);
   void Pack(const SMTaskRef<IOBuffer> &buf);

   void Format(xstring &buf,int level);
   const char *Format();
   void Format1(xstring &buf);
   const char *Format1();

   static const char *TypeName(be_type_t t);
};

#endif//BENCODE_H
