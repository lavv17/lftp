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

#ifndef ARGV_H
#define ARGV_H

#include "trio.h"
#include "StringSet.h"
#include "getopt.h"

class ArgV : public StringSet
{
   int ind;

public:
   ArgV() { ind=0; }
   ArgV(const char *a0) : StringSet(&a0,1) { ind=0; }
   ArgV(const char *a0, const char *args);
   ArgV(const ArgV& a) : StringSet(a) { ind=0; }
   ArgV(const ArgV *a) : StringSet(*a) { ind=0; }
   ArgV(int new_c,const char * const *new_v) : StringSet(new_v,new_c) { ind=0; }
   ~ArgV();

   ArgV& Append(const char *s) { StringSet::Append(s); return *this; }
   ArgV& Append(int a) { char buf[32]; sprintf(buf,"%d",a); return Append(buf); }
   ArgV& Add(const char *a) { return Append(a); } // alias

   char *Combine(int start_index=0,int end_index=0) const;

   // for the UNIX shell
   char *CombineShellQuoted(int start) const;
   // for lftp's CmdExec
   char *CombineQuoted(int start_index=0) const;
   char *CombineCmd(int i=0) const;

   int getopt_long(const char *opts,const struct option *lopts,int *lind=0);
   int getopt(const char *opts)
      {
	 return getopt_long(opts,0,0);
      }
   const char *getopt_error_message(int e);

   void seek(int n);
   void rewind() { seek(0); }
   const char *getnext();

   const char *getarg(int n) const { return String(n); }
   const char *getcurr() const { return ind<Count()?getarg(ind):0; }
   int getindex() const { return ind; }
   void setarg(int n,const char *s) { Replace(n,s); }
   void delarg(int n) { if(ind>n)--ind; Remove(n); }
   void insarg(int n,const char *s) { InsertBefore(n,s); }
   const char *a0() const { return getarg(0); }
   void back();
   int count() const { return Count(); }
   const char *const*GetV() const { return Set(); }
   char **GetVNonConst() { return SetNonConst(); }
};

#endif//ARGV_H
