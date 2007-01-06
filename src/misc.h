/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef MISC_H
#define MISC_H

#include "trio.h"
#include <sys/types.h>
#include <sys/time.h>
#ifdef TIME_WITH_SYS_TIME
# include <time.h>
#endif
#include <stdarg.h>

// expands tilde; returns pointer to static data
const char *expand_home_relative(const char *);

// returns ptr to last path element
const char *basename_ptr(const char *);
static inline char *basename_ptr(char *f) {
   return const_cast<char*>(basename_ptr(const_cast<const char *>(f)));
}

// glues file to dir; returns pointer to static storage
const char *dir_file(const char *dir,const char *file);

// glues file to given url; returns pointer to static storage
const char *url_file(const char *url,const char *file);

const char *output_file_name(const char *src,const char *dst,bool dst_local,
			     const char *dst_base,bool make_dirs);

const char *squeeze_file_name(const char *name,int w);

// mkdir -p
int   create_directories(char *);

// rm -rf
void  truncate_file_tree(const char *dir);

/* returns file descriptor terminal width; -1 on error, 0 on N/A */
int fd_width(int fd);

// returns malloc'ed cwd no matter how long it is
// returns 0 on error.
char *xgetcwd();

int percent(off_t offset,off_t size);

#define find_char(buf,len,ch) ((const char *)memchr(buf,ch,len))

extern const char month_names[][4];

int parse_month(const char *);
int parse_perms(const char *);
const char *format_perms(int p);
int parse_year_or_time(const char *year_or_time,int *year,int *hour,int *minute);
int guess_year(int month,int day,int hour,int minute);

time_t mktime_from_utc(const struct tm *);
time_t mktime_from_tz(struct tm *,const char *tz);

bool re_match(const char *line,const char *a,int flags=0);

struct subst_t {
   char from;
   const char *to;
};

/* Subst changes escape sequences to given strings, also substitutes \nnn
 * with corresponding character. Returns allocated buffer to be free'd */
char *Subst(const char *txt, const subst_t *s);

char **tokenize(const char *str, int *argc = NULL);
void tokenize_free(char **argv);

/* uses gettimeofday if available */
void xgettimeofday(time_t *sec, int *usec);

/* returns malloc'd date */
char *xstrftime(const char *format, const struct tm *tm);

/* returns malloc'd formatted string */
char *xvasprintf(const char *format, va_list ap);
char *xasprintf(const char *format, ...);

char *dirname_alloc(const char *fn);
char *dirname_modify(char *fn);
char *strip_trailing_slashes(char *fn);

/* returns last character of string or \0 if string is empty */
char last_char(const char *str);

int  base64_length (int len);
void base64_encode (const char *s, char *store, int length);

bool temporary_network_error(int e);

CDECL const char *get_lftp_home();

const char *memrchr(const char *buf,char c,size_t len);
static inline char *memrchr(char *buf,char c,size_t len) {
   return const_cast<char*>(memrchr(const_cast<const char*>(buf),c,len));
}

const char *shell_encode(const char *);

#define ListAdd(type,chain,this,next)  \
do {				       \
   this->next=chain;		       \
   chain=this;			       \
} while(0)

#define ListDel(type,chain,this,next)			 \
do {							 \
   for(type **scan=&chain; *scan; scan=&scan[0]->next)	 \
      if(*scan==this)					 \
      {							 \
	 delete replace_value(*scan,scan[0]->next);	 \
	 break;						 \
      }							 \
} while(0)

#define ListScan(type,chain,next) \
   for(type *scan=chain; scan; scan=scan->next)

#endif // MISC_H
