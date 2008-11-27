/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Library General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/* $Id$ */

#ifndef XMALLOC_H
#define XMALLOC_H

#include <stdlib.h>

#ifdef DBMALLOC
#include "dbmalloc.h"
#endif

void *xmalloc(size_t);
void *xrealloc(void *,size_t);
char *xstrdup(const char *s,int spare=0);
char *xstrset(char *&mem,const char *s);
char *xstrset(char *&mem,const char *s,size_t n);
#define alloca_strdup(s) alloca_strdup2((s),0)
#define alloca_strdup2(s,n) ((s)?strcpy((char*)alloca(strlen((s))+1+(n)),(s)) \
                                :((n)==0?0:(char*)alloca((n))))
#define alloca_append(s1,s2) strcat(alloca_strdup2((s1),strlen((s2))),(s2));

void xfree(void *p);
void xmalloc_register_block(void *);

#include "xstring.h"

static inline void *xmemdup(const void *m,int len)
{
   if(!m) return 0;
   void *buf=xmalloc(len);
   memcpy(buf,m,len);
   return buf;
}

#endif /* XMALLOC_H */
