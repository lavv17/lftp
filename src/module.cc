/*
 * lftp and utils
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "xstring.h"
#include "xmalloc.h"

#ifdef RTLD_NOW
# ifndef RTLD_GLOBAL
#  define RTLD_GLOBAL 0
# endif
# define DLOPEN_FLAGS RTLD_NOW|RTLD_GLOBAL
#else
/* SunOS4 manual says it is reserved and must be 1 */
# define DLOPEN_FLAGS 1
#endif
#ifdef RTLD_LAZY
# define DLOPEN_FLAGS_LAZY RTLD_LAZY
#else
# define DLOPEN_FLAGS_LAZY DLOPEN_FLAGS
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

class module_info
{
   module_info *next;
   static module_info *base;

   char *path;
   void *addr;

public:
   module_info(const char *p,void *a)
      {
	 path=xstrdup(p);
	 addr=a;
	 next=base;
	 base=this;
      }
   ~module_info()
      {
	 xfree(path);
	 for(module_info **scan=&base; *scan; scan=&scan[0]->next)
	 {
	    if(*scan==this)
	    {
	       *scan=scan[0]->next;
	       break;
	    }
	 }
      }
   static module_info *find_module(const char *name)
      {
	 int name_len=strlen(name);
	 for(module_info *scan=base; scan; scan=scan->next)
	 {
	    char *slash=strrchr(scan->path,'/');
	    char *scan_name=(slash?slash+1:scan->path);
	    if(!strncmp(scan_name,name,name_len)
	    && (scan_name[name_len]==0 || !strcmp(&scan_name[name_len],".so")))
	    {
	       return scan;
	    }
	 }
	 return 0;
      }
   static void delete_by_name(const char *name)
      {
	 module_info *m=find_module(name);
	 if(m)
	    delete m;
      }
};
module_info *module_info::base;

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
   map=dlopen(fullpath,DLOPEN_FLAGS); //DLOPEN_FLAGS_LAZY);
   if(map==0)
      return 0;
   (void)new module_info(fullpath,map);
#if 0 // for some reason this does not work.
   const char*const*depend=(const char*const*)dlsym(map,"module_depend");
   if(depend)
   {
      while(*depend)
      {
	 if(module_info::find_module(*depend)==0)
	 {
	    void *dep=module_load(*depend,0,0);
	    if(!dep)
	       fprintf(stderr,_("depend module `%s': %s\n"),*depend,module_error_message());
	 }
	 depend++;
      }
   }
#if DLOPEN_FLAGS!=DLOPEN_FLAGS_LAZY
   dlclose(map);
   // reopen it with RTLD_NOW
   map=dlopen(fullpath,DLOPEN_FLAGS);
   if(map==0)
   {
      module_info::delete_by_name(fullpath);
      return 0;
   }
#endif
#endif //0
   init=(init_t)dlsym(map,"module_init");
   if(init)
      (*init)(argc,argv);
   return map;
#else // !HAVE_DLOPEN
   return 0;
#endif
}

const char *module_error_message()
{
#ifdef HAVE_DLOPEN
   return dlerror();
#else
   return _("modules are not supported on this system");
#endif
}
