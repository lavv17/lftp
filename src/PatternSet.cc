/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "PatternSet.h"
#include "xmalloc.h"

PatternSet::PatternSet()
{
   chain=0;
   add=&chain;
}
void PatternSet::Add(Type t,Pattern *p)
{
   *add=new PatternLink(t,p,*add);
   add=&((*add)->next);
}
PatternSet::~PatternSet()
{
   while(chain)
   {
      PatternLink *del=chain;
      chain=chain->next;
      delete del;
   }
}
bool PatternSet::Match(Type type,const char *str) const
{
   if(!chain)
      return false;
   // want the first pattern to play its role.
   bool match = (chain->type!=type);
   for(PatternLink *scan=chain; scan; scan=scan->next)
   {
      if(scan->type==type)
	 match = match || scan->pattern->Match(str);
      else
	 match = match && !scan->pattern->Match(str);
   }
   return match;
}

PatternSet::Pattern::Pattern(const char *p)
{
   pattern=xstrdup(p);
}
PatternSet::Pattern::~Pattern()
{
   xfree(pattern);
}

PatternSet::Glob::Glob(const char *p) : Pattern(p)
{
   slash_count=0;
   for(p=pattern; *p; p++)
      if(*p=='/')
	 slash_count++;
}

// abc/def.zip matches *.zip
// abc/def/ghi matches def/g*
bool PatternSet::Glob::Match(const char *str)
{
   const char *scan=str+strlen(str);
   int countdown=slash_count;
   while(scan>str)
   {
      scan--;
      if(*scan=='/')
      {
	 if(countdown==0)
	 {
	    scan++;
	    break;
	 }
	 countdown--;
      }
   }
   return fnmatch(pattern,scan,FNM_PATHNAME);
}

PatternSet::Regex::Regex(const char *p) : Pattern(p)
{
   error=0;
   memset(&compiled,0,sizeof(compiled));  // safety.
   int errcode=regcomp(&compiled,pattern,REG_EXTENDED|REG_NOSUB);
   if(errcode)
   {
      size_t need=regerror(errcode,0,0,0);
      error=(char*)xmalloc(need);
      regerror(errcode,0,error,need);
   }
}
PatternSet::Regex::~Regex()
{
   if(!error)
      regfree(&compiled);
}
bool PatternSet::Regex::Match(const char *str)
{
   if(error)
      return false;
   return regexec(&compiled,str,0,0,0)==0;
}
