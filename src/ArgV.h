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

#ifndef ARGV_H
#define ARGV_H

class ArgV
{
   char **v;
   int c;
   int ind;

   void Init(int,const char * const *);

public:
   ArgV() { Init(0,0); }
   ArgV(const char *a0) { Init(1,&a0); }
   void Empty();
   void Append(const char *);
   ArgV(const ArgV& a) { Init(a.c,a.v); }
   ArgV(int new_c,const char * const *new_v) { Init(new_c,new_v); }
   ~ArgV() { Empty(); }

   char *Combine(int start_index=0) const;
   char *CombineQuoted(int start_index=0) const;

   int getopt_long(const char *opts,const struct option *lopts,int *lind);
   int getopt(const char *opts)
      {
	 return getopt_long(opts,0,0);
      }

   void rewind();
   char *getnext();

   char *getarg(int n) const
      {
	 if(n>=c)
	    return 0;
	 return v[n];
      }
   char *getcurr() const { return ind<c?getarg(ind):0; }
   int getindex() const { return ind; }
   void setarg(int n,const char *s);
   void delarg(int n);
   void insarg(int n,const char *s);
   char *a0() const { return getarg(0); }
   void back();
   int count() const { return c; }
   char **GetV() const { return v; }
};

#endif//ARGV_H
