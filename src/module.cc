/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <string.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif
#include <unistd.h>
#include <stddef.h>
#include "module.h"
#include "ResMgr.h"
#include "configmake.h"
#include "xstring.h"

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

static ResDecl res_mod_path("module:path", PKGLIBDIR "/" VERSION ":" PKGLIBDIR, 0,0);

/* dlopen can take a file without extension and automatically do the
 * right thing, however that doesn't fit with this code that tries to
 * stat before the dlopen call, hence need some help here */
#if defined(__MACH__) && defined(__APPLE__)
static const char ext[] = ".bundle";
#else
static const char ext[] = ".so";
#endif

static int access_so(xstring &fullpath)
{
   int res=access(fullpath,F_OK);
   if(res==-1)
   {
      if(!fullpath.ends_with(ext))
	 fullpath.append(ext);
      res=access(fullpath,F_OK);
   }
   return res;
}

static const char *find_module_alias(const char *path)
{
   const char *const *scan;
   for(scan=module_aliases; *scan; scan+=2)
      if(!strcmp(path,*scan))
	 return scan[1];
   return path;
}

void *module_load(const char *path,int argc,const char *const *argv)
{
#ifdef HAVE_DLOPEN
   void *map;
   const char *modules_path=res_mod_path.Query(path);
   xstring fullpath;
   init_t init;

   if(strchr(path,'/'))
   {
      fullpath.set(path);
      access_so(fullpath);
   }
   else
   {
      path=find_module_alias(path);
      char *p=alloca_strdup(modules_path);
      for(p=strtok(p,":"); p; p=strtok(0,":"))
      {
	 fullpath.vset(p,"/",path,NULL);
	 if(access_so(fullpath)==0)
	    break;
      }
      if(p==0)
      {
	 fullpath.vset(PKGLIBDIR,"/",VERSION,"/",path,NULL); // fallback
	 access_so(fullpath);
      }
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

bool module_init_preloaded(const char *module)
{
#if defined(HAVE_DLOPEN) && defined(RTLD_DEFAULT)
   module=find_module_alias(module);
   char *init_fn=alloca_strdup2(module,12);
   // change dashes to underscores in the function name
   for(char *scan=init_fn; *scan; scan++)
      if(*scan=='-')
	 *scan='_';
   strcat(init_fn,"_module_init");
   init_t init=(init_t)dlsym(RTLD_DEFAULT,init_fn);
   if(init) {
      (*init)(0,0);
      return true;
   }
#endif
   return false;
}
