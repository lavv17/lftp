/*
 * lftp and utils
 *
 * Copyright (c) 1999-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "trio.h"
#include <string.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif
#include <unistd.h>
#include "module.h"
#include "ResMgr.h"
#include "confpaths.h"
#include "xstring.h"
#include "xmalloc.h"

#ifndef RTLD_GLOBAL
# define RTLD_GLOBAL 0
#endif
#ifdef RTLD_NOW
# define DLOPEN_FLAGS RTLD_NOW|RTLD_GLOBAL
#else
/* SunOS4 manual says it is reserved and must be 1 */
# define DLOPEN_FLAGS 1
#endif
#ifdef RTLD_LAZY
# define DLOPEN_FLAGS_LAZY RTLD_LAZY|RTLD_GLOBAL
#else
# define DLOPEN_FLAGS_LAZY DLOPEN_FLAGS
#endif

typedef void (*init_t)(int,const char*const*);

/* TODO: this can go to a config file. */
static const char * const module_aliases[]=
{
   "proto-hftp",  "proto-http",
#if USE_SSL
   "proto-https", "proto-http",
   "proto-ftps",  "proto-ftp",
#endif
   "cmd-at",	  "cmd-sleep",
   "cmd-repeat",  "cmd-sleep",
   NULL
};

class lftp_module_info
{
   lftp_module_info *next;
   static lftp_module_info *base;

   char *path;
   void *addr;

public:
   lftp_module_info(const char *p,void *a)
      {
	 path=xstrdup(p);
	 addr=a;
	 next=base;
	 base=this;
      }
   ~lftp_module_info()
      {
	 xfree(path);
	 for(lftp_module_info **scan=&base; *scan; scan=&scan[0]->next)
	 {
	    if(*scan==this)
	    {
	       *scan=scan[0]->next;
	       break;
	    }
	 }
      }
   static lftp_module_info *find_module(const char *name)
      {
	 int name_len=strlen(name);
	 for(lftp_module_info *scan=base; scan; scan=scan->next)
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
	 lftp_module_info *m=find_module(name);
	 if(m)
	    delete m;
      }
};
lftp_module_info *lftp_module_info::base;

static ResDecl res_mod_path("module:path", PKGLIBDIR"/"VERSION":"PKGLIBDIR, 0,0);

void *module_load(const char *path,int argc,const char *const *argv)
{
#ifdef HAVE_DLOPEN
   void *map;
   const char *modules_path=res_mod_path.Query(path);
   char *fullpath=(char*)alloca(strlen(modules_path)+strlen(PKGLIBDIR)+1+strlen(path)+3+1);
   init_t init;

   if(strchr(path,'/'))
   {
      strcpy(fullpath,path);
      if(access(fullpath,F_OK)==-1)
      {
	 int len=strlen(fullpath);
	 if(len>3 && strcmp(fullpath+len-3,".so"))
	    strcat(fullpath,".so");
      }
   }
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
      char *p=alloca_strdup(modules_path);
      for(p=strtok(p,":"); p; p=strtok(0,":"))
      {
	 sprintf(fullpath,"%s/%s",p,path);
	 if(access(fullpath,F_OK)==0)
	    break;
	 int len=strlen(fullpath);
	 if(len>3 && strcmp(fullpath+len-3,".so"))
	 {
	    strcat(fullpath,".so");
	    if(access(fullpath,F_OK)==0)
	       break;
	 }
      }
      if(p==0)
	 sprintf(fullpath,"%s/%s/%s.so",PKGLIBDIR,VERSION,path); // fallback
   }
   map=dlopen(fullpath,DLOPEN_FLAGS);  // LAZY?
   if(map==0)
      return 0;
   (void)new lftp_module_info(fullpath,map);
#if 0 // for some reason this does not work even with LAZY (because of _init?).
   const char*const*depend=(const char*const*)dlsym(map,"module_depend");
   if(depend)
   {
      while(*depend)
      {
	 if(lftp_module_info::find_module(*depend)==0)
	 {
	    void *dep=module_load(*depend,0,0);
	    if(!dep)
	       fprintf(stderr,_("depend module `%s': %s\n"),*depend,module_error_message());
	 }
	 depend++;
      }
   }
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
