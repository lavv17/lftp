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

/*
   URL -> [PROTO://]CONNECT[[:]/PATH]
   CONNECT -> [USER[:PASS]@]HOST[:PORT]
*/

static bool valid_slot(const char *s);
static bool valid_bm(const char *s);

ParsedURL::ParsedURL(const char *url,bool proto_required,bool use_rfc1738)
{
   memory=(char*)xmalloc(strlen(url)*2+20+1);
   strcpy(memory,url);
   orig_url=xstrdup(url);

   proto=0;
   host=0;
   user=0;
   pass=0;
   port=0;
   path=0;

   char *base=memory;
   char *scan=base;
   while(is_ascii_alpha(*scan))
      scan++;
   if(scan[0]==':' && scan[1]=='/' && scan[2]=='/')
   {
      // found protocol
      *scan=0;
      scan+=3;
      proto=base;

      base=scan;

      if(!strcmp(proto,"file") && scan[0]=='/')
	 goto file_with_no_host;
   }
   else if(scan[0]==':' && !strncmp(base,"file:",5))
   {
      // special form for file protocol
      *scan=0;
      scan++;
      proto=base;
   file_with_no_host:
      memmove(scan+10,scan,strlen(scan)+1);
      host=scan;
      strcpy(host,"localhost");
      path=scan+10;
      goto decode;
   }
   else if(scan[0]==':'
   && ((!strncmp(base,"slot:",5) && valid_slot(scan+1))
       || (!strncmp(base,"bm:",3) && valid_bm(scan+1))))
   {
      // special form for selecting a connection slot or a bookmark
      *scan++=0;
      proto=base;
      host=scan;
      scan=strchr(scan,'/');
      if(scan)
      {
	 memmove(scan+1,scan,strlen(scan)+1);
	 *scan++=0;
	 path=scan;
      }
      goto decode;
   }
   else if(proto_required)
   {
      // all the rest is path, if protocol is required.
      path=base;
      goto decode;
   }

   scan=base;
   while(*scan && *scan!='/')
      scan++; // skip host name, port and user:pass

   if(*scan=='/') // directory
   {
      if(scan-1>=base && scan[-1]==':')
	 scan[-1]=0;
      if(scan[1]!='~')
      {
	 memmove(scan+1,scan,strlen(scan)+1);
	 if((!xstrcmp(proto,"ftp") || !xstrcmp(proto,"hftp"))
	 && use_rfc1738)
	 {
	    // special handling for ftp protocol.
	    if(!strncasecmp(scan+2,"%2F",3))
	    {
	       char *p=scan+5+(scan[5]=='/');
	       memmove(scan+2,p,strlen(p)+1);
	    }
	    else
	    {
	       if(!(is_ascii_alpha(scan[2]) && scan[3]==':' && scan[4]=='/'))
	       {
		  memmove(scan+3,scan+2,strlen(scan+2)+1);
		  scan[1]='~';
		  scan[2]='/';
	       }
	    }
	 }
      }
      *scan++=0;
      path=scan;
   }
   else if(proto)
   {
      if(!strcmp(proto,"http") || !strcmp(proto,"https"))
      {
	 scan++;
	 scan[0]='/';
	 scan[1]=0;
	 path=scan;
      }
   }

   // try to extract user name/password
   scan=strrchr(base,'@');
   if(scan)
   {
      *scan++=0;
      user=base;
      base=scan;

      scan=user;
      while(*scan && *scan!=':')
	 scan++;
      if(*scan==':')
      {
	 *scan++=0;
	 pass=scan;
      }
   }

   // extract host name/password
   scan=base;
   host=base;
   if(*scan=='[') // RFC2732 [ipv6]
   {
      while(*scan && *scan!=']')
	 scan++;
      if(*scan==']')
      {
	 *scan++=0;
	 host++;
      }
      else
	 scan=base;
   }

   if(scan==base)
   {
      while(*scan && *scan!=':')
	 scan++;
   }

   if(*scan==':') // port found
   {
      if(strchr(scan+1,':')==0)
      {
	 *scan++=0;
	 port=scan;
      }
      else
      {
	 /* more than one colon - maybe it is ipv6 digital address */
      }
   }

decode:
   url::decode_string(user);
   url::decode_string(pass);
   url::decode_string(host);
   url::decode_string(path);

   FileAccess *fa=0;
   if(!xstrcmp(proto,"slot"))
   {
      fa=ConnectionSlot::FindSession(host);
      if(!fa)
	 return;
      xfree(orig_url);
      orig_url=0;
      char *orig_path=alloca_strdup(path);
      xfree(memory);
      memory=(char*)xmalloc(
		     xstrlen(fa->GetProto())+1
		    +xstrlen(fa->GetUser())+1
		    +xstrlen(fa->GetPassword())+1
		    +xstrlen(fa->GetHostName())+1
		    +xstrlen(fa->GetPort())+1
		    +xstrlen(fa->GetCwd())+2
		    +xstrlen(orig_path)+1);
      proto=user=pass=host=port=path=0;
      char *next=memory;
      proto=next;
      strcpy(proto,fa->GetProto());
      next=proto+strlen(proto)+1;
      if(fa->GetUser())
      {
	 user=next;
	 strcpy(user,fa->GetUser());
	 next=user+strlen(user)+1;
      }
      if(fa->GetPassword())
      {
	 pass=next;
	 strcpy(pass,fa->GetPassword());
	 next=pass+strlen(pass)+1;
      }
      if(fa->GetHostName())
      {
	 host=next;
	 strcpy(host,fa->GetHostName());
	 next=host+strlen(host)+1;
      }
      if(fa->GetPort())
      {
	 port=next;
	 strcpy(port,fa->GetPort());
	 next=port+strlen(port)+1;
      }
      if(fa->GetCwd())
      {
	 path=next;
	 orig_path+=(orig_path!=0 && orig_path[0]=='/');
	 strcpy(path,dir_file(fa->GetCwd(),orig_path));
	 if(!orig_path || orig_path[0]==0)
	    strcat(path,"/");
      }
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
      ParsedURL bu(new_url);
      // move the data
      xfree(memory);
      xfree(orig_url);
      memcpy(this,&bu,sizeof(bu));
      bu.memory=0; // so that dtor won't free it
      bu.orig_url=0;
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
   int len=1;
   if(proto)
      len+=strlen(proto)+strlen("://");
   if(user)
   {
      len+=strlen(user)*3+1;
      if(pass)
	 len+=strlen(pass)*3+1;
   }
   if(host)
      len+=strlen(host)*3;
   if(port)
      len+=1+strlen(port)*3;
   if(path)
      len+=1+strlen(path)*3;

   char *url=(char*)xmalloc(len);

   bool is_file=!xstrcmp(proto,"file");
   bool is_ftp=(!xstrcmp(proto,"ftp") || !xstrcmp(proto,"hftp"));

   url[0]=0;
   if(proto)
   {
      strcpy(url,proto);
      strcat(url,is_file?":":"://");
   }
   if(user && !is_file)
   {
      url::encode_string(user,url+strlen(url),URL_USER_UNSAFE);
      if(pass)
      {
	 strcat(url,":");
	 url::encode_string(pass,url+strlen(url),URL_PASS_UNSAFE);
      }
      strcat(url,"@");
   }
   if(host && !is_file)
      url::encode_string(host,url+strlen(url),URL_HOST_UNSAFE);
   if(port && !is_file)
   {
      strcat(url,":");
      url::encode_string(port,url+strlen(url),URL_PORT_UNSAFE);
   }
   if(path==0)
      return url;
   if(strcmp(path,"~"))
   {
      if(path[0]!='/' && !is_file) // e.g. ~/path
	 strcat(url,"/");
      int p_offset=0;
      if(is_ftp && use_rfc1738)
      {
	 // some cruft for ftp urls...
	 if(path[0]=='/' && xstrcmp(home,"/"))
	 {
	    strcat(url,"/%2F");
	    p_offset=1;
	 }
	 else if(path[0]=='~' && path[1]=='/')
	    p_offset=2;
      }
      url::encode_string(path+p_offset,url+strlen(url),URL_PATH_UNSAFE);
   }
   return url;
}

void url::decode_string(char *p)
{
   if(!p)
      return;
   while(*p)
   {
      if(*p=='%' && p[1] && p[2])
      {
	 int n;
	 if(sscanf(p+1,"%2x",&n)==1)
	 {
	    *p++=n;
	    memmove(p,p+2,strlen(p+2)+1);
	    continue;
	 }
      }
      p++;
   }
}

/* encode_string was taken from wget-1.5.2 and slightly modified */

/* Encodes the unsafe characters (listed in URL_UNSAFE) in a given
   string, returning a malloc-ed %XX encoded string.  */
#define need_quote(c) (!unsafe || iscntrl((unsigned char)(c)) || !isascii((unsigned char)(c)) || strchr(unsafe,(c)))
char *url::encode_string (const char *s,char *res,const char *unsafe)
{
  char *p;

#if 0 // this easily leads to memory leaks.
  int i;
  if (res==0)
  {
     const char *b = s;
     for (i = 0; *s; s++, i++)
       if (need_quote(*s))
	 i += 2; /* Two more characters (hex digits) */
     res = (char *)xmalloc (i + 1);
     s = b;
  }
#endif
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
   static char *buf;
   static int buf_alloc;
   int need=strlen(url)+5;
   if(buf_alloc<need)
      buf=(char*)xrealloc(buf,buf_alloc=need);
   sprintf(buf,"%.*sXXXX%s",start,url,url+start+len);
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
