/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#ifndef RESMGR_H
#define RESMGR_H

#include "trio.h"
#include <sys/types.h>
#include <time.h>
#include "xmalloc.h"
#include "TimeDate.h"

typedef const char *ResValValid(char **value);
typedef const char *ResClValid(char **closure);

class ResValue;

struct ResType
{
   const char *name;
   const char *defvalue;
   ResValValid *val_valid;
   ResClValid *closure_valid;
   ResType *next;
   ~ResType();

   ResValue Query(const char *closure) const;
   bool QueryBool(const char *closure) const;
};

class ResMgr
{
   static bool class_inited;
   friend class ResType;
public:
   class Resource
   {
      friend class ResMgr;
      friend class ResType;

      const ResType *type;
      char *value;
      char *closure;

      Resource *next;

      bool ClosureMatch(const char *cl_data);

      Resource(Resource *next,const ResType *type,
	       char *closure,char *value)
      {
	 this->type=type;
	 this->value=value;
	 this->closure=closure;
	 this->next=next;
      }
      ~Resource()
      {
	 xfree(closure);
	 xfree(value);
      }
   };

private:
   static Resource *chain;
   static ResType *type_chain;

public:
   static void AddType(ResType *t) { t->next=type_chain; type_chain=t; }
   static const char *QueryNext(const char *name,const char **closure,Resource **ptr);
   static const char *SimpleQuery(const ResType *type,const char *closure);
   static const char *SimpleQuery(const char *name,const char *closure);
   static ResValue Query(const char *name,const char *closure);
   static bool QueryBool(const char *name,const char *closure);

   enum CmpRes {
      EXACT_PREFIX=0x00,SUBSTR_PREFIX=0x01,
      EXACT_NAME  =0x00,SUBSTR_NAME  =0x10,
      DIFFERENT=-1
   };

   static int VarNameCmp(const char *name1,const char *name2);
   static const char *FindVar(const char *name,const ResType **type);
   static const ResType *FindRes(const char *name);
   static const char *Set(const char *name,const char *closure,const char *value);

   static char *Format(bool with_defaults,bool only_defaults);
   static char **Generator(void);

   static const char *BoolValidate(char **value);
   static const char *TriBoolValidate(char **value);
   static const char *NumberValidate(char **value);
   static const char *UNumberValidate(char **value);
   static const char *FloatValidate(char **value);
   static const char *TimeIntervalValidate(char **value);
   static const char *RangeValidate(char **value);
   static const char *ERegExpValidate(char **value);
   static const char *IPv4AddrValidate(char **value);
#if INET6
   static const char *IPv6AddrValidate(char **value);
#endif
   static const char *UNumberPairValidate(char **value);
   static const char *FileAccessible(char **value,int mode,int want_dir=0);
   static const char *FileReadable(char **value);
   static const char *FileExecutable(char **value);
   static const char *DirReadable(char **value);
   static const char *CharsetValidate(char **value);
   static const char *NoClosure(char **);
   static bool str2bool(const char *value);

   static void ClassInit();

   static int ResourceCompare(const Resource *a,const Resource *b);
   static int VResourceCompare(const void *a,const void *b);
};

class ResDecl : public ResType
{
public:
   ResDecl(const char *a_name,const char *a_defvalue,
	   ResValValid *a_val_valid,ResClValid *a_closure_valid=0);
};
class ResDecls
{
public:
   ResDecls(ResType *array);
   ResDecls(ResType *r1,ResType *r2,...);
};

class ResValue
{
   const char *s;
public:
   ResValue(const char *s_new)
      {
	 s=s_new;
      }
   bool to_bool()
      {
	 return ResMgr::str2bool(s);
      }
   operator int()
      {
	 return strtol(s,0,0);
      }
   operator long()
      {
	 return strtol(s,0,0);
      }
   operator double()
      {
	 return atof(s);
      }
   operator float()
      {
	 return atof(s);
      }
   operator const char*()
      {
	 return s;
      }
   bool is_nil() { return s==0; }
   void ToNumberPair(int &a,int &b)
      {
	 switch(sscanf(s,"%d%*c%d",&a,&b))
	 {
	 case 0: a=0;
	 case 1: b=a;
	 }
      }
};

inline bool ResType::QueryBool(const char *closure) const
{
   return Query(closure).to_bool();
}
inline bool ResMgr::QueryBool(const char *name,const char *closure)
{
   return Query(name,closure).to_bool();
}

class TimeIntervalR : public TimeInterval
{
   const char *error_text;
   void init(const char *);
public:
   void Set(const char *s) { init(s); }
   TimeIntervalR() { error_text=0; }
   TimeIntervalR(const char *s) : TimeInterval(0,0) { init(s); }
   TimeIntervalR(ResValue r) : TimeInterval(0,0) { init(r); }
   TimeIntervalR(time_t s,int ms=0) : TimeInterval(s,ms) { error_text=0; }
   TimeIntervalR(const TimeDiff &d) : TimeInterval(d) { error_text=0; }
   bool Error() const { return error_text!=0; };
   const char *ErrorText() const { return error_text; }
};

class Range
{
   long long start,end;
   bool no_start,no_end;
   const char *error_text;

   static const char *scale(long long *value,char suf);

public:
   Range(const char *s);
   bool Match(long long n) const { return (no_start || n>=start) && (no_end || n<=end); }
   bool IsFull() { return no_start && no_end; }
   long long Random();
   bool Error() { return error_text!=0; };
   const char *ErrorText() { return error_text; }
};

#endif //RESMGR_H
