/*
 * lftp and utils
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include <string.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif
#include <unistd.h>
#include "module.h"
#include "confpaths.h"
#include "xalloca.h"

#ifdef RTLD_NOW
# define DLOPEN_FLAGS RTLD_NOW
#else
/* SunOS4 manual says it is reserved and must be 1 */
# define DLOPEN_FLAGS 1
#endif

typedef void (*init_t)(int,const char*const*);

/* XXX: this can go to a config file. */
static const char * const module_aliases[]=
{
   "proto-hftp",  "proto-http",
   "cmd-at",	  "cmd-sleep",
   "cmd-repeat",  "cmd-sleep",
   NULL
};

void *module_load(const char *path,int argc,const char *const *argv)
{
#ifdef HAVE_DLOPEN
   void *map;
   char *fullpath=(char*)alloca(strlen(PKGLIBDIR)+1+strlen(path)+3+1);
   init_t init;

   if(strchr(path,'/'))
      strcpy(fullpath,path);
   else
   {
      const char *const *scan;
      for(scan=module_aliases; *scan; scan+=2)
      {
	 if(!strcmp(path,*scan))
	 {
	    path=scan[1];
	    break;
	 }
      }
      sprintf(fullpath,"%s/%s",PKGLIBDIR,path);
   }
   if(access(fullpath,F_OK)==-1)
   {
      int len=strlen(fullpath);
      if(len>3 && strcmp(fullpath+len-3,".so"))
	 strcat(fullpath,".so");
   }
   map=dlopen(fullpath,DLOPEN_FLAGS);
   if(map==0)
      return 0;
   init=(init_t)dlsym(map,"module_init");
   if(init)
      (*init)(argc,argv);
   return map;
#else
   return 0;
#endif
}

const char *module_error_message()
{
#ifdef HAVE_DLOPEN
   return dlerror();
#else
   return "modules are not supported on this system";
#endif
}
