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
#include "GetPass.h"

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
   if(url.user && !url.pass)
   {
      url.pass=GetPass("ftp:proxy password: ");
      *p=(char*)xrealloc(*p,3*strlen(*p)+3*xstrlen(url.pass)+2);
      url.Combine(*p);
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
   sprintf(pass,"%s@",u);

   return pass;
}


// Static array of objects is wrongly initialized by IRIX CC and Unixware c++.
// So here goes list of arbitrarily named objects, they are not refered by name.
static ResDecl
   ResDecl00 ("ftp:anon-pass",		  "-lftp@",0,0),
   ResDecl01 ("ftp:anon-user",		  "anonymous",0,0),
   ResDecl02 ("ftp:fxp-passive-source",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl03 ("ftp:list-options",	  "",	   0,0),
   ResDecl04 ("ftp:nop-interval",	  "120",   ResMgr::UNumberValidate,0),
   ResDecl05 ("ftp:passive-mode",	  "on",    ResMgr::BoolValidate,0),
   ResDecl06 ("ftp:port-range",		  "full",  ResMgr::RangeValidate,0),
   ResDecl07 ("ftp:proxy",		  "",	   FtpProxyValidate,0),
   ResDecl08 ("ftp:rest-list",		  "no",	   ResMgr::BoolValidate,0),
   ResDecl09 ("ftp:rest-stor",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl10 ("ftp:skey-allow",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl11 ("ftp:skey-force",		  "no",    ResMgr::BoolValidate,0),
   ResDecl12 ("ftp:stat-interval",	  "1",	   ResMgr::UNumberValidate,0),
   ResDecl13 ("ftp:sync-mode",		  "on",    ResMgr::BoolValidate,0),
   ResDecl14 ("ftp:use-abor",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15 ("ftp:use-fxp",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl16 ("ftp:use-stat",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl17 ("ftp:use-quit",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl18 ("ftp:verify-address",	  "no",    ResMgr::BoolValidate,0),
   ResDecl19 ("ftp:verify-port",	  "no",    ResMgr::BoolValidate,0),
   ResDecl20 ("ftp:web-mode",		  "off",   ResMgr::BoolValidate,0),
   ResDecl21 ("hftp:cache",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl22 ("hftp:proxy",		  "",	   HttpProxyValidate,0),
   ResDecl23 ("hftp:use-head",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl24a("http:accept",		  "*/*",   0,0),
   ResDecl24b("http:accept-language",	  "",	   0,0),
   ResDecl24c("http:accept-charset",	  "",	   0,0),
   ResDecl24 ("http:cache",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl25 ("http:proxy",		  "",	   HttpProxyValidate,0),
   ResDecl26 ("http:user-agent",	  PACKAGE"/"VERSION,0,0),
   ResDecl27 ("http:cookie",		  "",	   0,0),
   ResDecl28 ("http:set-cookies",	  "no",	   0,0),
   ResDecl29 ("http:put-method",	  "PUT",   0,0),
   ResDecl30 ("http:put-content-type",	  "",	   0,0),
   ResDecl31 ("net:idle",		  "180",   ResMgr::UNumberValidate,0),
   ResDecl32 ("net:limit-max",		  "0",	   ResMgr::UNumberValidate,0),
   ResDecl33 ("net:limit-rate",		  "0",	   ResMgr::UNumberValidate,0),
   ResDecl34 ("net:limit-total-max",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl35 ("net:limit-total-rate",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl36 ("net:max-retries",	  "4096",  ResMgr::UNumberValidate,0),
   ResDecl36a("net:persist-retries",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl37 ("net:no-proxy",		  "",	   0,0),
   ResDecl38 ("net:reconnect-interval-base","30",  ResMgr::UNumberValidate,0),
   ResDecl39 ("net:reconnect-interval-multiplier","1.5",ResMgr::FloatValidate,0),
   ResDecl40 ("net:reconnect-interval-max","600",  ResMgr::UNumberValidate,0),
   ResDecl41 ("net:socket-buffer",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl42 ("net:socket-maxseg",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl43 ("net:timeout",		  "300",   ResMgr::UNumberValidate,0),
   ResDecl44 ("net:connection-limit",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl45 ("net:connection-takeover",  "yes",   ResMgr::BoolValidate,0),
   ResDecl46 ("mirror:time-precision",	  "1s",    ResMgr::TimeIntervalValidate,0),
   ResDecl47 ("mirror:loose-time-precision","24h", ResMgr::TimeIntervalValidate,0);

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
