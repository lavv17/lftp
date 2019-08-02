/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <fnmatch.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <cmath>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
CDECL_BEGIN
#include "regex.h"
CDECL_END
#include "ResMgr.h"
#include "SMTask.h"
#include "misc.h"
#include "StringSet.h"
#include "log.h"

xlist_head<Resource> Resource::all_list;
xmap<ResType*> *ResType::types_by_name;

int ResType::VarNameCmp(const char *good_name,const char *name)
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

bool ResType::IsAlias() const
{
   return closure_valid==ResMgr::AliasValidate;
}

const char *ResType::FindVar(const char *name,const ResType **type,const char **re_closure)
{
   const ResType *exact_proto=0;
   const ResType *exact_name=0;
   int sub=0;

   *type=types_by_name->lookup(name);
   if(*type)
      goto found; // exact match

   for(const ResType *type_scan=types_by_name->each_begin(); type_scan; type_scan=types_by_name->each_next())
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
   if(!*type && sub==0)
      return _("no such variable");
   if(sub==1)
      goto found;
   *type=0;
   return _("ambiguous variable name");

found:
   if((*type)->IsAlias()) {
      const char *alias_c=(*type)->GetAliasTarget();
      char *alias=alloca_strdup(alias_c);
      char *slash=strchr(alias,'/');
      if(slash) {
	 *slash=0;
	 if(re_closure)
	    *re_closure=alias_c+(slash+1-alias);
      }
      *type=types_by_name->lookup(alias);
      if(!*type)
	 return "invalid compatibility alias";
   }
   return 0;
}

const ResType *ResType::FindRes(const char *name)
{
   const ResType *type;
   const char *msg=FindVar(name,&type);
   if(msg)
      return 0;
   return type;
}

const char *ResType::Set(const char *name,const char *cclosure,const char *cvalue,bool def)
{
   ResType *type;
   // find type of given variable
   const char *msg=FindVar(name,&type,&cclosure);
   if(msg)
      return msg;

   return type->Set(cclosure,cvalue,def);
}

const char *ResType::Set(const char *cclosure,const char *cvalue,bool def)
{
   const char *msg=0;

   xstring_c value(cvalue);
   if(value && val_valid && (msg=val_valid(&value))!=0)
      return msg;

   xstring_c closure(cclosure);
   if((closure || closure_valid==ResMgr::HasClosure)
   && closure_valid && (msg=closure_valid(&closure))!=0)
      return msg;

   bool need_reconfig=false;

   xlist_for_each(Resource,*(type_value_list),node,scan)
   {
      // find the old value
      if(closure==scan->closure || !xstrcmp(scan->closure,closure))
      {
	 if(def)
	    return 0;
	 need_reconfig=true;
	 delete scan;
	 break;
      }
   }
   if(value)
   {
      (void)new Resource(this,closure,value,def);
      need_reconfig=true;
   }
   if(need_reconfig)
      ResClient::ReconfigAll(name);
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

void Resource::Format(xstring& buf) const
{
   buf.appendf("set %s",type->name);
   const char *s=closure;
   if(s)
   {
      buf.append('/');
      bool par=false;
      if(strcspn(s," \t>|;&")!=strlen(s))
	 par=true;
      if(par)
	 buf.append('"');
      while(*s)
      {
	 if(strchr("\"\\",*s))
	    buf.append('\\');
	 buf.append(*s++);
      }
      if(par)
	 buf.append('"');
   }
   buf.append(' ');
   s=value;

   bool par=false;
   if(*s==0 || strcspn(s," \t>|;&")!=strlen(s))
      par=true;
   if(par)
      buf.append('"');
   while(*s)
   {
      if(strchr("\"\\",*s))
	 buf.append('\\');
      buf.append(*s++);
   }
   if(par)
      buf.append('"');
   buf.append('\n');
}

static int PResourceCompare(const Resource *const*a,const Resource *const*b)
{
   return ResMgr::ResourceCompare(*a,*b);
}
static int RefResourceCompare(const Ref<Resource> *a,const Ref<Resource> *b)
{
   return ResMgr::ResourceCompare(*a,*b);
}

char *ResType::Format(bool with_defaults,bool only_defaults)
{
   RefArray<Resource> created;
   if(with_defaults || only_defaults)
   {
      for(ResType *dscan=types_by_name->each_begin(); dscan; dscan=types_by_name->each_next())
	 if((only_defaults || dscan->SimpleQuery(0)==0) && !dscan->IsAlias())
	    created.append(new Resource(dscan,
	       0,xstrdup(dscan->defvalue?dscan->defvalue:"(nil)")));
   }

   xstring buf("");

   if(!only_defaults)
   {
      // just created Resources are also in all_list.
      xarray<const Resource*> arr;
      xlist_for_each(Resource,Resource::all_list,node,scan) {
	 if(!scan->def || with_defaults)
	    arr.append(scan);
      }
      arr.qsort(PResourceCompare);
      for(int i=0; i<arr.count(); i++)
	 arr[i]->Format(buf);
   }
   else
   {
      created.qsort(RefResourceCompare);
      for(int i=0; i<created.count(); i++)
	 created[i]->Format(buf);
   }

   return buf.borrow();
}

char **ResType::Generator(void)
{
   StringSet res;

   for(ResType *dscan=types_by_name->each_begin(); dscan; dscan=types_by_name->each_next())
      if(!dscan->IsAlias())
	 res.Append(dscan->name);

   res.qsort();
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
   const int scale=1024;
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
const char *ResMgr::AliasValidate(xstring_c *)
{
   return "";
}

unsigned long long ResValue::to_unumber(unsigned long long max) const
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
long long ResValue::to_number(long long min,long long max) const
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
ResValue::operator int() const
{
   return to_number(INT_MIN,INT_MAX);
}
ResValue::operator long() const
{
   return to_number(LONG_MIN,LONG_MAX);
}
ResValue::operator unsigned() const
{
   return to_unumber(UINT_MAX);
}
ResValue::operator unsigned long() const
{
   return to_unumber(ULONG_MAX);
}
bool ResValue::to_tri_bool(bool a) const
{
   if(*s=='a' || *s=='A')
      return a;
   return to_bool();
}

Resource::Resource(ResType *type,const char *closure,const char *value,bool def)
   : type(type), value(value), closure(closure), def(def), all_node(this), type_value_node(this)
{
   all_list.add_tail(all_node);
   type->type_value_list->add_tail(type_value_node);
}
Resource::~Resource()
{
   all_node.remove();
   type_value_node.remove();
}

bool Resource::ClosureMatch(const char *cl_data)
{
   if(!closure && !cl_data)
      return true;
   if(!(closure && cl_data))
      return false;
   // a special case for domain name match (i.e. example.org matches *.example.org)
   if(closure[0]=='*' && closure[1]=='.' && !strcmp(closure+2,cl_data))
      return true;
   if(0==fnmatch(closure,cl_data,FNM_PATHNAME))
      return true;
   // try to match basename; helps matching torrent metadata url to *.torrent
   const char *bn=basename_ptr(cl_data);
   if(bn!=cl_data && 0==fnmatch(closure,bn,FNM_PATHNAME))
      return true;
   return false;
}

const char *ResMgr::QueryNext(const char *name,const char **closure,Resource **ptr)
{
   xlist<Resource> *node=0;
   if(*ptr==0)
   {
      const ResType *type=FindRes(name);
      if(!type) {
	 *ptr=0;
	 *closure=0;
	 return 0;
      }
      node=type->type_value_list->get_next();
   }
   else
   {
      node=(*ptr)->type_value_node.get_next();
   }
   *ptr=node->get_obj();
   if(*ptr) {
      *closure=(*ptr)->closure;
      return (*ptr)->value;
   }
   *closure=0;
   return 0;
}

const char *ResType::SimpleQuery(const char *closure) const
{
   // find the value
   xlist_for_each(Resource,*(type_value_list),node,scan)
      if(scan->ClosureMatch(closure))
	 return scan->value;
   return 0;
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
      v=SimpleQuery(closure);
   if(!v)
      v=SimpleQuery(0);
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
   Register();
}
ResDecls::ResDecls(ResType *array)
{
   while(array->name)
      array++->Register();
}
ResDecls::ResDecls(ResType *r1,ResType *r2,...)
{
   r.append(r1);
   r1->Register();
   if(!r2)
      return;
   r.append(r2);
   r2->Register();
   va_list v;
   va_start(v,r2);
   while((r1=va_arg(v,ResType *))!=0)
   {
      r1->Register();
      r.append(r1);
   }
   va_end(v);
}
ResDecls::~ResDecls()
{
   for(int i=0; i<r.count(); i++)
      r[i]->Unregister();
}

void ResType::Register()
{
   if(!types_by_name)
      types_by_name=new xmap<ResType*>;
   types_by_name->add(name,this);
   if(!type_value_list)
      type_value_list=new xlist_head<Resource>();
}
void ResType::Unregister()
{
   if(types_by_name)
      types_by_name->remove(name);
   if(type_value_list) {
      // remove all resources of this type
      xlist_for_each_safe(Resource,*type_value_list,node,scan,next)
	 delete scan;
      delete type_value_list;
      type_value_list=0;
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

void NumberPair::init(char sep1,const char *s)
{
   sep=sep1;
   Set(s);
}
long long NumberPair::parse1(const char *s)
{
   if(!s || !*s)
      return 0;
   const char *end=s;
   long long v=strtoll(s,const_cast<char**>(&end),0);
   long long m=get_power_multiplier(*end);
   if(s==end || m==0 || end[m>1]) {
      error_text=_("invalid number");
      return 0;
   }
   long long vm=v*m;
   if(vm/m!=v) {
      error_text=_("integer overflow");
      return 0;
   }
   return vm;
}
void NumberPair::Set(const char *s0)
{
   n1=n2=0;
   no_n1=no_n2=true;
   error_text=0;

   if(!s0)
      return;

   char *s1=alloca_strdup(s0);
   char *s2=s1;
   while(*s2 && *s2!=sep && *s2!=':')
      s2++;
   if(*s2)
      *s2++=0;
   else
      s2=0;

   n1=parse1(s1);
   no_n1=!*s1;
   n2=(s2?parse1(s2):n1);
   no_n2=(s2 && !*s2);

   if(!error_text && Log::global) {
      Log::global->Format(10,"%s translated to pair %lld%c%lld (%d,%d)\n",
	 s0,n1,sep,n2,no_n1,no_n2);
   }
}

Range::Range(const char *s) : NumberPair('-')
{
   if(!strcasecmp(s,"full") || !strcasecmp(s,"any"))
      return;
   Set(s);
}

long long Range::Random()
{
   random_init();

   if(no_n1 && no_n2)
      return random();
   if(no_n2)
      return n1+random();

   return n1 + (long long)((n2-n1+1)*random01());
}

const char *ResMgr::RangeValidate(xstring_c *s)
{
   Range r(*s);
   if(r.Error())
      return r.ErrorText();
   char *colon=strchr(s->get_non_const(),':');
   if(colon)
      *colon='-';
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
      const int max_err_len=128;
      char *err_msg=xstring::tmp_buf(max_err_len);
      regerror(err,0,err_msg,max_err_len);
      return err_msg;
   }
   regfree(&re);
   return 0;
}

const char *ResMgr::IPv4AddrValidate(xstring_c *value)
{
   if(!**value)
      return 0;
   if(!is_ipv4_address(*value))
      return _("Invalid IPv4 numeric address");
   return 0;
}

#if INET6
const char *ResMgr::IPv6AddrValidate(xstring_c *value)
{
   if(!**value)
      return 0;
   if(!is_ipv6_address(*value))
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
      xgetcwd_to(cwd);
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
   if(!error || (error && errno!=ENOENT))
      return error;
   const char *bn=basename_ptr(*value);
   xstring_c dir(dirname(*value));
   if(!*dir)
      xgetcwd_to(dir);
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

const char *ResMgr::HasClosure(xstring_c *c)
{
   if(!*c || !**c)
      return _("a closure is required for this setting");
   return 0;
}

const char *ResMgr::UNumberPairValidate(xstring_c *value)
{
   NumberPair pair(':',*value);
   if(pair.Error())
      return pair.ErrorText();
   return 0;
}
void ResValue::ToNumberPair(int &a,int &b) const
{
   NumberPair pair(':',s);
   if(pair.Error()) {
      a=b=0;
   } else {
      a=pair.N1();
      b=pair.HasN2()?pair.N2():a;
   }
}

xlist_head<ResClient> ResClient::list;
ResValue ResClient::Query(const char *name,const char *closure) const
{
   if(!strchr(name,':'))
   {
      const char *prefix=ResPrefix();
      name=xstring::cat(prefix,":",name,NULL);
      name=alloca_strdup(name);
   }
   if(!closure)
      closure=ResClosure();
   return ResMgr::Query(name,closure);
}
ResClient::ResClient()
   : node(this)
{
   list.add(node);
}
ResClient::~ResClient()
{
   node.remove();
}
void ResClient::ReconfigAll(const char *r)
{
   xlist_for_each(ResClient,list,node,scan)
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
bool ResType::QueryTriBool(const char *closure,bool a) const
{
   return Query(closure).to_tri_bool(a);
}
bool ResMgr::QueryTriBool(const char *name,const char *closure,bool a)
{
   return Query(name,closure).to_tri_bool(a);
}
bool ResClient::QueryTriBool(const char *name,const char *closure,bool a) const
{
   return Query(name,closure).to_tri_bool(a);
}

void ResType::ClassCleanup()
{
   xlist_for_each_safe(Resource,Resource::all_list,node,scan,next)
      delete scan;
   if(types_by_name) {
      for(ResType *t=types_by_name->each_begin(); t; t=types_by_name->each_next())
	 t->Unregister();
      delete types_by_name; types_by_name=0;
   }
}
