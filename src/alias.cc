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

#include <stdlib.h>
#include <stdio.h>

#include "alias.h"
#include "xmalloc.h"

Alias *Alias::base;

Alias::Alias(const char *alias,const char *value,Alias *next)
{
   this->alias=xstrdup(alias);
   this->value=xstrdup(value);
   this->next=next;
}

Alias::~Alias()
{
   xfree(this->alias);
   xfree(this->value);
}

void Alias::Add(const char *alias,const char *value)
{
   Alias **scan=&base;
   while(*scan)
   {
      int dif=strcasecmp((*scan)->alias,alias);
      if(dif==0)
      {
	 xfree((*scan)->value);
	 (*scan)->value=xstrdup(value);
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

#if 0
void Alias::List()
{
   Alias *scan=base;
   while(scan)
   {
      printf("alias %s \"%s\"\n",scan->alias,scan->value);
      scan=scan->next;
   }
}
#endif

char *Alias::Format()
{
   char *res;
   Alias *scan;
   int size=0;

   for(scan=base; scan; scan=scan->next)
   {
      size+=6+strlen(scan->alias)*2;
      size+=2+strlen(scan->value)*2+1+1;
   }
   res=(char*)xmalloc(size+1);
   char *store=res;

   for(scan=base; scan; scan=scan->next)
   {
      strcpy(store,"alias ");
      store+=strlen(store);
      char *s=scan->alias;
      while(*s)
      {
	 if(strchr("\" \t\\>|",*s))
	    *store++='\\';
	 *store++=*s++;
      }
      *store++=' ';
      s=scan->value;

      bool par=false;
      if(*s==0 || strcspn(s," \t>|")!=strlen(s))
	 par=true;
      if(par)
	 *store++='"';
      while(*s)
      {
	 if(strchr("\"\\",*s))
	    *store++='\\';
	 *store++=*s++;
      }
      if(par)
	 *store++='"';
      *store++='\n';
   }
   *store=0;
   return res;
}
