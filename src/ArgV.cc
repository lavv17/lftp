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

#include "getopt.h"
#include "xmalloc.h"
#include "misc.h"
#include "ArgV.h"

ArgV::ArgV(const char *a0, const char *args)
{
   int argc;
   char **argv = tokenize(args, &argc);
   Init(argc,argv);
   tokenize_free(argv);

   insarg(0, a0);
}

void ArgV::GetRoom(int n)
{
   // alloc one extra for trailing zero
   int size=((n+4)&~3);
   if(size>allocated)
   {
      v=(char**)xrealloc(v,sizeof(*v)*size);
      allocated=size;
   }
}

void ArgV::Init(int new_c,const char * const *new_v)
{
   c=new_c;
   v=0;
   allocated=0;
   GetRoom(c);
   int i;
   for(i=0; i<c; i++)
      v[i]=xstrdup(new_v[i]);
   v[i]=0;
   ind=0;
}

ArgV::~ArgV()
{
   int i;
   for(i=0; i<c; i++)
      xfree(v[i]);
   xfree(v);
}

void ArgV::Empty()
{
   int i;
   for(i=0; i<c; i++)
      xfree(v[i]);
   GetRoom(0);
   v[0]=0;
   c=0;
   ind=0;
}

void ArgV::seek(int n)
{
   if(n>=c)
      n=c;
   ind=n;
}

char *ArgV::getnext()
{
   if(++ind>=c)
   {
      ind=c; // so that getcurr will return 0
      return 0;
   }
   return v[ind];
}

void ArgV::back()
{
   if(ind>0)
      ind--;
}

char *ArgV::Combine(int start) const
{
   int	 i;
   char  *res;
   char	 *store,*arg;
   int	 len=0;

   for(i=start; i<c; i++)
      len+=strlen(v[i])+1;

   if(len==0)
      return(xstrdup(""));

   res=(char*)xmalloc(len);

   store=res;
   for(i=start; i<c; i++)
   {
      arg=v[i];
      while(*arg)
	 *store++=*arg++;
      *store++=' ';
   }
   store[-1]=0;

   return(res);
}

void ArgV::Append(const char *s)
{
   GetRoom(c+1);
   v[c++]=xstrdup(s);
   v[c]=0;
}

void ArgV::setarg(int n,const char *s)
{
   if(n==count())
      Append(s);
   else if(n<count() && n>=0)
   {
      xfree(v[n]);
      v[n]=xstrdup(s);
   }
}

void ArgV::delarg(int n)
{
   if(n<count() && n>=0)
   {
      if(n<ind)
	 ind--;
      xfree(v[n]);
      // copy with trailing null pointer
      memmove(v+n,v+n+1,(count()-n)*sizeof(*v));
      c--;
   }
}
void ArgV::insarg(int n,const char *s)
{
   if(n==count())
      Append(s);
   else
   {
      GetRoom(c+1);
      // copy with trailing null pointer
      memmove(v+n+1,v+n,(count()-n+1)*sizeof(*v));
      c++;
      v[n]=xstrdup(s);
   }
}

int ArgV::getopt_long(const char *opts,const struct option *lopts,int *lind)
{
   optind=ind;
   int r=::getopt_long(c,v,opts,lopts,lind);
   ind=optind;
   return r;
}
