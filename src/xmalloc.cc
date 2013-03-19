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
#include "trio.h"
#include "xmalloc.h"

static int memory_count=0;

static void memory_error_and_abort(const char *fname,size_t size)
{
   fprintf(stderr,"%s: out of virtual memory when trying to get %lu bytes\n",
	 fname,(long)size);
   exit(2);
}

void *xmalloc (size_t bytes)
{
   if(bytes==0)
      return 0;
   void *temp=(void*)malloc(bytes);
   if(temp==0)
      memory_error_and_abort("xmalloc",bytes);
   memory_count++;
#ifdef MEM_DEBUG
   printf("xmalloc %p %lu (count=%d)\n",temp,(long)bytes,memory_count);
#endif
   return(temp);
}

void *xrealloc(void *pointer,size_t bytes)
{
   void *temp;
   if(pointer==0 && bytes==0)
      return 0;
   if(bytes==0)
   {
      memory_count--;
      free(pointer);
      temp=0;
      goto leave;
   }
   if(pointer==0)
   {
      temp=(void*)malloc(bytes);
      memory_count++;
   }
   else
      temp=(void*)realloc(pointer,bytes);
   if(temp==0)
      memory_error_and_abort ("xrealloc",bytes);
leave:
#ifdef MEM_DEBUG
   printf("xrealloc %p %p %lu (count=%d)\n",pointer,temp,(long)bytes,memory_count);
#endif
   return(temp);
}

void xfree(void *p)
{
   if(!p)
      return;
#ifdef MEM_DEBUG
   printf("xfree %p (count=%d)\n",p,memory_count);
#endif
   memory_count--;
   free(p);
}

char *xstrdup(const char *s,int spare)
{
   if(!s)
      return (char*)xmalloc(spare);
#ifdef MEM_DEBUG
   printf("xstrdup \"%s\"\n",s);
#endif
   size_t len=strlen(s)+1;
   char *mem=(char*)xmalloc(len+spare);
   memcpy(mem,s,len);
   return mem;
}

char *xstrset(char *&mem,const char *s,size_t len)
{
   if(!s)
   {
      xfree(mem);
      return mem=0;
   }
#ifdef MEM_DEBUG
   printf("xstrset \"%.*s\"\n",len,s);
#endif
   if(s==mem)
   {
      mem[len]=0;
      return mem;
   }
   size_t old_len=(mem?strlen(mem)+1:0);
   if(mem && s>mem && s<mem+old_len)
   {
      memmove(mem,s,len);
      mem[len]=0;
      return mem;
   }
   if(old_len<len+1)
      mem=(char*)xrealloc(mem,len+1);
   memcpy(mem,s,len);
   mem[len]=0;
   return mem;
}
char *xstrset(char *&mem,const char *s)
{
   if(!s)
   {
      xfree(mem);
      return mem=0;
   }
#ifdef MEM_DEBUG
   printf("xstrset \"%s\"\n",s);
#endif
   if(s==mem)
      return mem;
   size_t old_len=(mem?strlen(mem)+1:0);
   size_t len=strlen(s)+1;
   if(mem && s>mem && s<mem+old_len)
      return (char*)memmove(mem,s,len);
   if(old_len<len)
      mem=(char*)xrealloc(mem,len);
   memcpy(mem,s,len);
   return mem;
}
void xmalloc_register_block(void *b)
{
   if(!b)
      return;
   memory_count++;
#ifdef MEM_DEBUG
   printf("xmalloc %p (count=%d)\n",b,memory_count);
#endif
}

#ifdef MEM_DEBUG
void *__builtin_new(size_t s)
{
   return xmalloc(s);
}
void __builtin_delete(void *p)
{
   xfree(p);
}
#endif
