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

// Static array of objects is wrongly initialized by IRIX CC and Unixware c++.
// So here goes list of arbitrarily named objects, they are never referred
// by name.
static ResDecl
   res_ftp_abor_max_wait   ("ftp:abor-max-wait",   "15s",   ResMgr::TimeIntervalValidate,0),
   ResDecl00a("ftp:acct",		  "",	   0,0),
   ResDecl00 ("ftp:anon-pass",		  "lftp@", 0,0),
   ResDecl01 ("ftp:anon-user",		  "anonymous",0,0),
   ResDecl01a("ftp:auto-sync-mode",	  "",	   ResMgr::ERegExpValidate,0),
   ResDecl01i("ftp:auto-passive-mode",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl01b("ftp:bind-data-socket",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl01f("ftp:charset",		  "",	   ResMgr::CharsetValidate,0),
   ResDecl01g("ftp:client",		  PACKAGE"/"VERSION,0,0),
   ResDecl01e("ftp:device-prefix",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl01c("ftp:fix-pasv-address",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl01h("ftp:ignore-pasv-address",  "no",	   ResMgr::BoolValidate,0),
   ResDecl01d("ftp:fxp-force",		  "no",	   ResMgr::BoolValidate,0),
   ResDecl02 ("ftp:fxp-passive-source",	  "no",	   ResMgr::BoolValidate,ResMgr::NoClosure),
   ResDecl02d("ftp:fxp-passive-sscn",	  "yes",   ResMgr::BoolValidate,ResMgr::NoClosure),
   ResDecl02b("ftp:home",		  "",	   0,0),
   ResDecl02a("ftp:site-group",		  "",	   0,0),
   ResDecl02c("ftp:lang",		  "",	   0,0),
   ResDecl03a("ftp:list-empty-ok",	  "no",	   0,0),
   ResDecl03 ("ftp:list-options",	  "",	   0,0),
   ResDecl04 ("ftp:nop-interval",	  "120",   ResMgr::UNumberValidate,0),
   ResDecl05 ("ftp:passive-mode",	  "on",    ResMgr::BoolValidate,0),
   ResDecl06 ("ftp:port-range",		  "full",  ResMgr::RangeValidate,0),
   ResDecl06a("ftp:port-ipv4",		  "",	   ResMgr::IPv4AddrValidate,0),
   ResDecl07 ("ftp:proxy",		  "",	   FtpProxyValidate,0),
   ResDecl07a("ftp:proxy-auth-joined",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl08 ("ftp:rest-list",		  "no",	   ResMgr::BoolValidate,0),
   ResDecl09 ("ftp:rest-stor",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl09a("ftp:timezone",		  "GMT",   0,0),
   ResDecl10 ("ftp:skey-allow",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl11 ("ftp:skey-force",		  "no",    ResMgr::BoolValidate,0),
#if USE_SSL
   ResDecl11a("ftp:ssl-allow",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl11b("ftp:ssl-force",		  "no",	   ResMgr::BoolValidate,0),
   ResDecl11c("ftp:ssl-protect-data",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl11g("ftp:ssl-protect-fxp",	  "no",    ResMgr::BoolValidate,0),
   ResDecl11f("ftp:ssl-protect-list",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl11d("ftp:ssl-auth",		  "TLS",   AuthArgValidate,0),
   ResDecl11e("ftp:ssl-allow-anonymous",  "no",	   ResMgr::BoolValidate,0),
   ResDecl11i("ftp:ssl-use-ccc",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl11h("ftps:initial-prot",	  "",	   ProtValidate,0),
#endif
   ResDecl12 ("ftp:stat-interval",	  "1",	   ResMgr::TimeIntervalValidate,0),
   ResDecl13 ("ftp:sync-mode",		  "on",    ResMgr::BoolValidate,0),
   ResDecl14 ("ftp:use-abor",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl14a("ftp:use-feat",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15 ("ftp:use-fxp",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15e("ftp:use-hftp",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15b("ftp:use-mdtm",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15d("ftp:use-mdtm-overloaded",  "no",	   ResMgr::BoolValidate,0),
   ResDecl15f("ftp:use-mlsd",		  "no",    ResMgr::BoolValidate,0),
   ResDecl15c("ftp:use-pret",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl15a("ftp:use-site-chmod",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl16a("ftp:use-site-idle",	  "no",    ResMgr::BoolValidate,0),
   ResDecl16d("ftp:use-site-utime",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl16b("ftp:use-size",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl16 ("ftp:use-stat",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl16c("ftp:use-telnet-iac",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl17 ("ftp:use-quit",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl18 ("ftp:verify-address",	  "no",    ResMgr::BoolValidate,0),
   ResDecl19 ("ftp:verify-port",	  "no",    ResMgr::BoolValidate,0),
   ResDecl20 ("ftp:web-mode",		  "off",   ResMgr::BoolValidate,0),
#define RETRY_530 \
   "too many|overloaded|try (again |back )?later|is restricted to|"\
   "maximum number|number of connect|only.*session.*allowed|more connection"
   ResDecl20a("ftp:retry-530",		  RETRY_530,ResMgr::ERegExpValidate,0),
   ResDecl20b("ftp:retry-530-anonymous",  "Login incorrect",ResMgr::ERegExpValidate,0),
   ResDecl21 ("hftp:cache",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl21a("hftp:cache-control",	  "",	   0,0),
   ResDecl22 ("hftp:proxy",		  "",	   HttpProxyValidate,0),
   ResDecl22a("hftp:use-authorization",	  "yes",   ResMgr::BoolValidate,0),
   ResDecl23 ("hftp:use-head",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl23c("hftp:use-mkcol",		  "no",	   ResMgr::BoolValidate,0),
   ResDecl23b("hftp:use-propfind",	  "no",	   ResMgr::BoolValidate,0),
   ResDecl23a("hftp:use-type",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl24a("http:accept",		  "*/*",   0,0),
   ResDecl24b("http:accept-language",	  "",	   0,0),
   ResDecl24c("http:accept-charset",	  "",	   0,0),
   ResDecl24e("http:authorization",	  "",	   0,0),
   ResDecl24 ("http:cache",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl24d("http:cache-control",	  "",	   0,0),
   ResDecl25 ("http:proxy",		  "",	   HttpProxyValidate,0),
   ResDecl25a("http:use-mkcol",		  "yes",   ResMgr::BoolValidate,0),
   ResDecl25b("http:use-propfind",	  "no",    ResMgr::BoolValidate,0),
   ResDecl26 ("http:user-agent",	  PACKAGE"/"VERSION,0,0),
   ResDecl27 ("http:cookie",		  "",	   0,0),
   ResDecl28 ("http:set-cookies",	  "no",	   0,0),
   ResDecl29a("http:post-content-type",   "application/x-www-form-urlencoded",0,0),
   ResDecl29 ("http:put-method",	  "PUT",   PutOrPost,0),
   ResDecl30 ("http:put-content-type",	  "",	   0,0),
   ResDecl30b("http:referer",		  "",	   0,0),
#if USE_SSL
   ResDecl30a("https:proxy",		  "",	   HttpProxyValidate,0),
#endif
   ResDecl31 ("net:idle",		  "3m",	   ResMgr::TimeIntervalValidate,0),
   ResDecl32 ("net:limit-max",		  "0",	   ResMgr::UNumberValidate,0),
   ResDecl33 ("net:limit-rate",		  "0:0",   ResMgr::UNumberPairValidate,0),
   ResDecl34 ("net:limit-total-max",	  "0",	   ResMgr::UNumberValidate,ResMgr::NoClosure),
   ResDecl35 ("net:limit-total-rate",	  "0:0",   ResMgr::UNumberPairValidate,ResMgr::NoClosure),
   ResDecl36 ("net:max-retries",	  "4096",  ResMgr::UNumberValidate,0),
   ResDecl36a("net:persist-retries",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl37 ("net:no-proxy",		  "",	   0,ResMgr::NoClosure),
   ResDecl38 ("net:reconnect-interval-base","30",  ResMgr::UNumberValidate,0),
   ResDecl39 ("net:reconnect-interval-multiplier","1.5",ResMgr::FloatValidate,0),
   ResDecl40 ("net:reconnect-interval-max","600",  ResMgr::UNumberValidate,0),
   ResDecl41 ("net:socket-buffer",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl42 ("net:socket-maxseg",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl42a("net:socket-bind-ipv4",	  "",	   ResMgr::IPv4AddrValidate,0),
#if INET6
   ResDecl42b("net:socket-bind-ipv6",	  "",	   ResMgr::IPv6AddrValidate,0),
#endif
   ResDecl43 ("net:timeout",		  "5m",   ResMgr::TimeIntervalValidate,0),
   ResDecl44 ("net:connection-limit",	  "0",	   ResMgr::UNumberValidate,0),
   ResDecl45 ("net:connection-takeover",  "yes",   ResMgr::BoolValidate,0),
   ResDecl46 ("mirror:order",		  "*.sfv *.sig *.md5* *.sum * */", 0,ResMgr::NoClosure),
   ResDecl47 ("mirror:parallel-directories", "yes", ResMgr::BoolValidate,ResMgr::NoClosure),
   ResDecl48 ("mirror:parallel-transfer-count", "1", ResMgr::UNumberValidate,ResMgr::NoClosure),
   ResDecl49 ("mirror:exclude-regex",	  "(^|/)(\\.in\\.|\\.nfs)",ResMgr::ERegExpValidate,ResMgr::NoClosure),
   ResDecl50 ("mirror:use-pget-n",	  "1",	   ResMgr::UNumberValidate,ResMgr::NoClosure),
   ResDecl51 ("mirror:set-permissions",	  "yes",   ResMgr::BoolValidate,ResMgr::NoClosure),
   ResDecl52 ("mirror:dereference",	  "no",    ResMgr::BoolValidate,ResMgr::NoClosure);

static ResDecl
   res_sftp_packets  ("sftp:max-packets-in-flight","16",ResMgr::UNumberValidate,0),
   res_sftp_proto_ver("sftp:protocol-version","4",ResMgr::UNumberValidate,0),
   res_sftp_rsize    ("sftp:size-read", "0x8000",ResMgr::UNumberValidate,0),
   res_sftp_wsize    ("sftp:size-write","0x8000",ResMgr::UNumberValidate,0),
   res_sftp_conn_prog("sftp:connect-program","ssh -a -x",0,0),
   res_sftp_srv_prog ("sftp:server-program","sftp",0,0),
   res_sftp_charset  ("sftp:charset","",ResMgr::CharsetValidate,0);

static ResDecl
   res_file_charset  ("file:charset",  "",   ResMgr::CharsetValidate,ResMgr::NoClosure);

#if USE_SSL
static ResDecl
   res_ssl_ca_file    ("ssl:ca-file",  "", ResMgr::FileReadable,ResMgr::NoClosure),
   res_ssl_crl_file   ("ssl:crl-file", "", ResMgr::FileReadable,ResMgr::NoClosure),
   res_ssl_key_file   ("ssl:key-file", "", ResMgr::FileReadable,0),
   res_ssl_cert_file  ("ssl:cert-file","", ResMgr::FileReadable,0),
   res_ssl_verify_cert("ssl:verify-certificate","no",ResMgr::BoolValidate,0);
# if USE_OPENSSL
static ResDecl
   res_ssl_ca_path    ("ssl:ca-path",  "", ResMgr::DirReadable, ResMgr::NoClosure),
   res_ssl_crl_path   ("ssl:crl-path", "", ResMgr::DirReadable, ResMgr::NoClosure);
# endif
#endif

#if INET6
# define DEFAULT_ORDER "inet6 inet"
#else
# define DEFAULT_ORDER "inet"
#endif

static ResDecl
   res_cache_enable("dns:cache-enable", "yes", ResMgr::BoolValidate,0),
   res_cache_expire("dns:cache-expire", "1h",  ResMgr::TimeIntervalValidate,0),
   res_cache_size  ("dns:cache-size",   "256", ResMgr::UNumberValidate,ResMgr::NoClosure),
   res_timeout	   ("dns:fatal-timeout","7d",  ResMgr::TimeIntervalValidate,0),
   res_max_retries ("dns:max-retries",  "1000",ResMgr::UNumberValidate,0),
   res_order	   ("dns:order",	DEFAULT_ORDER, OrderValidate,0),
   res_query_srv   ("dns:SRV-query",    "no",  ResMgr::BoolValidate,0),
   res_use_fork	   ("dns:use-fork",     "yes", ResMgr::BoolValidate,ResMgr::NoClosure);

static ResDecl
   res_fish_shell    ("fish:shell", "/bin/sh",  0,0),
   res_fish_connect  ("fish:connect-program", "ssh -a -x", 0,0),
   res_fish_charset  ("fish:charset","",ResMgr::CharsetValidate,0);

static ResDecl
   res_dir_colors ("color:dir-colors",   "",   0,ResMgr::NoClosure);

static ResDecl
   res_dest_dir	  ("xfer:destination-directory","",0,0),
   res_verify	  ("xfer:verify", "no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_verify_cmd ("xfer:verify-command","",ResMgr::FileExecutable,0);

#ifdef HAVE_LANGINFO_H
# include <langinfo.h>
#endif

bool ResMgr::class_inited;
void ResMgr::ClassInit()
{
   if(class_inited)
      return;
   class_inited=true;
   for(ResDecl *scan=type_chain; scan; scan=scan->next)
   {
      if(scan->defvalue && scan->val_valid)
      {
	 const char *error=(*scan->val_valid)(&scan->defvalue);
	 if(error)
	    fprintf(stderr,"Default value for %s is invalid: %s\n",scan->name,error);
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
