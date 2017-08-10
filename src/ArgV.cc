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

#include "misc.h"
#include "ArgV.h"

ArgV::ArgV(const char *a0, const char *args_c)
{
   ind=0;

   Append(a0);
   char *args=alloca_strdup(args_c);
   for(char *t=strtok(args," "); t; t=strtok(NULL," "))
      Append(t);
}
ArgV::~ArgV()
{
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

xstring& ArgV::CombineTo(xstring& res,int start,int end) const
{
   res.nset("",0);
   if(!end)
      end=Count();
   if(start>=end)
      return res;
   for(;;)
   {
      res.append(getarg(start++));
      if(start>=end)
	 return(res);
      res.append(' ');
   }
}

xstring& ArgV::CombineShellQuotedTo(xstring& res,int start) const
{
   res.nset("",0);
   if(start>=Count())
      return res;
   for(;;)
   {
      for(const char *arg=String(start++); *arg; arg++)
      {
	 if (is_shell_special(*arg))
	    res.append('\\');
	 res.append(*arg);
      }
      if(start>=Count())
	 return(res);
      res.append(' ');
   }
}

int ArgV::getopt_long(const char *opts,const struct option *lopts,int *lind)
{
   optind=ind;
   int r=::getopt_long(Count(),SetNonConst(),opts,lopts,lind);
   ind=optind;
   return r;
}

const char *ArgV::getopt_error_message(int e)
{
   if(optopt>=32 && optopt<127)
   {
      if(e==':')
	 return xstring::format("%s -- %c",_("option requires an argument"),optopt);
      else
	 return xstring::format("%s -- %c",_("invalid option"),optopt);
   }
   if(ind>1)
   {
      if(e==':')
	 return xstring::format(_("option `%s' requires an argument"),getarg(ind-1));
      else
	 return xstring::format(_("unrecognized option `%s'"),getarg(ind-1));
   }
   return _("invalid option");
}
