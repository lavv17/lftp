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

#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

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
   ResDecl  ("ftp:anon-pass",	       "-lftp@",0,0),
   ResDecl  ("ftp:anon-user",	       "anonymous",0,0),
   ResDecl  ("ftp:fxp-passive-source", "no",	ResMgr::BoolValidate,0),
   ResDecl  ("ftp:list-options",       "",0,0),
   ResDecl  ("ftp:nop-interval",       "120",	ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:passive-mode",       "on",    ResMgr::BoolValidate,0),
   ResDecl  ("ftp:port-range",	       "full",	ResMgr::RangeValidate,0),
   ResDecl  ("ftp:proxy",	       "",	FtpProxyValidate,0),
   ResDecl  ("ftp:rest-list",	       "no",	ResMgr::BoolValidate,0),
   ResDecl  ("ftp:rest-stor",	       "yes",	ResMgr::BoolValidate,0),
   ResDecl  ("ftp:skey-allow",	       "yes",   ResMgr::BoolValidate,0),
   ResDecl  ("ftp:skey-force",	       "no",    ResMgr::BoolValidate,0),
   ResDecl  ("ftp:stat-interval",      "1",	ResMgr::UNumberValidate,0),
   ResDecl  ("ftp:sync-mode",	       "on",    ResMgr::BoolValidate,0),
   ResDecl  ("ftp:use-abor",	       "yes",   ResMgr::BoolValidate,0),
   ResDecl  ("ftp:use-fxp",	       "yes",   ResMgr::BoolValidate,0),
   ResDecl  ("ftp:use-stat",	       "yes",   ResMgr::BoolValidate,0),
   ResDecl  ("ftp:verify-address",     "no",    ResMgr::BoolValidate,0),
   ResDecl  ("ftp:verify-port",	       "no",    ResMgr::BoolValidate,0),
   ResDecl  ("ftp:web-mode",	       "off",	ResMgr::BoolValidate,0),
   ResDecl  ("hftp:cache",	       "yes",	ResMgr::BoolValidate,0),
   ResDecl  ("hftp:proxy",	       "",	HttpProxyValidate,0),
   ResDecl  ("hftp:use-head",	       "yes",	ResMgr::BoolValidate,0),
   ResDecl  ("http:cache",	       "yes",	ResMgr::BoolValidate,0),
   ResDecl  ("http:proxy",	       "",	HttpProxyValidate,0),
   ResDecl  ("http:user-agent",	       PACKAGE"/"VERSION,0,0),
   ResDecl  ("net:idle",	       "180",   ResMgr::UNumberValidate,0),
   ResDecl  ("net:limit-max",	       "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:limit-rate",	       "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:limit-total-max",    "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:limit-total-rate",   "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:max-retries",	       "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:no-proxy",	       "",	0,0),
   ResDecl  ("net:reconnect-interval-base","30",ResMgr::UNumberValidate,0),
   ResDecl  ("net:reconnect-interval-multiplier","1.5",ResMgr::FloatValidate,0),
   ResDecl  ("net:reconnect-interval-max","600",ResMgr::UNumberValidate,0),
   ResDecl  ("net:socket-buffer",      "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:socket-maxseg",      "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:timeout",	       "300",   ResMgr::UNumberValidate,0),
   ResDecl  ("net:connection-limit",   "0",	ResMgr::UNumberValidate,0),
   ResDecl  ("net:connection-takeover","yes",	ResMgr::BoolValidate,0),
};

void ResMgr::ClassInit()
{
   // make anon-pass visible
   Set("ftp:anon-pass",0,FtpDefaultAnonPass());

   // inherit http proxy from environment
   const char *http_proxy=getenv("http_proxy");
   if(http_proxy)
   {
      Set("http:proxy",0,http_proxy);
      Set("hftp:proxy",0,http_proxy);
   }

   const char *ftp_proxy=getenv("ftp_proxy");
   if(ftp_proxy)
   {
      if(!strncmp(ftp_proxy,"ftp://",6))
	 Set("ftp:proxy",0,ftp_proxy);
      else if(!strncmp(ftp_proxy,"http://",7))
	 Set("hftp:proxy",0,ftp_proxy);
   }

   const char *no_proxy=getenv("no_proxy");
   if(no_proxy)
      Set("net:no-proxy",0,no_proxy);

#if INET6
   // check if ipv6 is really supported
   int s=socket(AF_INET6,SOCK_STREAM,IPPROTO_TCP);
   if(s==-1 && (errno==EINVAL
#ifdef EAFNOSUPPORT
      || errno==EAFNOSUPPORT
#endif
   ))
   {
      Set("dns:order",0,"inet");
   }
   if(s!=-1)
      close(s);
#endif // INET6
}
