/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "keyvalue.h"
#include <unistd.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include "xalloca.h"

int KeyValueDB::Read(int fd)
{
   int key_size=16;
   char *key=(char*)xmalloc(key_size);
   int value_size=16;
   char *value=(char*)xmalloc(value_size);

   int c;

   FILE *f=fdopen(fd,"r");

   for(;;)
   {
      c=getc(f);

      // skip leading space
      while(c!=EOF && (c==' ' || c=='\t'))
	 c=getc(f);

      if(c==EOF)
	 break;
      if(c=='\n')
	 continue;   // next line

      char *store=key;

      for(;;)
      {
	 if(store>=key+key_size-1)
	 {
	    int shift=store-key;
	    key=(char*)xrealloc(key,key_size*=2);
	    store=key+shift;
	 }
	 *store++=c;

	 c=getc(f);
	 if(c==EOF)
	    break;
	 if(c==' ' || c=='\t' || c=='\n')
	    break;
      }
      *store=0;

      if(c==EOF || c=='\n' || store==key)
	 break;	// invalid line

      // skip space in mid
      while(c!=EOF && (c==' ' || c=='\t'))
	 c=getc(f);

      if(c==EOF || c=='\n')
	 break;

      store=value;

      for(;;)
      {
	 if(store>=value+value_size-1)
	 {
	    int shift=store-value;
	    value=(char*)xrealloc(value,value_size*=2);
	    store=value+shift;
	 }
	 *store++=c;

	 c=getc(f);
	 if(c==EOF)
	    break;
	 if(c=='\n')
	    break;
      }
      *store=0;

      Add(key,value);

      if(c==EOF)
	 break;
   }
   fclose(f);
   xfree(key);
   xfree(value);
   return 0;
}

int KeyValueDB::VKeyCompare(const void *a,const void *b)
{
   KeyValueDB::Pair *pa=*(KeyValueDB::Pair*const*)a;
   KeyValueDB::Pair *pb=*(KeyValueDB::Pair*const*)b;
   return KeyValueDB::KeyCompare(pa,pb);
}

void KeyValueDB::Sort()
{
   int count=0;
   Pair *scan;
   for(scan=chain; scan; scan=scan->next)
      count++;

   if(count==0)
      return;

   Pair **arr=(Pair**)alloca(count*sizeof(*arr));
   count=0;
   for(scan=chain; scan; scan=scan->next)
      arr[count++]=scan;

   qsort(arr,count,sizeof(*arr),&KeyValueDB::VKeyCompare);

   chain=0;
   while(count--)
   {
      arr[count]->next=chain;
      chain=arr[count];
   }
}

char *KeyValueDB::Format(StringMangler value_mangle)
{
   Sort();

   Pair *p;
   int max_key_len=0;
   size_t size_required=0;
   int n=0;

   for(p=chain; p; p=p->next)
   {
      int len=strlen(p->key);
      if(len>max_key_len)
	 max_key_len=len;
      size_required+=1+strlen(value_mangle(p->value))+1;
      n++;
   }
   size_required+=max_key_len*n;
   max_key_len&=~7;  // save some bytes

   char *buf=(char*)xmalloc(size_required+1);
   char *store=buf;

   for(p=chain; p; p=p->next)
   {
      sprintf(store,"%-*s\t%s\n",max_key_len,p->key,value_mangle(p->value));
      store+=strlen(store);
   }
   *store=0; // this is for chain==0 case

   return buf;
}

int KeyValueDB::Write(int fd)
{
   char *buf=Format();
   int res=write(fd,buf,strlen(buf));
   xfree(buf);
   close(fd);
   return res;
}

void KeyValueDB::Add(const char *key,const char *value)
{
   Pair **p=LookupPair(key);
   if(!p)
      chain=new Pair(key,value,chain);
   else
   {
      xfree((*p)->value);
      (*p)->value=xstrdup(value);
   }
}

void KeyValueDB::Remove(const char *key)
{
   Pair **p=LookupPair(key);
   if(p)
      Purge(p);
}

KeyValueDB::Pair **KeyValueDB::LookupPair(const char *key) const
{
   for(const Pair * const*p=&chain; *p; p=&(*p)->next)
   {
      if((*p)->KeyCompare(key)==0)
	 return const_cast<KeyValueDB::Pair **>(p);
   }
   return 0;
}

const char *KeyValueDB::Lookup(const char *key) const
{
   const Pair * const*p=LookupPair(key);
   return p ? (*p)->value : 0;
}

int KeyValueDB::Lock(int fd,int type)
{
   struct flock	lk;
   lk.l_type=type;
   lk.l_whence=0;
   lk.l_start=0;
   lk.l_len=0;
   int res=fcntl(fd,F_SETLK,&lk);
   if(res==-1 && E_RETRY(errno))
   {
      int retries=5;
      for(int i=0; i<retries; i++)
      {
	 sleep(1);
	 write(2,".",1);
	 res=fcntl(fd,F_SETLK,&lk);
	 if(res==0)
	    break;
      }
      write(2,"\r",1);
   }
   if(res==-1 && E_LOCK_IGNORE(errno))
      return 0;
   return res;
}
