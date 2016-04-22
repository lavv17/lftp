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

#ifndef EXCLUDE_H
#define EXCLUDE_H

#include <sys/types.h>
CDECL_BEGIN
#include <regex.h>
CDECL_END
#include "xstring.h"

class PatternSet
{
public:
   enum Type { EXCLUDE,INCLUDE };

   class Pattern
   {
   protected:
      xstring_c pattern;
   public:
      virtual bool Match(const char *str)=0;
      Pattern(const char *str) : pattern(str) {}
      virtual ~Pattern() {}
   };

private:
   class PatternLink
   {
      friend class PatternSet;
      Type type;
      Pattern *pattern;
      PatternLink *next;
      PatternLink(Type t,Pattern *p,PatternLink *n)
	 {
	    type=t;
	    pattern=p;
	    next=n;
	 }
      ~PatternLink()
	 {
	    delete pattern;
	 }
   };

   PatternLink *chain;
   PatternLink *first;

public:
   PatternSet();
   ~PatternSet();

   void	Add(Type,Pattern *);
   void	AddFirst(Type,Pattern *);
   Type GetFirstType() const;

   bool Match(Type t,const char *str) const;
   bool MatchExclude(const char *str) const { return Match(EXCLUDE,str); }
   bool MatchInclude(const char *str) const { return Match(INCLUDE,str); }

   class Regex : public Pattern
   {
      regex_t compiled;
      xstring error;
   public:
      Regex(const char *str);
      bool Error() { return error!=0; }
      const char *ErrorText() { return error; }
      bool Match(const char *str);
      ~Regex();
   };
   class Glob : public Pattern
   {
      int slash_count;
   public:
      Glob(const char *p);
      bool Match(const char *str);
   };
};

#endif
