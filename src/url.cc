/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
	 if((!xstrcmp(proto,"ftp") || !xstrcmp(proto,"hftp"))
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
   url::decode_string(user.get_non_const());
   url::decode_string(pass.get_non_const());
   url::decode_string(host.get_non_const());
   path.set_length(url::decode_string(path.get_non_const()));

   FileAccess *fa=0;
   if(!xstrcmp(proto,"slot"))
   {
      fa=ConnectionSlot::FindSession(host);
      if(!fa)
	 return;
      orig_url.set(0);
      char *orig_path=alloca_strdup(path);
      proto.set(fa->GetProto());
      user.set(fa->GetUser());
      pass.set(fa->GetPassword());
      host.set(fa->GetHostName());
      port.set(fa->GetPort());
      path.set(fa->GetCwd());
      orig_path+=(orig_path!=0 && orig_path[0]=='/');
      path.set(dir_file(fa->GetCwd(),path));
      if(!orig_path || orig_path[0]==0)
	 path.append('/');
   }
   else if(!xstrcmp(proto,"bm"))
   {
      const char *bm=lftp_bookmarks.Lookup(host);
      if(!bm || !bm[0])
	 return;
      const char *new_url=0;
      if(orig_url)
      {
	 const char *new_path=orig_url+url::path_index(orig_url);
	 if(new_path[0]=='/')
	    new_path++;
	 char *u=alloca_strdup2(bm,strlen(new_path)+1);
	 if(new_path[0]=='/' || new_path[0]=='~')
	    u[url::path_index(u)]=0;
	 assert(u[0]);
	 if(u[strlen(u)-1]!='/' && new_path[0]!='/')
	    strcat(u,"/");
	 else if(u[strlen(u)-1]=='/' && new_path[0]=='/')
	    new_path++;
	 strcat(u,new_path);
	 new_url=u;
      }
      else
	 new_url=url_file(bm,path+(path && path[0]=='/'));
      parse(new_url,proto_required,use_rfc1738);
   }
}

static bool valid_slot(const char *cs)
{
   char *s=alloca_strdup(cs);
   char *slash=strchr(s,'/');
   if(slash)
      *slash=0;
   url::decode_string(s);
   return 0!=ConnectionSlot::Find(s);
}
static bool valid_bm(const char *bm)
{
   char *s=alloca_strdup(bm);
   char *slash=strchr(s,'/');
   if(slash)
      *slash=0;
   url::decode_string(s);
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

char *ParsedURL::Combine(const char *home,bool use_rfc1738)
{
   xstring u("");

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
   if(host && !is_file)
      u.append(url::encode(host,URL_HOST_UNSAFE));
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
   return u.borrow();
}

int url::decode_string(char *str)
{
   char *p=str;
   if(!p)
      return 0;
   char *o=p;
   while(*p)
   {
      if(*p=='%' && p[1] && p[2])
      {
	 int n;
	 if(sscanf(p+1,"%2x",&n)==1)
	 {
	    *o++=n;
	    p+=3;
	    continue;
	 }
      }
      *o++=*p++;
   }
   *o=0;
   return p-str;
}

/* encode_string was taken from wget-1.5.2 and slightly modified */

/* Encodes the unsafe characters (listed in URL_UNSAFE) in a given
   string, producing %XX encoded string.  */
#define need_quote(c) (iscntrl((unsigned char)(c)) || !isascii((unsigned char)(c)) || strchr(unsafe,(c)))
char *url::encode_string (const char *s,char *res,const char *unsafe)
{
  char *p;

  for (p = res; *s; s++)
  {
    if (need_quote(*s))
      {
	const unsigned char c = *s;
	*p++ = '%';
	sprintf(p,"%02X",c);
	p+=2;
      }
    else
      *p++ = *s;
  }
  *p = '\0';
  return res;
}
const char *url::encode(const char *s,const char *unsafe)
{
   if(!s || !*s)
      return s;
   static xstring u;
   u.truncate(0);
   char c;
   while((c=*s++))
   {
      if (need_quote(c))
      {
	 u.append('%');
	 static const char h[]="0123456789ABCDEF";
	 u.append(h[(c>>4)&15]);
	 u.append(h[(c&15)]);
      }
      else
	 u.append(c);
   }
   return u;
}

bool url::dir_needs_trailing_slash(const char *proto)
{
   if(!proto)
      return false;
   return !strcmp(proto,"http")
       || !strcmp(proto,"https");
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
   static xstring buf;
   buf.setf("%.*sXXXX%s",start,url,url+start+len);
   return buf;
}
const char *url::remove_password(const char *url)
{
   int start,len;
   if(!find_password_pos(url,&start,&len))
      return url;
   static char *buf;
   static int buf_alloc;
   int need=strlen(url)-len;
   if(buf_alloc<need)
      buf=(char*)xrealloc(buf,buf_alloc=need);
   sprintf(buf,"%.*s%s",start-1,url,url+start+len);
   return buf;
}
