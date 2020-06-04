/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2019 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <pwd.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stddef.h>

#include "ResMgr.h"
#include "url.h"
#include "GetPass.h"
#include "ascii_ctype.h"
#include "configmake.h"
#include "misc.h"
#include "localcharset.h"

static const char *FtpProxyValidate(xstring_c *p)
{
   ParsedURL url(*p);
   if(url.host==0)
   {
      p->truncate(0);
      return 0;
   }
   if(url.proto)
   {
      if(strcmp(url.proto,"ftp") && strcmp(url.proto,"http"))
	 return _("Proxy protocol unsupported");
   }
   if(url.user && !url.pass)
   {
      url.pass.set(GetPass(_("ftp:proxy password: ")));
      p->truncate();
      url.CombineTo(*p);
   }
   return 0;
}

static const char *SetValidate(xstring_c& s,const char *const *set,const char *name)
{
   const char *const *scan;
   for(scan=set; *scan; scan++)
      if(s.eq(*scan))
	 return 0;

   xstring &j=xstring::get_tmp();
   if(name)
      j.setf(_("%s must be one of: "),name);
   else
      j.set(_("must be one of: "));
   bool had_empty=false;
   for(scan=set; *scan; scan++) {
      if(!**scan) {
	 had_empty=true;
	 continue;
      }
      if(scan>set)
	 j.append(", ");
      j.append(*scan);
   }
   if(had_empty)
      j.append(_(", or empty"));
   return j;
}

static const char *FtpProxyAuthTypeValidate(xstring_c *s)
{
   static const char *const valid_set[]={
      "user", "joined", "joined-acct", "open", "proxy-user@host", 0
   };
   return SetValidate(*s,valid_set,"ftp:proxy-auth-type");
}

static const char *HttpProxyValidate(xstring_c *p)
{
   ParsedURL url(*p);
   if(url.host==0)
   {
      p->truncate(0);
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

static const char *PutOrPost(xstring_c *s)
{
   if(strcasecmp(*s,"PUT") && strcasecmp(*s,"POST"))
      return _("only PUT and POST values allowed");
   for(char *scan=s->get_non_const(); *scan; scan++)
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
const char *OrderValidate(xstring_c *s)
{
   static xstring error;
   xstring fixed;

   const char * const delim="\t ";
   char *s1=alloca_strdup(*s);
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
	 error.setf(_("unknown address family `%s'"),s1);
	 return error;
      }
      if(fixed)
	 fixed.vappend(" ",s1,NULL);
      else
	 fixed.set(s1);
   }
   s->set(fixed);
   return 0;
}

static const char *SortByValidate(xstring_c *s)
{
   static const char * const valid_set[]={
      "name", "name-desc", "size", "size-desc", "date", "date-desc", 0
   };
   return SetValidate(*s,valid_set,"mirror:order-by");
}

#if USE_SSL
static
const char *AuthArgValidate(xstring_c *s)
{
   for(char *i=s->get_non_const(); *i; i++)
      *i=to_ascii_upper(*i);

   const char *const valid_set[]={
      "SSL", "TLS", "TLS-P", "TLS-C", 0
   };
   return SetValidate(*s,valid_set,"ftp:ssl-auth");
}
static
const char *ProtValidate(xstring_c *s)
{
   for(char *i=s->get_non_const(); *i; i++)
      *i=to_ascii_upper(*i);

   const char *const valid_set[]={
      "C", "S", "E", "P", "", 0
   };
   return SetValidate(*s,valid_set,"ftps:initial-prot");
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
   {"ftp:catch-size",		 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:charset",		 "",	  ResMgr::CharsetValidate,0},
   {"ftp:client",		 PACKAGE "/" VERSION,0,0},
   {"ftp:compressed-re",	 "\\.(bz2|[glrsx7]z|lzma|lz[hox]|[ai]ce|apk|ar[cj]|cab|cfs|dar|[je]ar|lha|isz|pak|rar|sitx?|t(gz|bz2|lz)|tar\\.(gz|xz|bz2|lzma)|war|zipx?|zoo|[arz0][0-9][0-9])$",ResMgr::ERegExpValidate,ResMgr::NoClosure},
   {"ftp:device-prefix",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fix-pasv-address",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ignore-pasv-address",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fxp-force",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:fxp-passive-source",	 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"ftp:fxp-passive-sscn",	 "yes",   ResMgr::BoolValidate,ResMgr::NoClosure},
   {"ftp:home",			 "",	  0,0},
   {"ftp:site"			 "",	  0,0},
   {"ftp:site-group",		 "",	  0,0},
   {"ftp:lang",			 "",	  0,0},
   {"ftp:list-empty-ok",	 "no",	  0,0},
   {"ftp:list-options",		 "",	  0,0},
   {"ftp:mode-z-level",		 "6",	  ResMgr::UNumberValidate,0},
   {"ftp:nop-interval",		 "120",   ResMgr::UNumberValidate,0},
   {"ftp:passive-mode",		 "on",    ResMgr::BoolValidate,0},
   {"ftp:port-range",		 "full",  ResMgr::RangeValidate,0},
   {"ftp:port-ipv4",		 "",	  ResMgr::IPv4AddrValidate,0},
   {"ftp:prefer-epsv",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:proxy",		 "",	  FtpProxyValidate,0},
   {"ftp:proxy-auth-type",	 "user",  FtpProxyAuthTypeValidate,0},
   {"ftp:rest-list",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:rest-stor",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:timezone",		 "GMT",   0,0},
   {"ftp:too-many-re",		 "(Too many|No more) connections",ResMgr::ERegExpValidate,0},
   {"ftp:skey-allow",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:skey-force",		 "no",    ResMgr::BoolValidate,0},
   {"ftp:netkey-allow",		 "yes",   ResMgr::BoolValidate,0},
#if USE_SSL
   {"ftp:ssl-allow",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ssl-force",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-data",	 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-fxp",	 "yes",    ResMgr::BoolValidate,0},
   {"ftp:ssl-protect-list",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:ssl-auth",		 "TLS",   AuthArgValidate,0},
   {"ftp:ssl-allow-anonymous",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-use-ccc",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-shutdown-timeout",	 "5",	  ResMgr::TimeIntervalValidate,0},
   {"ftp:ssl-data-use-keys",	 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:ssl-copy-sid",		 "yes",	  ResMgr::BoolValidate,0},
   {"ftps:initial-prot",	 "",	  ProtValidate,0},
#endif
   {"ftp:stat-interval",	 "1",	  ResMgr::TimeIntervalValidate,0},
   {"ftp:strict-multiline",	 "off",	  ResMgr::BoolValidate,0},
   {"ftp:sync-mode",		 "on",    ResMgr::BoolValidate,0},
   {"ftp:trust-feat",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-abor",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-allo",		 "no",    ResMgr::BoolValidate,0},
   {"ftp:use-feat",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-fxp",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-hftp",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-mdtm",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-mdtm-overloaded",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-mlsd",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-mode-z",		 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:use-pret",		 "auto",  ResMgr::TriBoolValidate,0},
   {"ftp:use-site-chmod",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-site-idle",	 "no",    ResMgr::BoolValidate,0},
   {"ftp:use-site-utime",	 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:use-site-utime2",	 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:use-size",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-stat",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-stat-for-list",	 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-telnet-iac",	 "yes",   ResMgr::BoolValidate,0},
   {"ftp:use-tvfs",		 "auto",  ResMgr::TriBoolValidate,0},
   {"ftp:use-ip-tos",		 "no",	  ResMgr::BoolValidate,0},
   {"ftp:use-utf8",		 "yes",	  ResMgr::BoolValidate,0},
   {"ftp:use-quit",		 "yes",   ResMgr::BoolValidate,0},
   {"ftp:verify-address",	 "no",    ResMgr::BoolValidate,0},
   {"ftp:verify-port",		 "no",    ResMgr::BoolValidate,0},
   {"ftp:web-mode",		 "off",   ResMgr::BoolValidate,0},
   {"ftp:waiting-150-timeout",	 "5",	  ResMgr::TimeIntervalValidate,0},
#define RETRY_530 \
   "too many|overloaded|try (again |back )?later|is restricted to|"\
   "maximum number|number of connect|only.*session.*allowed|more connection|already connected|simultaneous login"
   {"ftp:retry-530",		 RETRY_530,ResMgr::ERegExpValidate,0},
   {"ftp:retry-530-anonymous",	 "Login incorrect",ResMgr::ERegExpValidate,0},
   {"hftp:cache",		 "yes",   ResMgr::BoolValidate,0},
   {"hftp:cache-control",	 "",	  0,0},
   {"hftp:decode",		 "no",	  ResMgr::BoolValidate,0},
   {"hftp:proxy",		 "",	  HttpProxyValidate,0},
   {"hftp:use-authorization",	 "yes",   ResMgr::BoolValidate,0},
   {"hftp:use-head",		 "yes",   ResMgr::BoolValidate,0},
   {"hftp:use-mkcol",		 "no",	  ResMgr::BoolValidate,0},
   {"hftp:use-propfind",	 "no",	  ResMgr::BoolValidate,0},
   {"hftp:use-range",		 "yes",   ResMgr::BoolValidate,0},
   {"hftp:use-allprop",		 "no",	  ResMgr::BoolValidate,0},
   {"hftp:use-type",		 "yes",   ResMgr::BoolValidate,0},
   {"http:accept",		 "*/*",   0,0},
   {"http:accept-language",	 "",	  0,0},
   {"http:accept-charset",	 "",	  0,0},
   {"http:accept-encoding",	 "",	  0,0},
   {"http:authorization",	 "",	  0,0},
   {"http:cache",		 "yes",   ResMgr::BoolValidate,0},
   {"http:cache-control",	 "",	  0,0},
   {"http:decode",		 "yes",	  ResMgr::BoolValidate,0},
   {"http:proxy",		 "",	  HttpProxyValidate,0},
   {"http:use-mkcol",		 "yes",   ResMgr::BoolValidate,0},
   {"http:use-propfind",	 "no",    ResMgr::BoolValidate,0},
   {"http:use-range",		 "yes",   ResMgr::BoolValidate,0},
   {"http:use-allprop",		 "no",	  ResMgr::BoolValidate,0},
   {"http:user-agent",		 PACKAGE "/" VERSION,0,0},
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
   {"net:limit-total-max",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:limit-total-rate",	 "0:0",   ResMgr::UNumberPairValidate,0},
   {"net:max-retries",		 "1000",  ResMgr::UNumberValidate,0},
   {"net:persist-retries",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:no-proxy",		 "",	  0,ResMgr::NoClosure},
   {"net:reconnect-interval-base","15",	  ResMgr::UNumberValidate,0},
   {"net:reconnect-interval-multiplier","1.5",ResMgr::FloatValidate,0},
   {"net:reconnect-interval-max","300",	  ResMgr::UNumberValidate,0},
   {"net:socket-buffer",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:socket-maxseg",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:socket-bind-ipv4",	 "",	  ResMgr::IPv4AddrValidate,0},
#if INET6
   {"net:socket-bind-ipv6",	 "",	  ResMgr::IPv6AddrValidate,0},
#endif
   {"net:timeout",		 "5m",	  ResMgr::TimeIntervalValidate,0},
   {"net:connection-limit",	 "0",	  ResMgr::UNumberValidate,0},
   {"net:connection-limit-timer","5m",	  ResMgr::TimeIntervalValidate,0},
   {"net:connection-takeover",	 "yes",   ResMgr::BoolValidate,0},

   {"mirror:sort-by",		 "name",  SortByValidate,ResMgr::NoClosure},
   {"mirror:order",		 "*.sfv *.sig *.md5* *.sum * */", 0,ResMgr::NoClosure},
   {"mirror:parallel-directories", "yes", ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:parallel-transfer-count", "0",ResMgr::UNumberValidate,0},
   {"mirror:exclude-regex",	 "(^|/)(\\.in\\.|\\.nfs)",ResMgr::ERegExpValidate,ResMgr::NoClosure},
   {"mirror:include-regex",	 "",	  ResMgr::ERegExpValidate,ResMgr::NoClosure},
   {"mirror:use-pget-n",	 "0",	  ResMgr::UNumberValidate,0},
   {"mirror:set-permissions",	 "yes",   ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:dereference",	 "no",    ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:skip-noaccess",	 "no",    ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:no-empty-dirs",	 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:require-source",	 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"mirror:overwrite",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},

   {"sftp:auto-confirm",	 "no",	  ResMgr::BoolValidate,0},
   {"sftp:max-packets-in-flight","16",	  ResMgr::UNumberValidate,0},
   {"sftp:protocol-version",	 "6",	  ResMgr::UNumberValidate,0},
   {"sftp:size-read",		 "32k",	  ResMgr::UNumberValidate,0},
   {"sftp:size-write",		 "32k",	  ResMgr::UNumberValidate,0},
   {"sftp:connect-program",	 "ssh -a -x",0,0},
   {"sftp:server-program",	 "sftp",  0,0},
   {"sftp:charset",		 "",	  ResMgr::CharsetValidate,0},
   {"sftp:use-full-path",	 "yes",	  ResMgr::BoolValidate,0},

   {"file:charset",		 "",	  ResMgr::CharsetValidate,ResMgr::NoClosure},
   {"file:use-lock",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"file:use-fallocate",	 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},

   {"dns:cache-enable",		 "yes",	  ResMgr::BoolValidate,0},
   {"dns:cache-expire",		 "1h",	  ResMgr::TimeIntervalValidate,0},
   {"dns:cache-size",		 "256",	  ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"dns:fatal-timeout",	 "7d",	  ResMgr::TimeIntervalValidate,0},
   {"dns:max-retries",		 "1000",  ResMgr::UNumberValidate,0},
   {"dns:name",			 "",	  0,ResMgr::HasClosure},
#if INET6
# define DEFAULT_ORDER "inet6 inet"
#else
# define DEFAULT_ORDER "inet"
#endif
   {"dns:order",		 DEFAULT_ORDER, OrderValidate,0},
   {"dns:SRV-query",		 "no",	  ResMgr::BoolValidate,0},
   {"dns:use-fork",		 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
#ifdef DNSSEC_LOCAL_VALIDATION
   {"dns:strict-dnssec",	 "no",	  ResMgr::BoolValidate,0},
#endif

   {"fish:auto-confirm",	 "no",	  ResMgr::BoolValidate,0},
   {"fish:shell",		 "/bin/sh",0,0},
   {"fish:connect-program",	 "ssh -a -x",0,0},
   {"fish:charset",		 "",	  ResMgr::CharsetValidate,0},

   {"color:dir-colors",		 "",	  0,ResMgr::NoClosure},

   {"xfer:destination-directory","",	  0,0},
   {"xfer:verify",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:verify-command",	 "",	  ResMgr::FileExecutable,0},
   {"xfer:auto-rename",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:max-log-size",	 "1M",	  ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"xfer:use-temp-file",	 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:temp-file-name",	 ".in.*", 0,ResMgr::NoClosure},
   {"xfer:timeout",		 "1d",	  ResMgr::TimeIntervalValidate,ResMgr::NoClosure},
   {"xfer:clobber",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:make-backup",		 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:keep-backup",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"xfer:backup-suffix",	 "~%Y%m%d%H%M%S~",0,ResMgr::NoClosure},
   {"xfer:parallel",		 "1",	  ResMgr::UNumberValidate,ResMgr::NoClosure},

   // deprecated settings
   {"xfer:log",		   "log:enabled/xfer",   0,ResMgr::AliasValidate},
   {"xfer:log-file",	   "log:file/xfer",	0,ResMgr::AliasValidate},
   {"xfer:max-log-size",   "log:max-size/xfer",	0,ResMgr::AliasValidate},

#if USE_SSL
   {"ssl:ca-file",		 "",	  ResMgr::FileReadable,ResMgr::NoClosure},
   {"ssl:crl-file",		 "",	  ResMgr::FileReadable,ResMgr::NoClosure},
   {"ssl:key-file",		 "",	  ResMgr::FileReadable,0},
   {"ssl:cert-file",		 "",	  ResMgr::FileReadable,0},
   {"ssl:check-hostname",	 "yes",	  ResMgr::BoolValidate,0},
   {"ssl:verify-certificate",	 "yes",	  ResMgr::BoolValidate,0},
   {"ssl:use-sni",		 "yes",	  ResMgr::BoolValidate,0},
   {"ssl:priority",		 "",	  0,0},
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

bool ResType::class_inited;
void ResType::ClassInit()
{
   if(class_inited)
      return;
   class_inited=true;
   for(ResType *scan=types_by_name->each_begin(); scan; scan=types_by_name->each_next())
   {
      if(scan->defvalue && scan->val_valid)
      {
	 xstring_c dv(scan->defvalue);
	 const char *error=(*scan->val_valid)(&dv);
	 if(error)
	    fprintf(stderr,"Default value for %s is invalid: %s\n",scan->name,error);
	 else if(strcmp(dv,scan->defvalue))
	    fprintf(stderr,"Default value for %s (%s) is not in canonic form: %s\n",scan->name,scan->defvalue,dv.get());
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

   const char *module_path=getenv("LFTP_MODULE_PATH");
   if(module_path)
      Set("module:path",0,module_path);

   const char *dir_colors=getenv("LS_COLORS");
   if(!dir_colors)
      dir_colors=getenv("ZLS_COLORS"); /* zsh */
   if(dir_colors)
      Set("color:dir-colors",0,dir_colors);

   const char *cs=locale_charset();
   if(cs && cs[0])
      Set("file:charset",0,cs);

   const char *time_style=getenv("TIME_STYLE");
   if(time_style && *time_style)
      Set("cmd:time-style",0,time_style);

   SetDefault("xfer:verify-command",0,PKGDATADIR"/verify-file");

   const char *ctx="xfer";
   SetDefault("log:enabled",ctx,"yes");
   SetDefault("log:show-time",ctx,"yes");
   SetDefault("log:file",ctx,dir_file(get_lftp_data_dir(),"transfer_log"));
}
