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
#include "xmalloc.h"
#include "xstring.h"
#include <stdio.h>
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "misc.h"
#include "ProcWait.h"
#include "SignalHook.h"

const char *dir_file(const char *dir,const char *file)
{
   if(dir==0 || dir[0]==0)
      return file;
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
	 res=mkdir(path,0755);
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

char *xgetcwd()
{
   int size=256;
   for(;;)
   {
      char *cwd=getcwd(0,size);
      if(cwd)
	 return cwd;
      if(errno!=ERANGE)
	 return 0;
      size*=2;
   }
}
