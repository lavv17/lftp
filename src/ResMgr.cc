/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>

#include <fnmatch.h>
#include "ResMgr.h"
#include "SMTask.h"
#include "xmalloc.h"
#include "xalloca.h"

ResMgr::Resource  *ResMgr::chain=0;
ResDecl		  *ResMgr::type_chain=0;

ResMgr::CmpRes ResMgr::VarNameCmp(const char *good_name,const char *name)
{
   CmpRes res=EXACT;
   const char *colon=strchr(good_name,':');
   if(colon && !strchr(name,':'))
   {
      good_name=colon+1;
      res=SUBSTR;
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
	 res=SUBSTR;
	 continue;
      }
      return DIFFERENT;
   }
   return res;
}

const char *ResMgr::FindVar(const char *name,ResDecl **type)
{
   *type=0;

   int sub=0;
   ResDecl *type_scan;
   for(type_scan=type_chain; type_scan; type_scan=type_scan->next)
   {
      switch(VarNameCmp(type_scan->name,name))
      {
      case EXACT:
	 *type=type_scan;
	 return 0;
      case SUBSTR:
	 sub++;
	 *type=type_scan;
	 break;
      case DIFFERENT:
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

const char *ResMgr::Set(const char *name,const char *cclosure,const char *cvalue)
{
   const char *msg;

   ResDecl *type;
   // find type of given variable
   msg=FindVar(name,&type);
   if(msg)
      return msg;

   char *value=xstrdup(cvalue);
   char *closure=xstrdup(cclosure);

   if(value && type->val_valid && (msg=(*type->val_valid)(&value))!=0)
   {
      xfree(value);
      return msg;
   }

   if(closure && type->closure_valid && (msg=(*type->closure_valid)(&closure))!=0)
   {
      xfree(closure);
      xfree(value);
      return msg;
   }

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
      {
	 free((*scan)->value);
	 (*scan)->value=value;
	 free(closure);
      }
      else
      {
	 Resource *to_free=*scan;
	 *scan=(*scan)->next;
	 free(to_free);
	 free(closure);
      }
      SMTask::ReconfigAll();
   }
   else
   {
      if(value)
      {
	 chain=new Resource(chain,type,closure,value);
	 SMTask::ReconfigAll();
      }
      else
	 free(closure);
   }
   return 0;
}

int ResMgr::ResourceCompare(const void *a,const void *b)
{
   const Resource *ar=*(const Resource*const*)a;
   const Resource *br=*(const Resource*const*)b;
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

char *ResMgr::Format(bool with_defaults,bool only_defaults)
{
   char *res;

   Resource *scan;
   ResDecl  *dscan;

   int n=0;
   int dn=0;
   int size=0;
   if(!only_defaults)
   {
      for(scan=chain; scan; scan=scan->next)
      {
	 size+=4+strlen(scan->type->name);
	 if(scan->closure)
	    size+=1+2*strlen(scan->closure);
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

   res=(char*)xmalloc(size+1);
   char *store=res;

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

   qsort(arr,n,sizeof(*arr),ResourceCompare);

   for(i=0; i<n; i++)
   {
      sprintf(store,"set %s",arr[i]->type->name);
      store+=strlen(store);
      char *s=arr[i]->closure;
      if(s)
      {
	 *store++='/';
	 while(*s)
	 {
	    if(strchr("\" \t\\>|",*s))
	       *store++='\\';
	    *store++=*s++;
	 }
      }
      *store++=' ';
      s=arr[i]->value;

      bool par=false;
      if(*s==0 || strcspn(s," \t>|")!=strlen(s))
	 par=true;
      if(par)
	 *store++='"';
      while(*s)
      {
	 if(strchr("\"\\",*s))
	    *store++='\\';
	 *store++=*s++;
      }
      if(par)
	 *store++='"';
      *store++='\n';
   }
   *store=0;
   for(i=0; i<dn; i++)
      delete created[i];
   return res;
}

const char *ResMgr::BoolValidate(char **value)
{
   char *v=*value;
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
   {
      free(v);
      *value=xstrdup(newval);
   }

   return 0;
}

const char *ResMgr::NumberValidate(char **value)
{
   char *v=*value;

   v+=strspn(v," \t");

   if(*v=='-')
      v++;

   int n=strspn(v,"1234567890");

   if(n==0)
      return _("invalid number");

   v[n]=0;

   return 0;
}

const char *ResMgr::UNumberValidate(char **value)
{
   char *v=*value;

   v+=strspn(v," \t");

   int n=strspn(v,"1234567890");

   if(n==0)
      return _("invalid number");

   v[n]=0;

   return 0;
}

bool ResMgr::Resource::ClosureMatch(const char *cl_data)
{
   if(!closure && !cl_data)
      return true;
   if(!(closure && cl_data))
      return false;
   return 0==fnmatch(closure,cl_data,FNM_PATHNAME);
}

const char *ResMgr::SimpleQuery(const char *name,const char *closure)
{
   const char *msg;

   ResDecl *type;
   // find type of given variable
   msg=FindVar(name,&type);
   if(msg)
      return 0;

   Resource **scan;
   // find the value
   for(scan=&chain; *scan; scan=&(*scan)->next)
      if((*scan)->type==type && (*scan)->ClosureMatch(closure))
	 break;

   // if found
   if(*scan)
      return (*scan)->value;

   return 0;
}

ResValue ResMgr::Query(const char *name,const char *closure)
{
   const char *msg;

   ResDecl *type;
   // find type of given variable
   msg=FindVar(name,&type);
   if(msg)
   {
      // debug only
      fprintf(stderr,"Query of variable `%s' failed: %s\n",name,msg);
      return 0;
   }

   for(;;)
   {
      Resource **scan;
      // find the value
      for(scan=&chain; *scan; scan=&(*scan)->next)
	 if((*scan)->type==type && (*scan)->ClosureMatch(closure))
	    break;
      // if found
      if(*scan)
	 return (*scan)->value;
      if(!closure)
	 break;
      closure=0;
   }

   return type->defvalue;
}

ResValue ResDecl::Query(const char *closure)
{
   const char *v=0;

   if(closure)
      v=ResMgr::SimpleQuery(name,closure);
   if(!v)
      v=ResMgr::SimpleQuery(name,0);
   if(!v)
      v=defvalue;

   return v;
}

bool ResMgr::str2bool(const char *s)
{
   return(strchr("TtYy1+",s[0])!=0 || !strcasecmp(s,"on"));
}
