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

/* $Id: Bencode.cc,v 1.1 2011/08/01 11:07:04 lav Exp $ */

#include <config.h>
#include <stdio.h>
#include "Bencode.h"
#include "c-ctype.h"

BeNode::BeNode(long long n)
   : type(BE_INT),num(n)
{}
BeNode::BeNode(const char *s,int len)
   : type(BE_STR),str(s,len)
{}
BeNode::BeNode(const char *s)
   : type(BE_STR),str(s)
{}
BeNode::BeNode(xarray_p<BeNode> *a)
   : type(BE_LIST)
{
   list.move_here(*a);
}
BeNode::BeNode(xmap_p<BeNode> *m)
   : type(BE_DICT)
{
   dict.move_here(*m);
}

BeNode *BeNode::Parse(const char *s,int s_len,int *rest)
{
   if(s_len<2) {
      *rest=0;
      return 0;
   }
   switch(*s)
   {
   case 'i':
   {
      s++;
      s_len--;
      bool neg=false;
      if(*s=='-') {
	 neg=true;
	 s++;
	 s_len--;
      }
      if(s_len<2) {
	 *rest=0;
	 return 0;
      }
      if(c_isdigit(*s))
      {
	 if(*s=='0' && s[1]!='e') {
	    *rest=s_len;
	    return 0;
	 }
	 long long n=*s++-'0';
	 s_len--;
	 while(s_len>1 && c_isdigit(*s)) {
	    n=n*10+*s++-'0';
	    s_len--;
	 }
	 if(s_len<1 || *s!='e') {
	    *rest=s_len;
	    return 0;
	 }
	 *rest=s_len-1;
	 return new BeNode(neg?-n:n);
      }
      *rest=s_len;
      return 0;
   }
   case 'l':
   {
      s++;
      s_len--;
      xarray_p<BeNode> a;
      while(s_len>1 && *s!='e')
      {
	 int rest1;
	 BeNode *n=Parse(s,s_len,&rest1);
	 if(!n) {
	    *rest=rest1;
	    return 0;
	 }
	 a.append(n);
	 s+=(s_len-rest1);
	 s_len=rest1;
      }
      if(s_len<1 || *s!='e') {
	 *rest=s_len;
	 return 0;
      }
      *rest=s_len-1;
      return new BeNode(&a);
   }
   case 'd':
   {
      const char *d_begin=s;
      s++;
      s_len--;
      xmap_p<BeNode> map;
      while(s_len>1 && *s!='e')
      {
	 int rest1;
	 BeNode *n=Parse(s,s_len,&rest1);
	 if(!n) {
	    *rest=rest1;
	    return 0;
	 }
	 if(n->type!=BE_STR) {
	    *rest=s_len;
	    return 0;
	 }
	 s+=(s_len-rest1);
	 s_len=rest1;
	 BeNode *v=Parse(s,s_len,&rest1);
	 if(!v) {
	    *rest=rest1;
	    return 0;
	 }
	 map.add(n->str,v);
	 delete n;
	 s+=(s_len-rest1);
	 s_len=rest1;
      }
      if(s_len<1 || *s!='e') {
	 *rest=s_len;
	 return 0;
      }
      s++;
      s_len--;
      *rest=s_len;
      BeNode *node=new BeNode(&map);
      node->str.nset(d_begin,s-d_begin);
      return node;
   }
   default:
      if(c_isdigit(*s))
      {
	 int n=*s++-'0';
	 s_len--;
	 while(s_len>0 && c_isdigit(*s)) {
	    if(n>=s_len) {
	       *rest=0;
	       return 0;
	    }
	    n=n*10+*s++-'0';
	    s_len--;
	 }
	 if(s_len<1 || *s!=':') {
	    *rest=s_len;
	    return 0;
	 }
	 s++;
	 s_len--;
	 if(s_len<n) {
	    *rest=0;
	    return 0;
	 }
	 *rest=s_len-n;
	 return new BeNode(s,n);
      }
      *rest=s_len;
      return 0;
   }
}

BeNode::~BeNode()
{
   for(int i=0; i<list.count(); i++) {
      delete list[i];
      list[i]=0;
   }
   for(BeNode *e=dict.each_begin(); e; e=dict.each_next())
   {
      delete e;
      dict.each_set(0);
   }
}

void BeNode::Format(xstring &buf,int level)
{
   int i;
   for(i=0; i<level; i++)
      buf.append('\t');
   switch(type)
   {
   case BE_STR:
      buf.append("STR: ");
      (str_lc?str_lc:str).dump_to(buf);
      buf.append("\n");
      break;
   case BE_INT:
      buf.appendf("INT: %lld\n",num);
      break;
   case BE_LIST:
      buf.appendf("LIST: %d items\n",list.count());
      for(i=0; i<list.count(); i++)
	 list[i]->Format(buf,level+1);
      break;
   case BE_DICT:
      buf.appendf("DICT: %d items\n",dict.count());
      for(BeNode *e=dict.each_begin(); e; e=dict.each_next())
      {
	 for(i=0; i<level+1; i++)
	    buf.append('\t');
	 buf.appendf("KEY=%s:\n",dict.each_key()->get());
	 e->Format(buf,level+2);
      }
      break;
   }
}

const char *BeNode::Format()
{
   static xstring buf;
   buf.set("");
   Format(buf,0);
   return buf;
}

void BeNode::Format1(xstring &buf)
{
   int i;
   switch(type)
   {
   case BE_STR:
      buf.append('"');
      (str_lc?str_lc:str).dump_to(buf);
      buf.append('"');
      break;
   case BE_INT:
      buf.appendf("%lld",num);
      break;
   case BE_LIST:
      buf.append('(');
      for(i=0; i<list.count(); i++) {
	 if(i>0)
	    buf.append(", ");
	 list[i]->Format1(buf);
      }
      buf.append(')');
      break;
   case BE_DICT:
      buf.append('{');
      i=0;
      for(BeNode *e=dict.each_begin(); e; e=dict.each_next(), i++)
      {
	 if(i>0)
	    buf.append(", ");
	 buf.appendf("\"%s\":",dict.each_key()->get());
	 e->Format1(buf);
      }
      buf.append('}');
      break;
   }
}
const char *BeNode::Format1()
{
   static xstring buf;
   buf.set("");
   Format1(buf);
   return buf;
}

const char *BeNode::TypeName(be_type_t t)
{
   static const char *table[]={
      "STR",
      "INT",
      "LIST",
      "DICT"
   };
   return table[t];
}

int BeNode::ComputeLength()
{
   int len=0;
   int i;
   switch(type)
   {
   case BE_STR:
      i=str.length();
      len+=1+i; // ':' + string
      while(i>=10) {
	 len++;
	 i/=10;
      }
      len++; // last digit
      break;
   case BE_INT:
      len+=1+xstring::format("%lld",num).length()+1; // 'i' + number + 'e'
      break;
   case BE_LIST:
      len++; // 'l'
      for(i=0; i<list.count(); i++)
	 len+=list[i]->ComputeLength();
      len++; // 'e'
      break;
   case BE_DICT:
      len++; // 'd'
      for(BeNode *e=dict.each_begin(); e; e=dict.each_next())
      {
	 const xstring &key=*dict.each_key();
	 i=key.length();
	 len+=1+i; // ':' + string
	 while(i>=10) {
	    len++;
	    i/=10;
	 }
	 len++; // last digit
	 len+=e->ComputeLength();
      }
      len++; // 'e'
      break;
   }
   return len;
}

void BeNode::Pack(Ref<IOBuffer> &buf)
{
   int i;
   switch(type)
   {
   case BE_STR:
      i=str.length();
      buf->Format("%d:",i);
      buf->Put(str);
      break;
   case BE_INT:
      buf->Format("i%llde",num);
      break;
   case BE_LIST:
      buf->Put('l');
      for(i=0; i<list.count(); i++)
	 list[i]->Pack(buf);
      buf->Put('e');
      break;
   case BE_DICT:
      buf->Put('l');
      for(BeNode *e=dict.each_begin(); e; e=dict.each_next())
      {
	 const xstring &key=*dict.each_key();
	 i=key.length();
	 buf->Format("%d:",i);
	 buf->Put(key);
	 e->Pack(buf);
      }
      buf->Put('e');
      break;
   }
}
