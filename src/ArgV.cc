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
#include "ArgV.h"

void ArgV::Init(int new_c,const char * const *new_v)
{
   c=new_c;
   v=(char**)xmalloc(sizeof(char*)*(c+1));
   int i;
   for(i=0; i<c; i++)
      v[i]=xstrdup(new_v[i]);
   v[i]=0;
   ind=0;
}

void ArgV::Empty()
{
   int i;
   for(i=0; i<c; i++)
      free(v[i]);
   v=(char**)xrealloc(v,sizeof(*v));
   v[0]=0;
   c=0;
   ind=0;
}

void ArgV::rewind()
{
   ind=0;
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

char *ArgV::Combine(int start)
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
   v=(char**)xrealloc(v,sizeof(*v)*(c+2));
   v[c++]=xstrdup(s);
   v[c]=0;
}

void ArgV::setarg(int n,const char *s)
{
   if(n==count())
      Append(s);
   else if(n<count() && n>=0)
   {
      free(v[n]);
      v[n]=xstrdup(s);
   }
}
