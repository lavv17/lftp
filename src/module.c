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

typedef void (*init_t)(int,const char*const*);

void *module_load(const char *path,int argc,const char *const *argv)
{
#ifdef HAVE_DLOPEN
   void *map;
   char *fullpath=(char*)alloca(strlen(PKGLIBDIR)+1+strlen(path)+3+1);
   init_t init;

   if(strchr(path,'/'))
      strcpy(fullpath,path);
   else
      sprintf(fullpath,"%s/%s",PKGLIBDIR,path);
   if(access(fullpath,F_OK)==-1)
   {
      int len=strlen(fullpath);
      if(len>3 && strcmp(fullpath+len-3,".so"))
	 strcat(fullpath,".so");
   }
   map=dlopen(fullpath,RTLD_NOW);
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
