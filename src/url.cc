/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "xstring.h"
#include <ctype.h>
#include <assert.h>
#include "url.h"
#include "ascii_ctype.h"
#include "ConnectionSlot.h"
#include "bookmark.h"
#include "misc.h"
#include "log.h"
#include "network.h"

/*
   URL -> [PROTO://]CONNECT[[:]/PATH]
   CONNECT -> [USER[:PASS]@]HOST[:PORT]

   exceptions:
      file:/PATH
      bm:BOOKMARK[/PATH]
      slot:SLOT[/PATH]
*/

static bool valid_slot(const char *s);
static bool valid_bm(const char *s);

ParsedURL::ParsedURL(const char *url,bool proto_required,bool use_rfc1738)
{
   parse(url,proto_required,use_rfc1738);
}
ParsedURL::~ParsedURL()
{
}

void ParsedURL::parse(const char *url,bool proto_required,bool use_rfc1738)
{
   orig_url.set(url);
   xstring_c connect;
   const char *base=url;
   const char *scan=base;
   while(is_ascii_alpha(*scan))
      scan++;
   if(scan[0]==':' && scan[1]=='/' && scan[2]=='/')
   {
      // found protocol
      proto.nset(base,scan-base);
      base=scan+=3;
      if(!strcmp(proto,"file") && scan[0]=='/')
	 goto file_with_no_host;
   }
   else if(scan[0]==':' && !strncmp(base,"file:",5))
   {
      // special form for file protocol
      proto.nset(base,scan-base);
      scan++;
   file_with_no_host:
      path.set(scan);
      host.set("localhost");
      goto decode;
   }
   else if(scan[0]==':'
   && ((!strncmp(base,"slot:",5) && valid_slot(scan+1))
       || (!strncmp(base,"bm:",3) && valid_bm(scan+1))))
   {
      // special form for selecting a connection slot or a bookmark
      proto.nset(base,scan-base);
      scan++;
      base=scan;
      scan=strchr(scan,'/');
      if(scan)
      {
	 host.nset(base,scan-base);
	 path.set(scan);
      }
      else
	 host.set(base);
      goto decode;
   }
   else if(proto_required)
   {
      // all the rest is path, if protocol is required.
      path.set(base);
      goto decode;
   }

   scan=base;
   while(*scan && *scan!='/')
      scan++; // skip host name, port and user:pass

   connect.nset(base,scan-base-(scan>base && scan[-1]==':'));

   if(*scan=='/') // directory
   {
      if(scan[1]=='~')
	 path.set(scan+1);
      else
      {
	 if((!xstrcmp(proto,"ftp") || !xstrcmp(proto,"ftps") || !xstrcmp(proto,"hftp"))
	 && use_rfc1738)
	 {
	    // special handling for ftp protocol.
	    if(!strncasecmp(scan+1,"%2F",3))
	       path.set(scan+1);
	    else if(!(is_ascii_alpha(scan[1]) && scan[2]==':' && scan[3]=='/'))
	       path.vset("~",scan,NULL);
	 }
	 else
	    path.set(scan);
      }
   }
   else if(proto)
   {
      if(!strcmp(proto,"http") || !strcmp(proto,"https"))
	 path.set("/");
   }

   // try to extract user name/password
   base=connect;
   scan=strrchr(base,'@');
   if(scan)
   {
      user.nset(base,scan-base);
      base=scan+1;
      scan=user;
      while(*scan && *scan!=':')
	 scan++;
      if(*scan==':')
      {
	 pass.set(scan+1);
	 user.truncate(scan-user);
      }
   }

   // extract host name
   scan=base;
   if(*scan=='[') // RFC2732 [ipv6]
   {
      while(*scan && *scan!=']')
	 scan++;
      if(*scan==']')
      {
	 scan++;
	 host.nset(base+1,scan-base-2);
      }
      else
	 scan=base;
   }

   if(scan==base)
   {
      while(*scan && *scan!=':')
	 scan++;
      host.nset(base,scan-base);
   }

   if(*scan==':') // port found
   {
      if(strchr(scan+1,':')==0)
      {
	 port.set(scan+1);
      }
      else
      {
	 /* more than one colon - maybe it is ipv6 digital address */
	 host.set(base);
      }
   }

decode:
   user.url_decode();
   pass.url_decode();
   host.url_decode();
   path.url_decode();

   if(!xstrcmp(proto,"slot"))
   {
      const FileAccess *fa=ConnectionSlot::FindSession(host);
      if(!fa)
	 return;
      orig_url.set(0);

      proto.set(fa->GetProto());
      user.set(fa->GetUser());
      pass.set(fa->GetPassword());
      host.set(fa->GetHostName());
      port.set(fa->GetPort());

      FA::Path cwd(fa->GetCwd());
      if(path)
	 cwd.Change(path+(path[0]=='/'));
      path.set(cwd);
   }
   else if(!xstrcmp(proto,"bm"))
   {
      const char *bm=lftp_bookmarks.Lookup(host);
      if(!bm || !bm[0])
	 return;
      const char *new_url=0;
      xstring u(bm);
      if(orig_url)
      {
	 const char *new_path=orig_url+url::path_index(orig_url);
	 if(new_path[0]=='/')
	    new_path++;
	 if(new_path[0]=='/' || new_path[0]=='~')
	    u.truncate(url::path_index(u));
	 assert(u[0]);
	 if(u.last_char()!='/' && new_path[0]!='/')
	    u.append('/');
	 else if(u.last_char()=='/' && new_path[0]=='/')
	    new_path++;
	 u.append(new_path);
	 new_url=u;
      }
      else
	 new_url=url_file(bm,path+(path && path[0]=='/'));
      parse(new_url,proto_required,use_rfc1738);
   }
}

static bool valid_slot(const char *cs)
{
   xstring& s=xstring::get_tmp(cs);
   s.truncate_at('/');
   s.url_decode();
   return 0!=ConnectionSlot::Find(s);
}
static bool valid_bm(const char *bm)
{
   xstring& s=xstring::get_tmp(bm);
   s.truncate_at('/');
   s.url_decode();
   const char *url=lftp_bookmarks.Lookup(s);
   return(url && !strchr(url,' ') && !strchr(url,'\t'));
}

int url::path_index(const char *base)
{
   const char *scan=base;
   while(is_ascii_alpha(*scan))
      scan++;
   if(scan[0]!=':')
      return 0;
   if(scan[1]=='/' && scan[2]=='/')
   {
      // found protocol
      const char *slash=strchr(scan+3,'/');
      if(slash)
	 return slash-base;
      return strlen(base);
   }
   else if(!strncmp(base,"file:",5))
   {
      // special form for file protocol
      return scan+1-base;
   }
   else if((!strncmp(base,"slot:",5) && valid_slot(base+5))
	|| (!strncmp(base,"bm:",3) && valid_bm(base+3)))
   {
      const char *slash=strchr(scan+1,'/');
      if(slash)
	 return slash-base;
      return strlen(base);
   }
   return 0;
}

const char *url::path_ptr(const char *base)
{
   if(!base)
      return 0;
   return base+path_index(base);
}

char *ParsedURL::Combine(const char *home,bool use_rfc1738)
{
   xstring buf("");
   return CombineTo(buf,home,use_rfc1738).borrow();
}
xstring& ParsedURL::CombineTo(xstring& u,const char *home,bool use_rfc1738) const
{
   bool is_file=!xstrcmp(proto,"file");
   bool is_ftp=(!xstrcmp(proto,"ftp") || !xstrcmp(proto,"hftp"));

   if(proto)
   {
      u.append(proto);
      u.append(is_file?":":"://");
   }
   if(user && !is_file)
   {
      u.append(url::encode(user,URL_USER_UNSAFE));
      if(pass)
      {
	 u.append(':');
	 u.append(url::encode(pass,URL_PASS_UNSAFE));
      }
      u.append('@');
   }
   if(host && !is_file) {
      unsigned encode_flags=0;
      if(xtld_name_ok(host))
	 encode_flags|=URL_ALLOW_8BIT;
      if(is_ipv6_address(host))
	 u.append('[').append(host).append(']');
      else
	 u.append_url_encoded(host,URL_HOST_UNSAFE,encode_flags);
   }
   if(port && !is_file)
   {
      u.append(':');
      u.append(url::encode(port,URL_PORT_UNSAFE));
   }
   if(path && strcmp(path,"~"))
   {
      if(path[0]!='/' && !is_file) // e.g. ~/path
	 u.append('/');
      int p_offset=0;
      if(is_ftp && use_rfc1738)
      {
	 // some cruft for ftp urls...
	 if(path[0]=='/' && xstrcmp(home,"/"))
	 {
	    u.append("/%2F");
	    p_offset=1;
	 }
	 else if(path[0]=='~' && path[1]=='/')
	    p_offset=2;
      }
      u.append(url::encode(path+p_offset,URL_PATH_UNSAFE));
   }
   return u;
}

const xstring& url::decode(const char *p)
{
   if(!p)
      return xstring::null;
   return xstring::get_tmp(p).url_decode();
}
const xstring& url::encode(const char *s,int len,const char *unsafe,unsigned flags)
{
   if(!s)
      return xstring::null;
   return xstring::get_tmp("").append_url_encoded(s,len,unsafe,flags);
}

bool url::dir_needs_trailing_slash(const char *proto_c)
{
   if(!proto_c)
      return false;
   char *proto=alloca_strdup(proto_c);
   char *colon=strchr(proto,':');
   if(colon)
      *colon=0;
   return !strcasecmp(proto,"http")
       || !strcasecmp(proto,"https");
}

bool url::find_password_pos(const char *url,int *start,int *len)
{
   *start=*len=0;

   const char *scan=strstr(url,"://");
   if(!scan)
      return false;

   scan+=3;

   const char *at=strchr(scan,'@');
   if(!at)
      return false;

   const char *colon=strchr(scan,':');
   if(!colon || colon>at)
      return false;

   const char *slash=strchr(scan,'/');
   if(slash && slash<at)
      return false;

   *start=colon+1-url;
   *len=at-colon-1;
   return true;
}

const char *url::hide_password(const char *url)
{
   int start,len;
   if(!find_password_pos(url,&start,&len))
      return url;
   return xstring::format("%.*sXXXX%s",start,url,url+start+len);
}
const char *url::remove_password(const char *url)
{
   int start,len;
   if(!find_password_pos(url,&start,&len))
      return url;
   return xstring::format("%.*s%s",start-1,url,url+start+len);
}

bool url::is_url(const char *p)
{
   ParsedURL url(p,true);
   return url.proto!=0;
}
