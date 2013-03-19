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
#include "keyvalue.h"
#include <unistd.h>
#include "trio.h"
#include <fcntl.h>
#include <errno.h>

int KeyValueDB::Read(int fd)
{
   xstring key;
   xstring value;

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

      key.truncate(0);
      for(;;)
      {
	 key.append(c);
	 c=getc(f);
	 if(c==EOF)
	    break;
	 if(c==' ' || c=='\t' || c=='\n')
	    break;
      }

      if(c==EOF || c=='\n' || key.length()==0)
	 break;	// invalid line

      // skip space in mid
      while(c!=EOF && (c==' ' || c=='\t'))
	 c=getc(f);

      if(c==EOF || c=='\n')
	 break;

      value.truncate(0);
      for(;;)
      {
	 value.append(c);
	 c=getc(f);
	 if(c==EOF)
	    break;
	 if(c=='\n')
	    break;
      }

      Add(key,value);

      if(c==EOF)
	 break;
   }
   fclose(f);
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

   for(p=chain; p; p=p->next)
   {
      int len=strlen(p->key);
      if(len>max_key_len)
	 max_key_len=len;
   }
   max_key_len&=~7;  // save some bytes

   xstring buf("");
   for(p=chain; p; p=p->next)
      buf.appendf("%-*s\t%s\n",max_key_len,p->key.get(),value_mangle(p->value));
   return buf.borrow();
}

int KeyValueDB::Write(int fd)
{
   xstring_ca buf(Format());
   int res=write(fd,buf,strlen(buf));
   close(fd);
   return res;
}

void KeyValueDB::Add(const char *key,const char *value)
{
   Pair **p=LookupPair(key);
   if(!p)
      AddPair(NewPair(key,value));
   else
      p[0]->SetValue(value);
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
   return p ? (*p)->value.get() : 0;
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
      bool echo=true;
      for(int i=0; i<retries; i++)
      {
	 sleep(1);
	 if(echo && write(2,".",1)==-1)
	    echo=false;
	 res=fcntl(fd,F_SETLK,&lk);
	 if(res==0)
	    break;
      }
      if(echo && write(2,"\r",1)==-1)
	 echo=false;
   }
   if(res==-1 && E_LOCK_IGNORE(errno))
      return 0;
   return res;
}
