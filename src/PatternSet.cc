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

#include <config.h>
#include <fnmatch.h>
#include "PatternSet.h"

PatternSet::PatternSet()
{
   chain=0;
   first=0;
}
void PatternSet::Add(Type t,Pattern *p)
{
   chain=new PatternLink(t,p,chain);
   if(!first)
      first=chain;
}

void PatternSet::AddFirst(Type t,Pattern *p)
{
   PatternLink *new_link=new PatternLink(t,p,NULL);
   if(!first)
      first=chain=new_link;
   else
      first->next=new_link;
}
PatternSet::Type PatternSet::GetFirstType() const
{
   return first->type;
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
   for(PatternLink *scan=chain; scan; scan=scan->next)
   {
      if(scan->pattern->Match(str))
	 return scan->type==type;
      if(!scan->next)
	 return scan->type!=type;
   }
   return false;
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
   return fnmatch(pattern,scan,FNM_PATHNAME)==0;
}

PatternSet::Regex::Regex(const char *p) : Pattern(p)
{
   memset(&compiled,0,sizeof(compiled));  // safety.
   int errcode=regcomp(&compiled,pattern,REG_EXTENDED|REG_NOSUB);
   if(errcode)
   {
      size_t need=regerror(errcode,0,0,0);
      xstring& e=xstring::get_tmp();
      e.get_space(need-1);
      e.set_length(regerror(errcode,0,e.get_non_const(),need)-1);
      error.setf(_("regular expression `%s': %s"),p,e.get());
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
