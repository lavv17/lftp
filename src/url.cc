/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include "xstring.h"
#include <ctype.h>
#include "xalloca.h"
#include "url.h"
#include "ascii_ctype.h"

/*
   URL -> [PROTO://]CONNECT[[:]/PATH]
   CONNECT -> [USER[:PASS]@]HOST[:PORT]
*/

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
   }
   else if(scan[0]==':' && !strncmp(base,"file:",5))
   {
      // special form for file protocol
      *scan=0;
      scan++;
      proto=base;
      memmove(scan+10,scan,strlen(scan)+1);
      host=scan;
      strcpy(host,"localhost");
      path=scan+10;
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
   scan=base;
   while(*scan && *scan!='@')
      scan++;
   if(*scan=='@')
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
   while(*scan && *scan!=':')
      scan++;

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
}

int url::path_index(const char *base)
{
   const char *scan=base;
   while(is_ascii_alpha(*scan))
      scan++;
   if(scan[0]==':' && scan[1]=='/' && scan[2]=='/')
   {
      // found protocol
      const char *slash=strchr(scan+3,'/');
      if(slash)
	 return slash-base;
      return strlen(base);
   }
   else if(scan[0]==':' && !strncmp(base,"file:",5))
   {
      // special form for file protocol
      return scan+1-base;
   }
   return 0;
}

void ParsedURL::Combine(char *url,const char *home,bool use_rfc1738)
{
   bool is_file=!strcmp(proto,"file");
   bool is_ftp=(!strcmp(proto,"ftp") || !strcmp(proto,"hftp"));

   strcpy(url,proto);
   strcat(url,is_file?":":"://");
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
      return;
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
#define need_quote(c) (!unsafe || iscntrl((unsigned char)(c)) || strchr(unsafe,(c)))
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
