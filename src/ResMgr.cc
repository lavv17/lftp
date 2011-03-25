/*
 * lftp and utils
 *
 * Copyright (c) 1996-2010 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include <config.h>

#include <fnmatch.h>
#include <ctype.h>
#include <unistd.h>
#include <math.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <limits.h>
CDECL_BEGIN
#include "regex.h"
CDECL_END
#include "ResMgr.h"
#include "SMTask.h"
#include "misc.h"
#include "StringSet.h"

ResMgr::Resource  *ResMgr::chain=0;
ResType		  *ResMgr::type_chain=0;

int ResMgr::VarNameCmp(const char *good_name,const char *name)
{
   int res=EXACT_PREFIX+EXACT_NAME;
   const char *colon=strchr(good_name,':');
   if(colon && !strchr(name,':'))
   {
      good_name=colon+1;
      res|=SUBSTR_PREFIX;
   }
   while(*good_name || *name)
   {
      if(*good_name==*name
      || (*good_name && *name && strchr("-_",*good_name) && strchr("-_",*name)))
      {
	 good_name++;
	 name++;
	 continue;
      }
      if(*name && !*good_name)
	 return DIFFERENT;
      if((!*name && *good_name)
      || (strchr("-_:",*name) && !strchr("-_:",*good_name)))
      {
	 good_name++;
	 if(strchr(name,':'))
	    res|=SUBSTR_PREFIX;
	 else
	    res|=SUBSTR_NAME;
	 continue;
      }
      return DIFFERENT;
   }
   return res;
}

const char *ResMgr::FindVar(const char *name,const ResType **type)
{
   const ResType *exact_proto=0;
   const ResType *exact_name=0;

   *type=0;

   int sub=0;
   const ResType *type_scan;
   for(type_scan=type_chain; type_scan; type_scan=type_scan->next)
   {
      switch(VarNameCmp(type_scan->name,name))
      {
      case EXACT_PREFIX+EXACT_NAME:
	 *type=type_scan;
	 return 0;
      case EXACT_PREFIX+SUBSTR_NAME:
	 if(!exact_proto && !exact_name)
	    sub=0;
	 exact_proto=*type=type_scan;
	 sub++;
	 break;
      case SUBSTR_PREFIX+EXACT_NAME:
	 if(!exact_proto && !exact_name)
	    sub=0;
	 exact_name=*type=type_scan;
	 sub++;
	 break;
      case SUBSTR_PREFIX+SUBSTR_NAME:
	 if(exact_proto || exact_name)
	    break;
	 sub++;
	 *type=type_scan;
	 break;
      default:
	 break;
      }
   }
   if(!type_scan && sub==0)
      return _("no such variable");
   if(sub==1)
      return 0;
   *type=0;
   return _("ambiguous variable name");
}

const ResType *ResMgr::FindRes(const char *name)
{
   const ResType *type;
   const char *msg=FindVar(name,&type);
   if(msg)
      return 0;
   return type;
}

const char *ResMgr::Set(const char *name,const char *cclosure,const char *cvalue)
{
   const char *msg;

   const ResType *type;
   // find type of given variable
   msg=FindVar(name,&type);
   if(msg)
      return msg;

   xstring_c value(cvalue);
   if(value && type->val_valid && (msg=(*type->val_valid)(&value))!=0)
      return msg;

   xstring_c closure(cclosure);
   if(closure && type->closure_valid && (msg=(*type->closure_valid)(&closure))!=0)
      return msg;

   Resource **scan;
   // find the old value
   for(scan=&chain; *scan; scan=&(*scan)->next)
      if((*scan)->type==type
	 && ((closure==0 && (*scan)->closure==0)
	     || (closure && (*scan)->closure
	         && !strcmp((*scan)->closure,closure))))
	 break;

   // if found
   if(*scan)
   {
      if(value)
	 (*scan)->value.set(value);
      else
      {
	 Resource *to_free=*scan;
	 *scan=(*scan)->next;
	 delete to_free;
      }
      ResClient::ReconfigAll(type->name);
   }
   else
   {
      if(value)
      {
	 chain=new Resource(chain,type,closure,value);
	 ResClient::ReconfigAll(type->name);
      }
   }
   return 0;
}

int ResMgr::ResourceCompare(const Resource *ar,const Resource *br)
{
   int diff=strcmp(ar->type->name,br->type->name);
   if(diff)
      return diff;
   if(ar->closure==br->closure)
      return 0;
   if(ar->closure==0)
      return -1;
   if(br->closure==0)
      return 1;
   return strcmp(ar->closure,br->closure);
}

int ResMgr::VResourceCompare(const void *a,const void *b)
{
   const ResMgr::Resource *ar=*(const ResMgr::Resource*const*)a;
   const ResMgr::Resource *br=*(const ResMgr::Resource*const*)b;
   return ResMgr::ResourceCompare(ar,br);
}

char *ResMgr::Format(bool with_defaults,bool only_defaults)
{
   Resource *scan;
   ResType  *dscan;

   int n=0;
   int dn=0;
   int size=0;
   if(!only_defaults)
   {
      for(scan=chain; scan; scan=scan->next)
      {
	 size+=4+strlen(scan->type->name);
	 if(scan->closure)
	    size+=1+1+2*strlen(scan->closure)+1;
	 size+=1+1+2*strlen(scan->value)+1+1;
	 n++;
      }
   }
   if(with_defaults || only_defaults)
   {
      for(dscan=type_chain; dscan; dscan=dscan->next)
      {
	 size+=4+strlen(dscan->name);
	 size+=1+1+2*xstrlen(dscan->defvalue)+1+1;
	 dn++;
      }
   }

   xstring res("");

   Resource **created=(Resource**)alloca((dn+1)*sizeof(Resource*));
   Resource **c_store=created;
   dn=0;
   if(with_defaults || only_defaults)
   {
      for(dscan=type_chain; dscan; dscan=dscan->next)
      {
	 if(only_defaults || SimpleQuery(dscan->name,0)==0)
	 {
	    dn++;
	    *c_store++=new Resource(0,dscan,
	       0,xstrdup(dscan->defvalue?dscan->defvalue:"(nil)"));
	 }
      }
   }

   Resource **arr=(Resource**)alloca((n+dn)*sizeof(Resource*));
   n=0;
   if(!only_defaults)
   {
      for(scan=chain; scan; scan=scan->next)
	 arr[n++]=scan;
   }
   int i;
   if(with_defaults || only_defaults)
   {
      for(i=0; i<dn; i++)
	 arr[n++]=created[i];
   }

   qsort(arr,n,sizeof(*arr),&ResMgr::VResourceCompare);

   for(i=0; i<n; i++)
   {
      res.appendf("set %s",arr[i]->type->name);
      const char *s=arr[i]->closure;
      if(s)
      {
	 res.append('/');
	 bool par=false;
	 if(strcspn(s," \t>|;&")!=strlen(s))
	    par=true;
	 if(par)
	    res.append('"');
	 while(*s)
	 {
	    if(strchr("\"\\",*s))
	       res.append('\\');
	    res.append(*s++);
	 }
	 if(par)
	    res.append('"');
      }
      res.append(' ');
      s=arr[i]->value;

      bool par=false;
      if(*s==0 || strcspn(s," \t>|;&")!=strlen(s))
	 par=true;
      if(par)
	 res.append('"');
      while(*s)
      {
	 if(strchr("\"\\",*s))
	    res.append('\\');
	 res.append(*s++);
      }
      if(par)
	 res.append('"');
      res.append('\n');
   }
   for(i=0; i<dn; i++)
      delete created[i];
   return res.borrow();
}

char **ResMgr::Generator(void)
{
   Resource *scan;
   ResType  *dscan;

   int n=0;
   int dn=0;
   for(scan=chain; scan; scan=scan->next)
      n++;
   for(dscan=type_chain; dscan; dscan=dscan->next)
      dn++;

   StringSet res;

   Resource **created=(Resource**)alloca((dn+1)*sizeof(Resource*));
   Resource **c_store=created;
   dn=0;
   for(dscan=type_chain; dscan; dscan=dscan->next)
   {
      if(SimpleQuery(dscan->name,0)==0)
      {
         dn++;
	 *c_store++=new Resource(0,dscan,
	     0,xstrdup(dscan->defvalue?dscan->defvalue:"(nil)"));
      }
   }

   Resource **arr=(Resource**)alloca((n+dn)*sizeof(Resource*));
   n=0;
   for(scan=chain; scan; scan=scan->next)
      arr[n++]=scan;

   int i;
   for(i=0; i<dn; i++)
      arr[n++]=created[i];

   qsort(arr,n,sizeof(*arr),&ResMgr::VResourceCompare);

   for(i=0; i<n; i++)
      res.Append(arr[i]->type->name);

   for(i=0; i<dn; i++)
      delete created[i];
   return res.borrow();
}

const char *ResMgr::BoolValidate(xstring_c *value)
{
   const char *v=*value;
   const char *newval=0;

   switch(v[0])
   {
   case 't':   newval="true";	 break;
   case 'T':   newval="True";	 break;
   case 'f':   newval="false";	 break;
   case 'F':   newval="False";	 break;
   case 'y':   newval="yes";	 break;
   case 'Y':   newval="Yes";	 break;
   case 'n':   newval="no";	 break;
   case 'N':   newval="No";	 break;
   case '1':   newval="1";	 break;
   case '0':   newval="0";	 break;
   case '+':   newval="+";	 break;
   case '-':   newval="-";	 break;
   case 'o':   newval=(v[1]=='f' || v[1]=='F')?"off":"on";  break;
   case 'O':   newval=(v[1]=='f' || v[1]=='F')?"Off":"On";  break;
   default:
      return _("invalid boolean value");
   }
   if(strcmp(v,newval))
      value->set(newval);

   return 0;
}

const char *ResMgr::TriBoolValidate(xstring_c *value)
{
   if(!BoolValidate(value))
      return 0;

   /* not bool */
   const char *v=*value;
   const char *newval=0;

   switch(v[0])
   {
   case 'a':   newval="auto";	 break;
   case 'A':   newval="Auto";	 break;
   default:
      return _("invalid boolean/auto value");
   }

   if(strcmp(v,newval))
      value->set(newval);

   return 0;
}

static const char power_letter[] =
{
  0,	/* not used */
  'K',	/* kibi ('k' for kilo is a special case) */
  'M',	/* mega or mebi */
  'G',	/* giga or gibi */
  'T',	/* tera or tebi */
  'P',	/* peta or pebi */
  'E',	/* exa or exbi */
  'Z',	/* zetta or 2**70 */
  'Y'	/* yotta or 2**80 */
};
static unsigned long long get_power_multiplier(char p)
{
   const char *scan=power_letter;
   int scale=1024;
   unsigned long long mul=1;
   p=toupper(p);
   while(scan<power_letter+sizeof(power_letter)) {
      if(p==*scan)
	 return mul;
      mul*=scale;
      scan++;
   }
   return 0;
}

const char *ResMgr::NumberValidate(xstring_c *value)
{
   const char *v=*value;
   const char *end=v;

   (void)strtoll(v,const_cast<char**>(&end),0);
   unsigned long long m=get_power_multiplier(*end);

   if(v==end || m==0 || end[m>1])
      return _("invalid number");

   return 0;
}
const char *ResMgr::FloatValidate(xstring_c *value)
{
   const char *v=*value;
   const char *end=v;

   (void)strtod(v,const_cast<char**>(&end));
   unsigned long long m=get_power_multiplier(*end);

   if(v==end || m==0 || end[m>1])
      return _("invalid floating point number");

   return 0;
}
const char *ResMgr::UNumberValidate(xstring_c *value)
{
   const char *v=*value;
   const char *end=v;

   (void)strtoull(v,const_cast<char**>(&end),0);
   unsigned long long m=get_power_multiplier(*end);

   if(!isdigit((unsigned char)v[0])
   || v==end || m==0 || end[m>1])
      return _("invalid unsigned number");

   return 0;
}
unsigned long long ResValue::to_unumber(unsigned long long max)
{
   if (is_nil())
      return 0;
   const char *end=s;
   unsigned long long v=strtoull(s,const_cast<char**>(&end),0);
   unsigned long long m=get_power_multiplier(*end);
   unsigned long long vm=v*m;
   if(vm/m!=v || vm>max)
      return max;
   return vm;
}
long long ResValue::to_number(long long min,long long max)
{
   if (is_nil())
      return 0;
   const char *end=s;
   long long v=strtoll(s,const_cast<char**>(&end),0);
   long long m=get_power_multiplier(*end);
   long long vm=v*m;
   if(vm/m!=v)
      return v>0?max:min;
   if(vm>max)
      return max;
   if(vm<min)
      return min;
   return vm;
}
ResValue::operator int()
{
   return to_number(INT_MIN,INT_MAX);
}
ResValue::operator long()
{
   return to_number(LONG_MIN,LONG_MAX);
}
ResValue::operator unsigned()
{
   return to_unumber(UINT_MAX);
}
ResValue::operator unsigned long()
{
   return to_unumber(ULONG_MAX);
}

ResMgr::Resource::Resource(Resource *next,const ResType *type,const char *closure,const char *value)
   : type(type), value(value), closure(closure), next(next)
{
}
ResMgr::Resource::~Resource()
{
}

bool ResMgr::Resource::ClosureMatch(const char *cl_data)
{
   if(!closure && !cl_data)
      return true;
   if(!(closure && cl_data))
      return false;
   return 0==fnmatch(closure,cl_data,FNM_PATHNAME)
      || (closure[0]=='*' && closure[1]=='.' && !strcmp(closure+2,cl_data));
}

const char *ResMgr::QueryNext(const char *name,const char **closure,Resource **ptr)
{
   const ResType *type=FindRes(name);
   if(!type)
      return 0;

   if(*ptr==0)
      *ptr=chain;
   else
      *ptr=(*ptr)->next;

   while(*ptr)
   {
      if((*ptr)->type==type)
      {
	 *closure=(*ptr)->closure;
	 return (*ptr)->value;
      }
      *ptr=(*ptr)->next;
   }
   return 0;
}

const char *ResMgr::SimpleQuery(const ResType *type,const char *closure)
{
   // find the value
   for(Resource *scan=chain; scan; scan=scan->next)
      if(scan->type==type && scan->ClosureMatch(closure))
	 return scan->value;
   return 0;
}
const char *ResMgr::SimpleQuery(const char *name,const char *closure)
{
   const ResType *type=FindRes(name);
   if(!type)
      return 0;

   return SimpleQuery(type,closure);
}

ResValue ResMgr::Query(const char *name,const char *closure)
{
   const char *msg;

   const ResType *type;
   // find type of given variable
   msg=FindVar(name,&type);
   if(msg)
   {
      // debug only
      // fprintf(stderr,_("Query of variable `%s' failed: %s\n"),name,msg);
      return 0;
   }

   return type->Query(closure);
}

ResValue ResType::Query(const char *closure) const
{
   const char *v=0;

   if(closure)
      v=ResMgr::SimpleQuery(this,closure);
   if(!v)
      v=ResMgr::SimpleQuery(this,0);
   if(!v)
      v=defvalue;

   return v;
}

bool ResMgr::str2bool(const char *s)
{
   return(strchr("TtYy1+",s[0])!=0 || !strcasecmp(s,"on"));
}

ResDecl::ResDecl(const char *a_name,const char *a_defvalue,ResValValid *a_val_valid,ResClValid *a_closure_valid)
{
   name=a_name;
   defvalue=a_defvalue;
   val_valid=a_val_valid;
   closure_valid=a_closure_valid;
   ResMgr::AddType(this);
}
ResDecls::ResDecls(ResType *array)
{
   while(array->name)
      ResMgr::AddType(array++);
}
ResDecls::ResDecls(ResType *r1,ResType *r2,...)
{
   ResMgr::AddType(r1);
   if(!r2)
      return;
   ResMgr::AddType(r2);
   va_list v;
   va_start(v,r2);
   while((r1=va_arg(v,ResType *))!=0)
      ResMgr::AddType(r1);
   va_end(v);
}

ResType::~ResType()
{
   for(ResType **scan=&ResMgr::type_chain; *scan; scan=&(*scan)->next)
   {
      if(*scan==this)
      {
	 *scan=this->next;
	 break;
      }
   }

   {
      // remove all resources of this type
      bool modified=false;
      ResMgr::Resource **scan=&ResMgr::chain;
      while(*scan)
      {
	 if((*scan)->type==this)
	 {
	    delete replace_value(*scan,(*scan)->next);
	    modified=true;
	 }
	 else
	 {
	    scan=&(*scan)->next;
	 }
      }
#if 0 // this makes trouble at exit.
      if(modified)
	 SMTask::ReconfigAll(this->name);
#endif
   }
}

void TimeIntervalR::init(const char *s)
{
   double interval=0;
   infty=false;
   error_text=0;

   if(!strncasecmp(s,"inf",3)
   || !strcasecmp(s,"forever")
   || !strcasecmp(s,"never"))
   {
      infty=true;
      return;
   }
   int pos=0;
   for(;;)
   {
      double prec;
      char ch='s';
      int pos1=strlen(s+pos);
      int n=sscanf(s+pos,"%lf%c%n",&prec,&ch,&pos1);
      if(n<1)
	 break;
      ch=tolower((unsigned char)ch);
      if(ch=='m')
	 prec*=MINUTE;
      else if(ch=='h')
	 prec*=HOUR;
      else if(ch=='d')
	 prec*=DAY;
      else if(ch!='s')
      {
	 error_text=_("Invalid time unit letter, only [smhd] are allowed.");
	 return;
      }
      interval+=prec;
      pos+=pos1;
   }
   if(pos==0)
   {
      error_text=_("Invalid time format. Format is <time><unit>, e.g. 2h30m.");
      return;
   }
   TimeDiff::Set(interval);
}

const char *ResMgr::TimeIntervalValidate(xstring_c *s)
{
   TimeIntervalR t(*s);
   if(t.Error())
      return t.ErrorText();
   return 0;
}

Range::Range(const char *s)
{
   start=end=0;
   no_start=no_end=true;
   error_text=0;

   if(!strcasecmp(s,"full") || !strcasecmp(s,"any"))
      return;

   int n;
   int len=strlen(s);
   char start_k=0,end_k=0;
   if(sscanf(s,"%lld-%n",&start,&n)==1 && n==len)
      no_start=false;
   else if(sscanf(s,"-%lld%n",&end,&n)==1 && n==len)
      no_end=false;
   else if(sscanf(s,"%lld-%lld%n",&start,&end,&n)==2 && n==len)
      no_start=no_end=false;
   else if(sscanf(s,"%lld%c-%n",&start,&start_k,&n)==2 && n==len)
      no_start=false;
   else if(sscanf(s,"-%lld%c%n",&end,&end_k,&n)==2 && n==len)
      no_end=false;
   else if(sscanf(s,"%lld%c-%lld%n",&start,&start_k,&end,&n)==3 && n==len)
      no_start=no_end=false;
   else if(sscanf(s,"%lld-%lld%c%n",&start,&end,&end_k,&n)==3 && n==len)
      no_start=no_end=false;
   else if(sscanf(s,"%lld%c-%lld%c%n",&start,&start_k,&end,&end_k,&n)==4 && n==len)
      no_start=no_end=false;
   else
   {
      error_text=_("Invalid range format. Format is min-max, e.g. 10-20.");
      return;
   }
   if(start_k)
      error_text=scale(&start,start_k);
   if(!error_text && end_k)
      error_text=scale(&end,end_k);
   if(!no_start && !no_end && start>end)
      start=replace_value(end,start);
}

const char *Range::scale(long long *value,char suf)
{
   // kilo, Mega, Giga, Tera, Peta, Exa, Zetta, Yotta
   static const char s[]="kMGTPEZY";
   const char *match=strchr(s,suf);
   if(!match)
      return _("Invalid suffix. Valid suffixes are: k, M, G, T, P, E, Z, Y");
   unsigned shift=10*(match-s+1);
   if(((*value<<shift)>>shift)!=*value)
      return _("Integer overflow");
   *value<<=shift;
   return 0;
}

long long Range::Random()
{
   random_init();

   if(no_start && no_end)
      return random();
   if(no_end)
      return start+random();

   return start + (long long)((end-start+1)*random01());
}

const char *ResMgr::RangeValidate(xstring_c *s)
{
   Range r(*s);
   if(r.Error())
      return r.ErrorText();
   return 0;
}

const char *ResMgr::ERegExpValidate(xstring_c *s)
{
   if(**s==0)
      return 0;
   regex_t re;
   int err=regcomp(&re,*s,REG_EXTENDED|REG_NOSUB);
   if(err)
   {
      static char err_msg[128];
      regerror(err,0,err_msg,sizeof(err_msg));
      return err_msg;
   }
   regfree(&re);
   return 0;
}

const char *ResMgr::IPv4AddrValidate(xstring_c *value)
{
   if(!**value)
      return 0;
   struct in_addr addr;
   if(!inet_pton(AF_INET,*value,&addr))
      return _("Invalid IPv4 numeric address");
   return 0;
}

#if INET6
const char *ResMgr::IPv6AddrValidate(xstring_c *value)
{
   if(!**value)
      return 0;
   struct in6_addr addr;
   if(!inet_pton(AF_INET6,*value,&addr))
      return _("Invalid IPv6 numeric address");
   return 0;
}
#endif

const char *ResMgr::FileAccessible(xstring_c *value,int mode,bool want_dir)
{
   if(!**value)
      return 0;
   const char *f=expand_home_relative(*value);
   xstring_c cwd;
   const char *error=0;
   if(f[0]!='/')
   {
      cwd.set_allocated(xgetcwd());
      if(cwd)
	 f=dir_file(cwd,f);
   }
   struct stat st;
   if(stat(f,&st)<0)
      error=strerror(errno);
   else if(want_dir ^ S_ISDIR(st.st_mode))
      error=strerror(errno=want_dir?ENOTDIR:EISDIR);
   else if(access(f,mode)<0)
      error=strerror(errno);
   else
      value->set(f);
   return error;
}
const char *ResMgr::FileReadable(xstring_c *value)
{
   return FileAccessible(value,R_OK);
}
const char *ResMgr::FileExecutable(xstring_c *value)
{
   return FileAccessible(value,X_OK);
}
const char *ResMgr::DirReadable(xstring_c *value)
{
   return FileAccessible(value,R_OK|X_OK,true);
}
const char *ResMgr::FileCreatable(xstring_c *value)
{
   if(!**value)
      return 0;
   const char *error=FileAccessible(value,W_OK,false);
   if(error && errno!=ENOENT)
      return error;
   const char *bn=basename_ptr(*value);
   xstring_c dir(dirname(*value));
   if(!*dir)
      dir.set_allocated(xgetcwd());
   error=FileAccessible(&dir,X_OK|W_OK,true);
   if(!error)  // dir may be expanded, combine it with base file name.
      value->set(dir_file(dir,bn));
   return error;
}

#ifdef HAVE_ICONV
CDECL_BEGIN
# include <iconv.h>
CDECL_END
#endif
const char *ResMgr::CharsetValidate(xstring_c *value)
{
   if(!**value)
      return 0;
#ifdef HAVE_ICONV
   iconv_t ic=iconv_open(*value,*value);
   if(ic==(iconv_t)-1)
      return _("this encoding is not supported");
   iconv_close(ic);
   return 0;
#else
   return _("this encoding is not supported");
#endif
}

const char *ResMgr::NoClosure(xstring_c *)
{
   return _("no closure defined for this setting");
}

const char *ResMgr::UNumberPairValidate(xstring_c *value)
{
   int n=0;

   unsigned a,b;
   if(2>sscanf(value->get(),"%u%*c%u%n",&a,&b,&n))
   {
      if(UNumberValidate(value))
	 return _("invalid pair of numbers");
      return 0;
   }

   value->truncate(n);

   return 0;
}
void ResValue::ToNumberPair(int &a,int &b)
{
   switch(sscanf(s,"%d%*c%d",&a,&b))
   {
   case 0: a=0;
   case 1: b=a;
   }
}

ResClient *ResClient::chain;
ResValue ResClient::Query(const char *name,const char *closure) const
{
   if(!strchr(name,':'))
   {
      const char *prefix=ResPrefix();
      name=xstring::format("%s:%s",prefix,name);
      name=alloca_strdup(name);
   }
   if(!closure)
      closure=ResClosure();
   return ResMgr::Query(name,closure);
}
ResClient::ResClient()
{
   ListAdd(ResClient,chain,this,next);
}
ResClient::~ResClient()
{
   ListDel(ResClient,chain,this,next);
}
void ResClient::ReconfigAll(const char *r)
{
   ListScan(ResClient,chain,next)
      scan->Reconfig(r);
}

bool ResType::QueryBool(const char *closure) const
{
   return Query(closure).to_bool();
}
bool ResMgr::QueryBool(const char *name,const char *closure)
{
   return Query(name,closure).to_bool();
}
bool ResClient::QueryBool(const char *name,const char *closure) const
{
   return Query(name,closure).to_bool();
}
