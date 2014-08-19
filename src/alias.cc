/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <stdlib.h>
#include "trio.h"

#include "alias.h"

Alias *Alias::base;

void Alias::Add(const char *alias,const char *value)
{
   Alias **scan=&base;
   while(*scan)
   {
      int dif=strcasecmp((*scan)->alias,alias);
      if(dif==0)
      {
	 (*scan)->value.set(value);
	 return;
      }
      if(dif>0)
	 break;
      scan=&((*scan)->next);
   }
   *scan=new Alias(alias,value,*scan);
}

void Alias::Del(const char *alias)
{
   Alias **scan=&base;
   while(*scan)
   {
      int dif=strcasecmp((*scan)->alias,alias);
      if(dif==0)
      {
	 Alias *tmp=(*scan)->next;
	 delete *scan;
	 *scan=tmp;
	 return;
      }
      scan=&((*scan)->next);
   }
}

const char *Alias::Find(const char *alias)
{
   Alias *scan=base;
   while(scan)
   {
      int dif=strcasecmp(scan->alias,alias);
      if(dif==0)
	 return(scan->value);
      if(dif>0)
	 break;
      scan=scan->next;
   }
   return 0;
}

char *Alias::Format()
{
   xstring res("");
   for(Alias *scan=base; scan; scan=scan->next)
   {
      res.append("alias ");
      const char *s=scan->alias;
      while(*s)
      {
	 if(strchr("\" \t\\>|",*s))
	    res.append('\\');
	 res.append(*s++);
      }
      res.append(' ');
      s=scan->value;

      bool par=false;
      if(*s==0 || strcspn(s," \t>|")!=strlen(s))
	 par=true;
      if(par)
	 res.append('"');
      while(*s)
      {
	 if(strchr("\"\\",*s))
	    res.append('\\');
	 res.append(*s++);
      }
      if(par)
	 res.append('"');
      res.append('\n');
   }
   return res.borrow();
}
