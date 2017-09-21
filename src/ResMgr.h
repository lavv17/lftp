/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef RESMGR_H
#define RESMGR_H

#include "trio.h"
#include <sys/types.h>
#include <time.h>
#include "TimeDate.h"
#include "xstring.h"
#include "xlist.h"
#include "xmap.h"

typedef const char *ResValValid(xstring_c *value);
typedef const char *ResClValid(xstring_c *closure);

class ResValue;
class ResMgr;
class Resource;

struct ResType
{
   static bool class_inited;
   static xmap<ResType*> *types_by_name;

   const char *name;
   const char *defvalue;
   ResValValid *val_valid;
   ResClValid *closure_valid;
   xlist_head<Resource> *type_value_list;

   const char *SimpleQuery(const char *closure) const;
   ResValue Query(const char *closure) const;
   bool QueryBool(const char *closure) const;
   bool QueryTriBool(const char *closure,bool a) const;

   bool IsAlias() const;
   const char *GetAliasTarget() const { return defvalue; }

   const char *Set(const char *cclosure,const char *cvalue,bool def=false);
   static const char *Set(const char *name,const char *closure,const char *value,bool def=false);
   static const char *SetDefault(const char *name,const char *closure,const char *value)
      { return Set(name,closure,value,true); }

   void Register();
   void Unregister();

   static char *Format(bool with_defaults,bool only_defaults);
   static char **Generator(void);

   static void ClassInit();
   static void ClassCleanup();

   enum CmpRes {
      EXACT_PREFIX=0x00,SUBSTR_PREFIX=0x01,
      EXACT_NAME  =0x00,SUBSTR_NAME  =0x10,
      DIFFERENT=-1
   };
   static int VarNameCmp(const char *name1,const char *name2);
   static const char *FindVar(const char *name,const ResType **type,const char **re_closure=0);
   static const char *FindVar(const char *name,ResType **type,const char **re_closure=0) { return FindVar(name,const_cast<const ResType **>(type),re_closure); }
   static const ResType *FindRes(const char *name);
};

class Resource
{
   friend class ResMgr;
   friend struct ResType;
   static xlist_head<Resource> all_list;

   const ResType *type;
   xstring_c value;
   xstring_c closure;
   bool def;

   xlist<Resource> all_node;
   xlist<Resource> type_value_node;

   bool ClosureMatch(const char *cl_data);
   void Format(xstring& buf) const;

   Resource(ResType *type,const char *closure,const char *value,bool def=false);
public:
   ~Resource();
};

class ResMgr : public ResType
{
   ResMgr();
public:
   static const char *QueryNext(const char *name,const char **closure,Resource **ptr);
   static ResValue Query(const char *name,const char *closure);
   static bool QueryBool(const char *name,const char *closure);
   static bool QueryTriBool(const char *name,const char *closure,bool a);

   static const char *BoolValidate(xstring_c *value);
   static const char *TriBoolValidate(xstring_c *value);
   static const char *NumberValidate(xstring_c *value);
   static const char *UNumberValidate(xstring_c *value);
   static const char *FloatValidate(xstring_c *value);
   static const char *TimeIntervalValidate(xstring_c *value);
   static const char *RangeValidate(xstring_c *value);
   static const char *ERegExpValidate(xstring_c *value);
   static const char *IPv4AddrValidate(xstring_c *value);
   static const char *IPv6AddrValidate(xstring_c *value);
   static const char *UNumberPairValidate(xstring_c *value);
   static const char *FileAccessible(xstring_c *value,int mode,bool want_dir=false);
   static const char *FileReadable(xstring_c *value);
   static const char *FileExecutable(xstring_c *value);
   static const char *DirReadable(xstring_c *value);
   static const char *FileCreatable(xstring_c *value);
   static const char *CharsetValidate(xstring_c *value);
   static const char *NoClosure(xstring_c *);
   static const char *HasClosure(xstring_c *);
   static bool str2bool(const char *value);
   static const char *AliasValidate(xstring_c *);

   static int ResourceCompare(const Resource *a,const Resource *b);
};

class ResDecl : public ResType
{
public:
   ResDecl(const char *a_name,const char *a_defvalue,
	   ResValValid *a_val_valid,ResClValid *a_closure_valid=0);
   ~ResDecl() { Unregister(); }
};
class ResDecls
{
   xarray<ResType*> r;
public:
   ResDecls(ResType *array);
   ResDecls(ResType *r1,ResType *r2,...);
   ~ResDecls();
};

class ResValue
{
   const char *s;
public:
   ResValue(const char *s_new)
      {
	 s=s_new;
      }
   bool to_bool() const
      {
	 return ResMgr::str2bool(s);
      }
   bool to_tri_bool(bool a) const;
   unsigned long long to_unumber(unsigned long long max) const;
   long long to_number(long long min,long long max) const;
   operator int() const;
   operator long() const;
   operator unsigned() const;
   operator unsigned long() const;
   operator double() const { return atof(s); }
   operator float() const  { return atof(s); }
   operator const char*() const
      {
	 return s;
      }
   bool is_nil() const { return s==0; }
   bool is_empty() const { return s==0 || *s==0; }
   void ToNumberPair(int &a,int &b) const;
};

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

class NumberPair
{
protected:
   long long n1,n2;
   bool no_n1,no_n2;
   const char *error_text;
   char sep;

   static const char *scale(long long *value,char suf);
   long long parse1(const char *s);

   void init(char sep,const char *s);

public:
   NumberPair(char sep) { init(sep,0); }
   NumberPair(char sep,const char *s) { init(sep,s); }
   void Set(const char *s);
   bool Error() { return error_text!=0; };
   const char *ErrorText() { return error_text; }
   long long N1() { return n1; }
   long long N2() { return n2; }
   bool HasN1() { return !no_n1; }
   bool HasN2() { return !no_n2; }
};
class Range : public NumberPair
{
public:
   Range(const char *s);
   bool Match(long long n) const { return (no_n1 || n>=n1) && (no_n2 || n<=n2); }
   bool IsFull() { return no_n1 && no_n2; }
   long long Random();
};

class ResClient
{
   static xlist_head<ResClient> list;
   xlist<ResClient> node;
protected:
   virtual const char *ResPrefix() const { return 0; }
   virtual const char *ResClosure() const { return 0; }
   virtual void Reconfig(const char *) {}
   ResValue Query(const char *name,const char *closure=0) const;
   bool QueryBool(const char *name,const char *closure=0) const;
   bool QueryTriBool(const char *name,const char *closure,bool a) const;
   ResClient();
   virtual ~ResClient();
public:
   static void ReconfigAll(const char *);
};

#endif //RESMGR_H
