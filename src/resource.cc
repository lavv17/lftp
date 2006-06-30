/*
 * lftp and utils
 *
 * Copyright (c) 1999-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "ascii_ctype.h"
#include "confpaths.h"

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
      if(strcmp(url.proto,"ftp") && strcmp(url.proto,"http"))
	 return _("Proxy protocol unsupported");
   }
   if(url.user && !url.pass)
   {
      url.pass=GetPass(_("ftp:proxy password: "));
      xfree(*p);
      *p=url.Combine();
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
      if(strcmp(url.proto,"http")
      && strcmp(url.proto,"https"))
	 return _("Proxy protocol unsupported");
   }
   return 0;
}

#if 0
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
#endif

static const char *PutOrPost(char **s)
{
   if(strcasecmp(*s,"PUT") && strcasecmp(*s,"POST"))
      return _("only PUT and POST values allowed");
   for(char *scan=*s; *scan; scan++)
      *scan=to_ascii_upper((unsigned char)*scan);
   return 0;
}

static const char *const af_list[]=
{
   "inet",
#if INET6
   "inet6",
#endif
   0
};
static
const char *OrderValidate(char **s)
{
   static char *error=0;

   const char * const delim="\t ";
   char *s1=alloca_strdup(*s);
   char *fixed=(char*)xmalloc(strlen(s1)+1);
   fixed[0]=0;

   for(s1=strtok(s1,delim); s1; s1=strtok(0,delim))
   {
      const char *const *f;
      for(f=af_list; *f; f++)
      {
	 if(!strcasecmp(s1,*f))
	    break;
      }
      if(!*f)
      {
	 const char * const format=_("unknown address family `%s'");
	 error=(char*)xrealloc(error,strlen(format)+strlen(s1));
	 sprintf(error,format,s1);
	 return error;
      }
      if(fixed[0])
	 strcat(fixed," ");
      strcat(fixed,s1);
   }
   xfree(*s);
   *s=fixed;
   return 0;
}

#if USE_SSL
static
const char *AuthArgValidate(char **s)
{
   for(char *i=*s; *i; i++)
      *i=to_ascii_upper(*i);

   if(strcmp(*s,"SSL")
   && strcmp(*s,"TLS")
   && strcmp(*s,"TLS-P")
   && strcmp(*s,"TLS-C"))
      return _("ftp:ssl-auth must be one of: SSL, TLS, TLS-P, TLS-C");

   return 0;
}
static
const char *ProtValidate(char **s)
{
   if(!**s)
      return 0;

   for(char *i=*s; *i; i++)
      *i=to_ascii_upper(*i);

   if(strcmp(*s,"P")
   && strcmp(*s,"C")
   && strcmp(*s,"S")
   && strcmp(*s,"E"))
      return _("must be one of: C, S, E, P, or empty");

   return 0;
}
#endif

static ResType lftp_vars[] = {
   {"ftp:abor-max-wait",	 "15s",	  ResMgr::TimeIntervalValidate,0},
   {"ftp:acct",			 "",	  0,0},
   {"ftp:anon-pass",		 "lftp@", 0,0},
   {"ftp:anon-user",		 "anonymous",0,0},
   {"ftp:auto-sync-mode",	 "",	  ResMgr::ERegExpValidate,0},
   {"ftp:auto-passive-mode",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:bind-data-socket",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:charset",		 "",	  ResMgr::CharsetValidate,0},
   {"ftp:client",		 PACKAGE"/"VERSION,0,0},
   {"ftp:device-prefix",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fix-pasv-address",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ignore-pasv-address",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fxp-force",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fxp-passive-source",	 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"ftp:fxp-passive-sscn",	 "yes",   ResMgr::BoolValidate,ResMgr::NoClosure},
   {"ftp:home",			 "",	  0,0},
   {"ftp:site-group",		 "",	  0,0},
   {"ftp:lang",			 "",	  0,0},
   {"ftp:list-empty-ok",	 "no",	  0,0},
   {"ftp:list-options",		 "",	  0,0},
   {"ftp:nop-interval",		 "120",   ResMgr::UNumberValidate,0},
   {"ftp:passive-mode",		 "on",    ResMgr::BoolValidate,0},
   {"ftp:port-range",		 "full",  ResMgr::RangeValidate,0},
   {"ftp:port-ipv4",		 "",	  ResMgr::IPv4AddrValidate,0},
   {"ftp:proxy",		 "",	  FtpProxyValidate,0},
   {"ftp:proxy-auth-joined",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:rest-list",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:rest-stor",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:timezone",		 "GMT",   0,0},
   {"ftp:skey-allow",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:skey-force",		 "no",    ResMgr::BoolValidate,0},
#if USE_SSL
   {"ftp:ssl-allow",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ssl-force",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-data",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-fxp",	 "no",    ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-list",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ssl-auth",		 "TLS",   AuthArgValidate,0},
   {"ftp:ssl-allow-anonymous",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-use-ccc",		 "no",	  ResMgr::BoolValidate,0},
   {"ftps:initial-prot",	 "",	  ProtValidate,0},
#endif
   {"ftp:stat-interval",	 "1",	  ResMgr::TimeIntervalValidate,0},
   {"ftp:sync-mode",		 "on",    ResMgr::BoolValidate,0},
   {"ftp:use-abor",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-allo",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-feat",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-fxp",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-hftp",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-mdtm",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-mdtm-overloaded",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-mlsd",		 "no",    ResMgr::BoolValidate,0},
   {"ftp:use-pret",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-site-chmod",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-site-idle",	 "no",    ResMgr::BoolValidate,0},
   {"ftp:use-site-utime",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-size",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-stat",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-telnet-iac",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-quit",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:verify-address",	 "no",    ResMgr::BoolValidate,0},
   {"ftp:verify-port",		 "no",    ResMgr::BoolValidate,0},
   {"ftp:web-mode",		 "off",   ResMgr::BoolValidate,0},
#define RETRY_530 \
   "too many|overloaded|try (again |back )?later|is restricted to|"\
   "maximum number|number of connect|only.*session.*allowed|more connection"
   {"ftp:retry-530",		 RETRY_530,ResMgr::ERegExpValidate,0},
   {"ftp:retry-530-anonymous",	 "Login incorrect",ResMgr::ERegExpValidate,0},
   {"hftp:cache",		 "yes",   ResMgr::BoolValidate,0},
   {"hftp:cache-control",	 "",	  0,0},
   {"hftp:proxy",		 "",	  HttpProxyValidate,0},
   {"hftp:use-authorization",	 "yes",   ResMgr::BoolValidate,0},
   {"hftp:use-head",		 "yes",   ResMgr::BoolValidate,0},
   {"hftp:use-mkcol",		 "no",	  ResMgr::BoolValidate,0},
   {"hftp:use-propfind",	 "no",	  ResMgr::BoolValidate,0},
   {"hftp:use-type",		 "yes",   ResMgr::BoolValidate,0},
   {"http:accept",		 "*/*",   0,0},
   {"http:accept-language",	 "",	  0,0},
   {"http:accept-charset",	 "",	  0,0},
   {"http:authorization",	 "",	  0,0},
   {"http:cache",		 "yes",   ResMgr::BoolValidate,0},
   {"http:cache-control",	 "",	  0,0},
   {"http:proxy",		 "",	  HttpProxyValidate,0},
   {"http:use-mkcol",		 "yes",   ResMgr::BoolValidate,0},
   {"http:use-propfind",	 "no",    ResMgr::BoolValidate,0},
   {"http:user-agent",		 PACKAGE"/"VERSION,0,0},
   {"http:cookie",		 "",	  0,0},
   {"http:set-cookies",		 "no",	  0,0},
   {"http:post-content-type",	 "application/x-www-form-urlencoded",0,0},
   {"http:put-method",		 "PUT",   PutOrPost,0},
   {"http:put-content-type",	 "",	  0,0},
   {"http:referer",		 "",	  0,0},
#if USE_SSL
   {"https:proxy",		 "",	  HttpProxyValidate,0},
#endif
   {"net:idle",			 "3m",	  ResMgr::TimeIntervalValidate,0},
   {"net:limit-max",		 "0",	  ResMgr::UNumberValidate,0},
   {"net:limit-rate",		 "0:0",   ResMgr::UNumberPairValidate,0},
   {"net:limit-total-max",	 "0",	  ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"net:limit-total-rate",	 "0:0",   ResMgr::UNumberPairValidate,ResMgr::NoClosure},
   {"net:max-retries",		 "4096",  ResMgr::UNumberValidate,0},
   {"net:persist-retries",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:no-proxy",		 "",	  0,ResMgr::NoClosure},
   {"net:reconnect-interval-base","30",	  ResMgr::UNumberValidate,0},
   {"net:reconnect-interval-multiplier","1.5",ResMgr::FloatValidate,0},
   {"net:reconnect-interval-max","600",	  ResMgr::UNumberValidate,0},
   {"net:socket-buffer",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:socket-maxseg",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:socket-bind-ipv4",	 "",	  ResMgr::IPv4AddrValidate,0},
#if INET6
   {"net:socket-bind-ipv6",	 "",	  ResMgr::IPv6AddrValidate,0},
#endif
   {"net:timeout",		 "5m",	  ResMgr::TimeIntervalValidate,0},
   {"net:connection-limit",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:connection-takeover",	 "yes",   ResMgr::BoolValidate,0},

   {"mirror:order",		 "*.sfv *.sig *.md5* *.sum * */", 0,ResMgr::NoClosure},
   {"mirror:parallel-directories", "yes", ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:parallel-transfer-count", "1",ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"mirror:exclude-regex",	 "(^|/)(\\.in\\.|\\.nfs)",ResMgr::ERegExpValidate,ResMgr::NoClosure},
   {"mirror:use-pget-n",	 "1",	  ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"mirror:set-permissions",	 "yes",   ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:dereference",	 "no",    ResMgr::BoolValidate,ResMgr::NoClosure},

   {"sftp:max-packets-in-flight","16",	  ResMgr::UNumberValidate,0},
   {"sftp:protocol-version",	 "4",	  ResMgr::UNumberValidate,0},
   {"sftp:size-read",		 "0x8000",ResMgr::UNumberValidate,0},
   {"sftp:size-write",		 "0x8000",ResMgr::UNumberValidate,0},
   {"sftp:connect-program",	 "ssh -a -x",0,0},
   {"sftp:server-program",	 "sftp",  0,0},
   {"sftp:charset",		 "",	  ResMgr::CharsetValidate,0},

   {"file:charset",		 "",	  ResMgr::CharsetValidate,ResMgr::NoClosure},

   {"dns:cache-enable",		 "yes",	  ResMgr::BoolValidate,0},
   {"dns:cache-expire",		 "1h",	  ResMgr::TimeIntervalValidate,0},
   {"dns:cache-size",		 "256",	  ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"dns:fatal-timeout",	 "7d",	  ResMgr::TimeIntervalValidate,0},
   {"dns:max-retries",		 "1000",  ResMgr::UNumberValidate,0},
#if INET6
# define DEFAULT_ORDER "inet6 inet"
#else
# define DEFAULT_ORDER "inet"
#endif
   {"dns:order",		 DEFAULT_ORDER, OrderValidate,0},
   {"dns:SRV-query",		 "no",	  ResMgr::BoolValidate,0},
   {"dns:use-fork",		 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},

   {"fish:shell",		 "/bin/sh",0,0},
   {"fish:connect-program",	 "ssh -a -x",0,0},
   {"fish:charset",		 "",	  ResMgr::CharsetValidate,0},

   {"color:dir-colors",		 "",	  0,ResMgr::NoClosure},

   {"xfer:destination-directory","",	  0,0},
   {"xfer:verify",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:verify-command",	 "",	  ResMgr::FileExecutable,0},

#if USE_SSL
   {"ssl:ca-file",		 "",	  ResMgr::FileReadable,ResMgr::NoClosure},
   {"ssl:crl-file",		 "",	  ResMgr::FileReadable,ResMgr::NoClosure},
   {"ssl:key-file",		 "",	  ResMgr::FileReadable,0},
   {"ssl:cert-file",		 "",	  ResMgr::FileReadable,0},
   {"ssl:verify-certificate",	 "no",	  ResMgr::BoolValidate,0},
# if USE_OPENSSL
   {"ssl:ca-path",		 "",	  ResMgr::DirReadable,ResMgr::NoClosure},
   {"ssl:crl-path",		 "",	  ResMgr::DirReadable,ResMgr::NoClosure},
# endif
#endif
   {0}
};
static ResDecls lftp_vars_register(lftp_vars);

#ifdef HAVE_LANGINFO_H
# include <langinfo.h>
#endif

bool ResMgr::class_inited;
void ResMgr::ClassInit()
{
   if(class_inited)
      return;
   class_inited=true;
   for(ResType *scan=type_chain; scan; scan=scan->next)
   {
      if(scan->defvalue && scan->val_valid)
      {
	 char *dv=xstrdup(scan->defvalue);
	 const char *error=(*scan->val_valid)(&dv);
	 if(error)
	    fprintf(stderr,"Default value for %s is invalid: %s\n",scan->name,error);
	 else if(strcmp(dv,scan->defvalue))
	    fprintf(stderr,"Default value for %s (%s) is not in canonic form: %s\n",scan->name,scan->defvalue,dv);
      }
   }

   // inherit http proxy from environment
   const char *http_proxy=getenv("http_proxy");
   if(http_proxy)
   {
      Set("http:proxy",0,http_proxy);
      Set("hftp:proxy",0,http_proxy);
   }

#if USE_SSL
   const char *https_proxy=getenv("https_proxy");
   if(https_proxy)
      Set("https:proxy",0,https_proxy);
#endif

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

   const char *module_path=getenv("LFTP_MODULE_PATH");
   if(module_path)
      Set("module:path",0,module_path);

   const char *dir_colors=getenv("LS_COLORS");
   if(!dir_colors)
      dir_colors=getenv("ZLS_COLORS"); /* zsh */
   if(dir_colors)
      Set("color:dir-colors",0,dir_colors);

#if defined(HAVE_NL_LANGINFO) && defined(CODESET)
   char *cs=nl_langinfo(CODESET);
   if(cs)
      Set("file:charset",0,cs);
#endif

   const char *time_style=getenv("TIME_STYLE");
   if(time_style && *time_style)
      Set("cmd:time-style",0,time_style);

   Set("xfer:verify-command",0,PKGDATADIR"/verify-file");
}
