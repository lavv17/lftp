/*
 * lftp and utils
 *
 * Copyright (c) 1996-2004 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "xmalloc.h"
#include "misc.h"
#include "ArgV.h"

ArgV::ArgV(const char *a0, const char *args)
{
   ind=0;

   int argc;
   char **argv = tokenize(args, &argc);
   Assign(argv,argc);
   tokenize_free(argv);

   insarg(0, a0);
}

void ArgV::seek(int n)
{
   if(n>=Count())
      n=Count();
   ind=n;
}

const char *ArgV::getnext()
{
   const char *s=String(++ind);
   if(!s)
      ind=Count(); // getcurr will return 0
   return s;
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
   char	 *store;
   const char *arg;
   int	 len=0;

   for(i=start; i<Count(); i++)
      len+=strlen(getarg(i))+1;

   if(len==0)
      return(xstrdup(""));

   res=(char*)xmalloc(len);

   store=res;
   for(i=start; i<Count(); i++)
   {
      arg=getarg(i);
      while(*arg)
	 *store++=*arg++;
      *store++=' ';
   }
   store[-1]=0;

   return(res);
}

int ArgV::getopt_long(const char *opts,const struct option *lopts,int *lind)
{
   optind=ind;
   int r=::getopt_long(Count(),SetNonConst(),opts,lopts,lind);
   ind=optind;
   return r;
}
