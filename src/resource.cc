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

#include <pwd.h>
#include <unistd.h>

#include "ResMgr.h"
#include "url.h"

static const char *FtpProxyValidate(char **p)
{
   ParsedURL url(*p);
   if(url.host==0)
   {
      if((*p)[0]!=0)
	 (*p)[0]=0;
      return 0;
   }
   if(url.proto)
   {
      if(strcmp(url.proto,"ftp") /* TODO: && strcmp(url.proto,"http")*/)
	 return _("Proxy protocol unsupported");
   }
   return 0;
}

static const char *HttpProxyValidate(char **p)
{
   ParsedURL url(*p);
   if(url.host==0)
   {
      if((*p)[0]!=0)
	 (*p)[0]=0;
      return 0;
   }
   if(url.proto)
   {
      if(strcmp(url.proto,"http"))
	 return _("Proxy protocol unsupported");
   }
   return 0;
}

static const char *FtpDefaultAnonPass()
{
   static char *pass=0;

   if(pass)
      return pass;

   struct passwd *pw=getpwuid(getuid());
   const char *u=pw?pw->pw_name:"unknown";
   pass=(char*)xmalloc(strlen(u)+3);
   sprintf(pass,"-%s@",u);

   return pass;
}


static ResDecl resources[]={
   ResDecl  ("ftp:sync-mode",      "on", ResMgr::BoolValidate,0),
   ResDecl  ("ftp:timeout",	   "600",ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:reconnect-interval","30", ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:passive-mode",   "off",ResMgr::BoolValidate,0),
   ResDecl  ("ftp:relookup-always","off",ResMgr::BoolValidate,0),
   ResDecl  ("ftp:proxy",          "",   FtpProxyValidate,0),
   ResDecl  ("ftp:nop-interval",   "120",ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:idle",	   "180",ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:max-retries",    "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:skey-allow",     "yes",ResMgr::BoolValidate,0),
   ResDecl  ("ftp:skey-force",     "no", ResMgr::BoolValidate,0),
   ResDecl  ("ftp:anon-user",      "anonymous",0,0),
   ResDecl  ("ftp:anon-pass",      FtpDefaultAnonPass(),0,0),
   ResDecl  ("ftp:socket-buffer",  "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:socket-maxseg",  "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:verify-address", "no", ResMgr::BoolValidate,0),
   ResDecl  ("ftp:verify-port",    "no", ResMgr::BoolValidate,0),
   ResDecl  ("ftp:limit-rate",     "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:limit-max",      "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("http:timeout",        "600",ResMgr::UNumberValidate,0),
   ResDecl  ("http:reconnect-interval","30", ResMgr::UNumberValidate,0),
   ResDecl  ("http:relookup-always","off",ResMgr::BoolValidate,0),
   ResDecl  ("http:proxy",          "",   HttpProxyValidate,0),
   ResDecl  ("http:idle",	    "30",ResMgr::UNumberValidate,0),
   ResDecl  ("http:max-retries",    "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("http:socket-buffer",  "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("http:socket-maxseg",  "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("http:limit-rate",     "0",  ResMgr::UNumberValidate,0),
   ResDecl  ("http:limit-max",      "0",  ResMgr::UNumberValidate,0)
};

void ResMgr::ClassInit()
{
   // nothing to do, actually. ctors do all the work
}
