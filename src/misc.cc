/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "xstring.h"
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <sys/ioctl.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#include <sys/time.h>
#ifdef TIME_WITH_SYS_TIME
# include <time.h>
#endif

#include <regex.h>
#include "misc.h"
#include "ProcWait.h"
#include "SignalHook.h"
#include "url.h"

const char *dir_file(const char *dir,const char *file)
{
   if(dir==0 || dir[0]==0)
      return file?file:dir;
   if(file==0 || file[0]==0)
      return dir;
   if(file[0]=='/')
      return file;

   static char *buf=0;
   static int buf_size=0;

   if(buf && dir==buf) // it is possible to dir_file(dir_file(dir,dir),file)
      dir=alloca_strdup(dir);

   int len=strlen(dir)+1+strlen(file)+1;
   if(buf_size<len)
      buf=(char*)xrealloc(buf,buf_size=len);
   len=strlen(dir);
   if(len==0)
      sprintf(buf,"%s",file);
   else if(dir[len-1]=='/')
      sprintf(buf,"%s%s",dir,file);
   else
      sprintf(buf,"%s/%s",dir,file);
   return buf;
}

const char *url_file(const char *url,const char *file)
{
   static char *buf=0;
   static int buf_size=0;

   if(buf && url==buf) // it is possible to url_file(url_file(url,dir),file)
      url=alloca_strdup(url);

   int len=3*xstrlen(url)+4+3*xstrlen(file)+1;
   if(buf_size<len)
      buf=(char*)xrealloc(buf,buf_size=len);
   if(!url || url[0]==0)
   {
      strcpy(buf,file);
      return buf;
   }
   ParsedURL u(url);
   if(!u.proto)
   {
      strcpy(buf,dir_file(url,file));
      return buf;
   }
   if(file[0]=='/' || file[0]=='~' || u.path==0 || u.path[0]==0)
   {
      u.path=(char*)file;
      u.Combine(buf);
      return buf;
   }
   url::encode_string(file,buf,URL_PATH_UNSAFE);
   return dir_file(url,buf);
}

const char *output_file_name(const char *src,const char *dst,bool dst_local,
			     const char *dst_base,bool make_dirs)
{
   bool dst_is_dir=false;
   if(dst)
   {
      if(dst_base)
	 dst=url_file(dst_base,dst);
      if(url::is_url(dst))
	 dst_local=false;
      if(dst_local)
      {
	 dst=expand_home_relative(dst);
	 struct stat st;
	 if(stat(dst,&st)!=-1 && S_ISDIR(st.st_mode))
	    dst_is_dir=true;
      }
      int len=strlen(dst);
      if(len>0 && dst[len-1]=='/')
	 dst_is_dir=true;
      if(!dst_is_dir)
	 return dst;
   }

   ParsedURL u_src(src,true);
   if(u_src.proto)
      src=u_src.path;
   if(!src)
      return "";  // there will be error anyway.
   const char *base=basename_ptr(src);
   if(make_dirs && !dst)
   {
      base=src;
      if(base[0]=='~')
      {
	 base=strchr(base,'/');
	 if(!base)
	    base="";
      }
      while(*base=='/')
	 base++;
   }
   return url_file(dst?dst:dst_base,base);
}

const char *basename_ptr(const char *s)
{
   const char *s1=s+strlen(s);
   while(s1>s && s1[-1]=='/')
      s1--;
   while(s1>s && s1[-1]!='/')
      s1--;
   return s1;
}

const char *expand_home_relative(const char *s)
{
   if(s[0]=='~')
   {
      const char *home=0;
      const char *sl=strchr(s+1,'/');;
      static char *ret_path=0;

      if(s[1]==0 || s[1]=='/')
      {
	 home=getenv("HOME");
      }
      else
      {
	 // extract user name and find the home
	 int name_len=(sl?sl-s-1:strlen(s+1));
	 char *name=(char*)alloca(name_len+1);
	 strncpy(name,s+1,name_len);
	 name[name_len]=0;

	 struct passwd *pw=getpwnam(name);
	 if(pw)
	    home=pw->pw_dir;
      }
      if(home==0)
	 return s;

      if(sl)
      {
	 ret_path=(char*)xrealloc(ret_path,strlen(sl)+strlen(home)+1);
	 strcpy(ret_path,home);
	 strcat(ret_path,sl);
	 return ret_path;
      }
      return home;
   }
   return s;
}

int   create_directories(char *path)
{
   char  *sl=path;
   int	 res;

   if(access(path,0)==0)
      return 0;

   for(;;)
   {
      sl=strchr(sl,'/');
      if(sl==path)
      {
	 sl++;
	 continue;
      }
      if(sl)
	 *sl=0;
      if(access(path,0)==-1)
      {
	 res=mkdir(path,0777);
	 if(res==-1)
	 {
	    if(errno!=EEXIST)
	    {
	       fprintf(stderr,"mkdir(%s): %s\n",path,strerror(errno));
	       if(sl)
		  *sl='/';
	       return(-1);
	    }
	 }
      }
      if(sl)
	 *sl++='/';
      else
	 break;
   }
   return 0;
}

void  truncate_file_tree(const char *dir)
{
   fflush(stderr);
   pid_t pid;
   switch(pid=fork())
   {
   case(0): // child
      SignalHook::Ignore(SIGINT);
      SignalHook::Ignore(SIGTSTP);
      SignalHook::Ignore(SIGQUIT);
      SignalHook::Ignore(SIGHUP);
      execlp("rm","rm","-rf",dir,NULL);
      perror("execlp(rm)");
      fflush(stderr);
      _exit(1);
   case(-1):   // error
      perror("fork()");
      return;
   default: // parent
      (new ProcWait(pid))->Auto();  // don't wait for termination
   }
}

int fd_width(int fd)
{
   if(fd == -1) return -1;
   if(!isatty(fd)) return 0;

#ifdef TIOCGWINSZ
   struct winsize sz;
   sz.ws_col=sz.ws_row=0;
   ioctl(fd,TIOCGWINSZ,&sz);
   if(sz.ws_col==0)
      sz.ws_col=80;
   return(sz.ws_col);
#else /* !TIOCGWINSZ */
   return 80;
#endif
}

char *xgetcwd()
{
   int size=256;
   for(;;)
   {
      char *cwd=getcwd(0,size);
      if(cwd)
      {
	 xmalloc_register_block(cwd);
	 return cwd;
      }
      if(errno!=ERANGE)
	 return 0;
      size*=2;
   }
}

int parse_perms(const char *s)
{
   int p=0;

   if(strlen(s)!=9
   && !(strlen(s)==10 && s[9]=='+'))   // ACL tag
      bad: return -1;

   switch(s[0])
   {
   case('r'): p|=S_IRUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISUID; break;
   case('s'): p|=S_ISUID; // fall-through
   case('x'): p|=S_IXUSR; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IRGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISGID; break;
   case('s'): p|=S_ISGID; // fall-through
   case('x'): p|=S_IXGRP; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IROTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWOTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('T'): case('t'): p|=S_ISVTX; break;
   case('l'): case('L'): p|=S_ISGID; p&=~S_IXGRP; break;
   case('x'): p|=S_IXOTH; break;
   case('-'): break;
   default: goto bad;
   }

   return p;
}

// it does not prepend file type.
const char *format_perms(int p)
{
   static char s[10];
   memset(s,'-',9);
   if(p&0400) s[0]='r';
   if(p&0200) s[1]='w';
   if(p&0100) s[2]='x';
   if(p&0040) s[3]='r';
   if(p&0020) s[4]='w';
   if(p&0010) s[5]='x';
   if(p&0004) s[6]='r';
   if(p&0002) s[7]='w';
   if(p&0001) s[8]='x';
   if(p&01000) s[8]=(p&0001?'t':'T');
   if(p&02000) s[5]=(p&0010?'s':'S');
   if(p&04000) s[2]=(p&0100?'s':'S');
   return s;
}

const char month_names[][4]={
   "Jan","Feb","Mar","Apr","May","Jun",
   "Jul","Aug","Sep","Oct","Nov","Dec",
   ""
};
int parse_month(const char *m)
{
   for(int i=0; month_names[i][0]; i++)
      if(!strcasecmp(month_names[i],m))
	 return(i%12);
   return -1;
}

int parse_year_or_time(const char *year_or_time,int *year,int *hour,int *minute)
{
   if(year_or_time[2]==':')
   {
      if(2!=sscanf(year_or_time,"%2d:%2d",hour,minute))
	 return -1;
      *year=-1;
   }
   else
   {
      if(1!=sscanf(year_or_time,"%d",year))
	 return -1;;
      *hour=*minute=0;
   }
   return 0;
}
int guess_year(int month,int day,int hour,int minute)
{
   time_t curr=time(0);
   struct tm &now=*localtime(&curr);
   int year=now.tm_year+1900;
   if(((month     *32+        day)*64+       hour)*64+       minute
    > ((now.tm_mon*32+now.tm_mday)*64+now.tm_hour)*64+now.tm_min)
      year--;
   return year;
}
int percent(off_t offset,off_t size)
{
   if(offset>=size)
      return 100;
   // use floating point to avoid integer overflow.
   return int(double(offset)*100/size);
}

const char *squeeze_file_name(const char *name,int w)
{
   static char *buf;
   static int buf_len;

   int name_len=strlen(name);

   if(name_len<=w)
      return name;

   if(buf_len<w+20)
      buf=(char*)xrealloc(buf,buf_len=w+20);

   const char *b=basename_ptr(name);
   int b_len=name_len-(b-name);
   if(b_len<=w-4 && b_len>w-15)
   {
      strcpy(buf,".../");
      strcat(buf,b);
      return buf;
   }
   b=name+name_len-w;
   if(w<4)
      return b;
   strcpy(buf,"...");
   strcat(buf,b+3);
   return buf;
}

/* Converts struct tm to time_t, assuming the data in tm is UTC rather
   than local timezone (mktime assumes the latter).

   Contributed by Roger Beeman <beeman@cisco.com>, with the help of
   Mark Baushke <mdb@cisco.com> and the rest of the Gurus at CISCO.  */
time_t
mktime_from_utc (struct tm *t)
{
  time_t tl, tb;

  tl = mktime (t);
  if (tl == -1)
    return -1;
  tb = mktime (gmtime (&tl));
  return (tl <= tb ? (tl + (tl - tb)) : (tl - (tb - tl)));
}


bool re_match(const char *line,const char *a,int flags)
{
   if(!a || !*a)
      return false;
   regex_t re;
   if(regcomp(&re,a,REG_EXTENDED|REG_NOSUB|flags))
      return false;
   bool res=(0==regexec(&re,line,0,0,0));
   regfree(&re);
   return res;
}

char *Subst(const char *txt, const subst_t *s)
{
   char *buf=0;
   int buf_size=256;

   if(buf==0)
      buf=(char*)xmalloc(buf_size);

   char *store=buf;

   char str[3];

   *store=0;
   while(*txt)
   {
      char ch = *txt++;
      const char *to_add = NULL;
      if(ch=='\\' && *txt && *txt!='\\')
      {
	 ch=*txt++;
	 if(isdigit(ch) && ch != '8' && ch != '9') {
	    unsigned len;
	    unsigned code;
	    txt--;
	    sscanf(txt,"%3o%n",&code,&len);
	    ch=code;
	    txt+=len;
	    str[0]=ch;
	    str[1]=0;
	    to_add=str;
	 } else {
	    for(int i = 0; s[i].from; i++) {
	       if(s[i].from != ch) continue;
	       to_add=s[i].to;
	       if(!to_add) to_add = "";
	    }
	    if(!to_add) {
	       str[0]='\\';
	       str[1]=ch;
	       str[2]=0;
	       to_add=str;
	    }
	 }
      }
      else
      {
	 if(ch=='\\' && *txt=='\\')
	    txt++;
	 str[0]=ch;
	 str[1]=0;
	 to_add=str;
      }

      if(to_add==0)
	 continue;

      int store_index=store-buf;
      int need=store_index+strlen(to_add)+1;
      if(buf_size<need)
      {
	 while(buf_size<need)
	    buf_size*=2;
	 buf=(char*)xrealloc(buf,buf_size);
	 store=buf+store_index;
      }

      strcpy(store,to_add);
      store+=strlen(to_add);
   }
   return(buf);
}

/* if we put escape-handling, etc. in here, the main parser
 * could possibly use it */
char **tokenize(const char *str, int *argc)
{
   char *buf = xstrdup(str);
   char **argv = NULL;
   int _argc;
   if(!argc) argc = &_argc;
   *argc = 0;

   for(int i = 0; buf[i]; ) {
      (*argc)++;
      argv = (char **) xrealloc(argv, sizeof(char *) * (*argc));
      argv[*argc-1] = buf+i;

      while(buf[i] && buf[i] != ' ') i++;
      if(buf[i] == ' ') buf[i++] = 0;
   }

   argv = (char **) xrealloc(argv, sizeof(char *) * (*argc+1));
   argv[*argc] = NULL;
   return argv;
}

void tokenize_free(char **argv)
{
   if(!argv) return;
   xfree(argv[0]);
   xfree(argv);
}

void xgettimeofday(time_t *sec, int *usec)
{
#ifdef HAVE_GETTIMEOFDAY
   struct timeval tv;
   gettimeofday(&tv,0);
   if(sec) *sec = tv.tv_sec;
   if(usec) *usec = tv.tv_usec;
#else
   if(sec) time(sec);
   if(usec) *usec = 0;
#endif
}
