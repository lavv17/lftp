/*
 * lftp - file transfer program
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
#include "ascii_ctype.h"
#include <assert.h>
#include "HttpDir.h"
#include "url.h"
#include "ArgV.h"
#include "LsCache.h"
#include "misc.h"
#include "log.h"
#include "DirColors.h"

static bool token_eq(const char *buf,int len,const char *token)
{
   int token_len=strlen(token);
   if(len<token_len)
      return false;
   return !strncasecmp(buf,token,token_len)
	    && (token_len==len || !is_ascii_alnum(buf[token_len]));
}

static bool find_value(const char *scan,const char *more,const char *name,char *store)
{
   for(;;)
   {
      while(is_ascii_space(*scan))
	 scan++;
      if(scan>=more)
	 return false;

      if(!is_ascii_alnum(*scan))
      {
	 scan++;
	 continue;
      }

      bool match=token_eq(scan,more-scan,name);

      while(is_ascii_alnum(*scan))
	 scan++;
      if(scan>=more)
	 return false;

      if(*scan!='=')
	 continue;

      scan++;
      char quote=0;
      if(*scan=='"' || *scan=='\'')
	 quote=*scan++;

      while(scan<more && (quote ? *scan!=quote : !is_ascii_space(*scan)))
      {
	 if(match)
	    *store++=*scan;
	 scan++;
      }
      if(match)
      {
	 *store=0;
	 return true;
      }
      if(scan>=more)
	 return false;
      if(quote)
	 scan++;  // skip closing quotation mark.
   }
   return false;
}

static
void remove_tags(char *buf)
{
   for(;;)
   {
      char *less=strchr(buf,'<');
      char *amp=strstr(buf,"&nbsp;");
      if(!less && !amp)
	 break;
      if(amp && (!less || amp<less))
      {
	 amp[0]=' ';
	 memmove(amp+1,amp+6,strlen(amp+6)+1);
	 buf=amp+1;
	 continue;
      }
#if 0
      if(token_eq(less+1,strlen(less+1),"a"))
      {
	 // don't allow anchors to be skipped.
      	 *less=0;
	 break;
      }
#endif
      char *more=strchr(less+1,'>');
      if(!more)
	 break;
      memmove(less,more+1,strlen(more+1)+1);
      buf=less;
   }
}

#if 0 // unused
static
const char *strncasestr(const char *buf,int len,const char *str)
{
   int str_len=strlen(str);
   while(len>=str_len)
   {
      if(!strncasecmp(buf,str,str_len))
	 return buf;
      buf++;
      len--;
   }
   return 0;
}
#endif

static
const char *find_eol(const char *buf,int len,bool eof,int *eol_size)
{
   const char *real_eol=find_char(buf,len,'\n');
   // check if the tag after eol is <TD> or </TD>
   while(real_eol)
   {
      const char *scan=real_eol+1;
      while(scan<buf+len && is_ascii_space(*scan))
	 scan++;  // skip space
      if(scan<buf+len && *scan!='<')
	 break;
      if(scan+5>buf+len)
      {
	 if(!eof)
	    real_eol=0;
	 break;
      }
      if(strncasecmp(scan,"<td",3) && strncasecmp(scan,"</td",4))
	 break;
      real_eol=find_char(scan,len-(scan-buf),'\n');
   }
   const char *less=find_char(buf,len,'<');;
   const char *more=0;
   if(less)
   {
      int rest=len-(less+1-buf);
      more=find_char(less+1,rest,'>');
      if(more
      && !token_eq(less+1,rest,"br")
      && !token_eq(less+1,rest,"/tr")
      && !token_eq(less+1,rest,"tr"))
      {
	 // if the tag is finished and not BR nor /TR nor TR, ignore it.
	 less=0;
	 more=0;
      }
   }
   // is real_eol past the tag?
   if(real_eol && less && real_eol>less)
      real_eol=0;  // then ignore it.
   // real_eol not found?
   if(!real_eol)
   {
      // BR or /TR found?
      if(less && more)
      {
	 *eol_size=more-less+1;
	 return less;
      }
      *eol_size=0;
      if(eof)
	 return buf+len;
      return 0;
   }
   *eol_size=1;
   if(real_eol>buf && real_eol[-1]=='\r')
   {
      real_eol--;
      (*eol_size)++;
   }
   return real_eol;
}

/* This function replaces &amp; &lt; &gt; &quot; to appropriate characters */
static void decode_amps(char *s)
{
   static const struct pair
      { char str[7]; char ch; }
   table[]={
      { "&amp;",  '&' },
      { "&lt;",   '<' },
      { "&gt;",   '>' },
      { "&quot;", '"' },
      { "", 0 }
   };
   const struct pair *scan;

   for(char *a=s; a; a=strchr(a,'&'))
   {
      for(scan=table; scan->ch; scan++)
      {
	 int len=strlen(scan->str);
	 if(!strncmp(a,scan->str,len))
	 {
	    *a=scan->ch;
	    memmove(a+1,a+len,strlen(a+len)+1);
	    break;
	 }
      }
      a++;
   }
}

class file_info
{
public:
   long long size;
   int year,month,day,hour,minute,second;
   char *sym_link;
   bool free_sym_link;
   bool is_sym_link;
   bool is_directory;
   char month_name[32];
   char size_str[32];
   char perms[12];
   char user[32];
   char group[32];
   int nlink;
   time_t date;
   int date_prec;

   void clear();
   bool validate();

   file_info()
   {
      is_directory=false;
      free_sym_link=false;
      clear();
   }
};
void file_info::clear()
{
   size=-1;
   year=-1;
   month=-1;
   day=0;
   hour=-1;
   minute=-1;
   second=-1;
   month_name[0]=0;
   size_str[0]=0;
   perms[0]=0;
   if(free_sym_link)
      xfree(sym_link);
   sym_link=0;
   is_sym_link=false;
   user[0]=0;
   group[0]=0;
   nlink=0;
   date=NO_DATE;
   date_prec=-1;
}
bool file_info::validate()
{
   if(year!=-1)
   {
      // server's y2000 problem :)
      if(year<37)
	 year+=2000;
      else if(year<100)
	 year+=1900;
   }

   if(day<1 || day>31 || hour<-1 || hour>23 || minute<-1 || minute>59
   || (month==-1 && !is_ascii_alnum(month_name[0])))
      return false;

   return true;
}


#define debug(str) Log::global->Format(10,"* %s\n",str)

static bool try_apache_listing(file_info &info,const char *str)
{
   info.clear();

   // usual apache listing: DD-Mon-YYYY hh:mm size
   int n=sscanf(str,"%2d-%3s-%4d %2d:%2d %30s",
	       &info.day,info.month_name,&info.year,
	       &info.hour,&info.minute,info.size_str);
   if(n==6 && (info.size_str[0]=='-' || is_ascii_digit(info.size_str[0])))
   {
      debug("apache listing matched");
      return true;
   }
   return false;
}
static bool try_apache_listing_unusual(file_info &info,const char *str)
{
   info.clear();

   // unusual apache listing: size DD-Mon-YYYY
   int n=sscanf(str,"%30s %2d-%3s-%d",
	       info.size_str,&info.day,info.month_name,&info.year);
   if(n==4 && (info.size_str[0]=='-' || is_ascii_digit(info.size_str[0])))
   {
      debug("unusual apache listing matched");
      return true;
   }
   return false;
}
static bool try_netscape_proxy(file_info &info,const char *str)
{
   info.clear();

   char size_unit[7];
   char week_day[4];
   // Netscape-Proxy 2.53
   int n=sscanf(str,"%lld %6s %3s %3s %d %2d:%2d:%2d %4d",
	       &info.size,size_unit,week_day,
	       info.month_name,&info.day,
	       &info.hour,&info.minute,&info.second,&info.year);
   if(n==9)
   {
      if(!strcasecmp(size_unit,"bytes")
      || !strcasecmp(size_unit,"byte"))
	 sprintf(info.size_str,"%lld",info.size);
      else
      {
	 sprintf(info.size_str,"%lld%s",info.size,size_unit);
	 info.size=-1;
      }
      debug("Netscape-Proxy 2.53 listing matched");
      return true;
   }
   n=sscanf(str,"%3s %3s %d %2d:%2d:%2d %4d %30s",
	    week_day,info.month_name,&info.day,
	    &info.hour,&info.minute,&info.second,&info.year,info.size_str);
   if(n==7 || (n==8 && !is_ascii_digit(info.size_str[0])))
   {
      strcpy(info.size_str,"-");
      if(!info.is_directory)
	 info.is_sym_link=true;
      debug("Netscape-Proxy 2.53 listing matched (dir/symlink)");
      return true;
   }
   return false;
}
static bool try_squid_eplf(file_info &info,const char *str)
{
   info.clear();

   char week_day[4];
   int n=sscanf(str,"%3s %3s %d %2d:%2d:%2d %4d %30s",
	    week_day,info.month_name,&info.day,
	    &info.hour,&info.minute,&info.second,&info.year,info.size_str);
   if(n==8) // maybe squid's EPLF listing.
   {
      // no symlinks here.
      debug("squid EPLF listing matched");
      return true;
   }
   return false;
}
static bool try_mini_proxy(file_info &info,const char *buf)
{
   info.clear();

   char PM[3];
   // Mini-Proxy web server.
   if(7==sscanf(buf,"%d/%d/%d %d:%d %2s %30s",
	       &info.month,&info.day,&info.year,
	       &info.hour,&info.minute,PM,info.size_str))
   {
      if(!strcasecmp(PM,"PM"))
      {
	 info.hour+=12;
	 if(info.hour==24)
	    info.hour=0;
      }
      if(!is_ascii_digit(info.size_str[0]))
      {
	 if(!strcasecmp(info.size_str,"<dir>"))
	    info.is_directory=true;
	 strcpy(info.size_str,"-");
      }
      info.month--;
      debug("Mini-Proxy web server listing matched");
      return true;
   }
   return false;
}
static bool try_apache_unixlike(file_info &info,const char *buf,
   const char *more,const char *more1,const char **info_string,int *info_string_len)
{
   info.clear();

   // Apache Unix-like listing (from apache proxy):
   //   Perms Nlnk user [group] size Mon DD (YYYY or hh:mm)
   int perms_code;
   char year_or_time[6];
   int consumed;

   int n=sscanf(buf,"%11s %d %31s %31s %lld %3s %2d %5s%n",info.perms,&info.nlink,
	       info.user,info.group,&info.size,info.month_name,&info.day,
	       year_or_time,&consumed);
   if(n==4) // bsd-like listing without group?
   {
      info.group[0]=0;
      n=sscanf(buf,"%11s %d %31s %lld %3s %2d %5s%n",info.perms,&info.nlink,
	    info.user,&info.size,info.month_name,&info.day,year_or_time,&consumed);
   }
   if(n>=7 && -1!=(perms_code=parse_perms(info.perms+1))
   && -1!=(info.month=parse_month(info.month_name))
   && -1!=parse_year_or_time(year_or_time,&info.year,&info.hour,&info.minute))
   {
      sprintf(info.size_str,"%lld",info.size);
      if(info.perms[0]=='d')
	 info.is_directory=true;
      else if(info.perms[0]=='l')
      {
	 info.is_sym_link=true;
	 char *str=string_alloca(more1-more);
	 memcpy(str,more+1,more1-more-4);
	 str[more1-more-4]=0;
	 str=strstr(str," -> ");
	 if(str)
	 {
	    info.sym_link=xstrdup(str+4);
	    info.free_sym_link=true;
	 }
      }
      *info_string=buf;
      *info_string_len=consumed;
      debug("apache ftp over http proxy listing matched");
      return true;
   }
   return false;
}

static bool try_roxen(file_info &info,const char *str)
{
   info.clear();

   // Roxen listing ([size] {kb/Mb} application/octet-stream YYYY-MM-DD)
   // or (directory YYYY-MM-DD)
   char size_mod[6];
   long size_mod_i=0;

   str=strchr(str+(*str=='\n'),'\n');
   if(!str)
      return false;
   int n=sscanf(str,"%26s %5s %*[a-z0-9/-] %4d-%2d-%2d",info.size_str,size_mod,
	 &info.year,&info.month,&info.day);
   if(n==5)
   {
      if(!strncmp(size_mod,"byte",4))
	 size_mod_i=1;
      else if(!strcmp(size_mod,"kb"))
	 size_mod_i=1024;
      else if(!strcmp(size_mod,"Mb"))
	 size_mod_i=1024*1024;
      else if(!strcmp(size_mod,"Gb"))
	 size_mod_i=1024*1024*1024;
      if(size_mod_i)
      {
	 sprintf(info.size_str,"%s%s",info.size_str,size_mod);
	 debug("Roxen web server listing matched");
	 return true;
      }
   }
   strcpy(info.size_str,"-");
   n=sscanf(str," directory %4d-%2d-%2d",&info.year,&info.month,&info.day);
   if(n==3)
   {
      debug("Roxen web server listing matched (directory)");
      info.is_directory=true;
      return true;
   }
   return false;
}
static bool try_squid_ftp(file_info &info,const char *str,char *str_with_tags)
{
   info.clear();

   char year_or_time[6];

   // squid's ftp listing: Mon DD (YYYY or hh:mm) [size]
   int n=sscanf(str,"%3s %2d %5s %30s",info.month_name,&info.day,year_or_time,info.size_str);
   if(n<3)
      return false;
   if(!is_ascii_digit(info.size_str[0]))
      strcpy(info.size_str,"-");
   if(-1==parse_year_or_time(year_or_time,&info.year,&info.hour,&info.minute))
      return false;
   if(-1==parse_month(info.month_name))
      return false;  // be strict.

   char *ptr;
   ptr=strstr(str_with_tags," -> <A HREF=\"");
   if(ptr)
   {
      info.is_sym_link=true;
      info.sym_link=ptr+13;
      ptr=strchr(info.sym_link,'"');
      if(!ptr)
	 info.sym_link=0;
      else
      {
	 *ptr=0;
	 url::decode_string(info.sym_link);
      }
   }
   debug("squid ftp listing matched");
   return true;
}

static bool try_wwwoffle_ftp(file_info &info,const char *buf,
   const char *ext,const char **info_string,int *info_string_len)
{
   info.clear();

   //   Perms Nlnk user [group] size Mon DD (YYYY or hh:mm)
   int perms_code;
   char year_or_time[6];
   int consumed;

   int n=sscanf(buf,"%11s %d %31s %31s %lld %3s %2d %5s%n",info.perms,&info.nlink,
	       info.user,info.group,&info.size,info.month_name,&info.day,
	       year_or_time,&consumed);
   if(n==4) // bsd-like listing without group?
   {
      info.group[0]=0;
      n=sscanf(buf,"%11s %d %31s %lld %3s %2d %5s%n",info.perms,&info.nlink,
	    info.user,&info.size,info.month_name,&info.day,year_or_time,&consumed);
   }
   if(n>=7 && -1!=(perms_code=parse_perms(info.perms+1))
   && -1!=(info.month=parse_month(info.month_name))
   && -1!=parse_year_or_time(year_or_time,&info.year,&info.hour,&info.minute))
   {
      sprintf(info.size_str,"%lld",info.size);
      if(info.perms[0]=='d')
	 info.is_directory=true;
      else if(info.perms[0]=='l')
      {
	 info.is_sym_link=true;
	 const char *p=strstr(ext,"-&gt; ");
	 if(p)
	 {
	    info.sym_link=xstrdup(p+6);
	    info.free_sym_link=true;
	 }
      }
      *info_string=buf;
      *info_string_len=consumed;
      debug("wwwoffle ftp over http proxy listing matched");
      return true;
   }
   return false;
}

//     4096             Jun 25 23:48Directory| ***
//     4096             Jun 23  2002Directory| ***
//       50             Jul  9 18:37Symbolic Link| ***
//      217             Jun  3 06:01Plain Text| ***
//    40419             Jul  6 13:06Hypertext Markup Language| ***
// 14289850             Jul 16 17:04Windows Bitmap| ***
//  6668926             Jul 17 16:01Binary Executable| ***
static bool try_csm_proxy(file_info &info,const char *str)
{
   info.clear();

   int n;
   int status = false;
   char additional_file_info[33];
   int has_additional_file_info = false;

   memset(additional_file_info, '\0', sizeof (additional_file_info));

   // try to match hour:minute
   if (5 <= (n = sscanf(str,"%lld %3s %d %2d:%2d%32s",
	       &info.size, info.month_name, &info.day, &info.hour, &info.minute, additional_file_info))) {
      status = true;
      if (6 == n)
	  has_additional_file_info = true;
   } else {
       // try to match year instead of hour:minute
       info.clear();
       if (4 <= (n = sscanf(str,"%lld %3s %d %4d%32s",
		       &info.size, info.month_name, &info.day, &info.year, additional_file_info))) {
	   status = true;
	   if (5 == n)
	       has_additional_file_info = true;
       }
   }

   if (status) {
      debug("csm_proxy listing matched");
      sprintf(info.size_str, "%lld", info.size);
      if (has_additional_file_info && additional_file_info[0]) {
	  if (!strncasecmp("Symbolic Link",additional_file_info,13)) {
	      info.is_sym_link = true;
	  } else if (!strncasecmp("Directory",additional_file_info,9)) {
	      info.is_directory = true;
	  } else {
	      // fprintf(stderr, "try_csm_proxy: |%s|\n", additional_file_info);
	      Log::global->Format(10,
		      "* try_csm_proxy: unknown file type '%s'\n",
		      additional_file_info);
	  }
      }
   }
   return status;
}

// this procedure is highly inefficient in some cases,
// esp. when it has to return for more data many times.
static int parse_html(const char *buf,int len,bool eof,Buffer *list,
      FileSet *set,FileSet *all_links,ParsedURL *prefix,char **base_href,
      LsOptions *lsopt=0, int color = 0)
{
   const char *end=buf+len;
   const char *less=find_char(buf,len,'<');
   int eol_len=0;
   int skip_len=0;
   const char *eol;

   eol=find_eol(buf,len,eof,&eol_len);
   if(eol)
      skip_len=eol-buf+eol_len;

   if(less==0)
      return skip_len;
   if(skip_len>0 && eol<less)
      return skip_len;
   if(end-less-1>=3 && less[1]=='!' && less[2]=='-' && less[3]=='-')
   {
      // found comment
      if(end-less-4<3)
	 return less-buf;
      const char *scan=less+4;
      for(;;)
      {
	 const char *eoc=find_char(scan,end-scan,'>');
	 if(!eoc)
	 {
	    if(eof)  // unterminated comment.
	       return len;
	    return less-buf;
	 }
	 if(eoc>=less+4+2 && eoc[-1]=='-' && eoc[-2]=='-')
	    return eoc+1-buf;
	 scan=eoc+1;
      }
   }
   // FIXME: a > sign can be inside quoted value. (?)
   const char *more=find_char(less+1,end-less-1,'>');
   if(more==0)
   {
      if(eof)
	 return len;
      return 0;
   }
   // we have found a tag
   int tag_len=more-buf+1;
   if(more-less<3)
      return tag_len;   // too small

   int max_link_len=more-less+1+2;
   if(base_href && *base_href)
      max_link_len+=strlen(*base_href)+1;
   char *link_target=(char*)alloca(max_link_len);

   static const struct tag_link
	 { const char *tag, *link; }
      tag_list[]={
      /* taken from wget-1.5.3: */
      /* NULL-terminated list of tags and modifiers someone would want to
	 follow -- feel free to edit to suit your needs: */
	 { "a", "href" },
	 { "img", "src" },
	 { "img", "href" },
	 { "body", "background" },
	 { "frame", "src" },
	 { "iframe", "src" },
	 { "fig", "src" },
	 { "overlay", "src" },
	 { "applet", "code" },
	 { "script", "src" },
	 { "embed", "src" },
	 { "bgsound", "src" },
	 { "area", "href" },
	 { "img", "lowsrc" },
	 { "input", "src" },
	 { "layer", "src" },
	 { "table", "background"},
	 { "th", "background"},
	 { "td", "background"},
	 /* Tags below this line are treated specially.  */
	 { "base", "href" },
	 { "meta", "content" },
	 { NULL, NULL }
      };

   // FIXME: a tag can have many links.
   const struct tag_link *tag_scan;
   for(tag_scan=tag_list; tag_scan->tag; tag_scan++)
   {
      if(token_eq(less+1,end-less-1,tag_scan->tag))
      {
	 if(find_value(less+1+strlen(tag_scan->tag),more,
			tag_scan->link,link_target))
	    break;
      }
   }
   if(tag_scan->tag==0)
      return tag_len;	// not interesting

   bool hftp=(prefix && (!xstrcmp(prefix->proto,"hftp")
		      || !xstrcmp(prefix->proto,"ftp")));

   // ok, found the target.

   decode_amps(link_target);  // decode all &amp; and similar

   if(!strcasecmp(tag_scan->tag,"base"))
   {
      if(base_href)
      {
	 xfree(*base_href);
	 *base_href=xstrdup(link_target,+2);
	 if(hftp)
	 {
	    // workaround apache proxy bugs.
	    char *t=strstr(*base_href,";type=");
	    if(t && t[6] && t[7]=='/' && t[8]==0)
	       *t=0;
	    char *p=*base_href+url::path_index(*base_href);
	    if(p[0]=='/' && p[1]=='/')
      	    {
	       memmove(p+4,p+2,strlen(p+2)+1);
	       memcpy(p+1,"%2F",3);
	    }
	 }
      }
      return tag_len;
   }
   if(!strcasecmp(tag_scan->tag,"meta"))
   {
      // skip 0; URL=
      while(*link_target && is_ascii_digit(*link_target))
	 link_target++;
      if(*link_target!=';')
	 return tag_len;
      link_target++;
      while(*link_target && is_ascii_space(*link_target))
	 link_target++;
      if(strncasecmp(link_target,"URL=",4))
	 return tag_len;
      link_target+=4;
      if(link_target[0]=='\'')
      {
	 // FIXME: maybe a more complex value parser is required.
	 link_target++;
	 int len=strlen(link_target);
	 if(len>0 && link_target[len-1]=='\'')
	    link_target[len-1]=0;
      }
   }

   bool icon=false;
   if(!strcasecmp(tag_scan->tag,"img")
   && !strcasecmp(tag_scan->link,"src"))
      icon=true;

   bool a_href=false;
   if(!strcasecmp(tag_scan->tag,"a")
   && !strcasecmp(tag_scan->link,"href"))
      a_href=true;

   // check if the target is a relative and not a cgi
   if(strchr(link_target,'?'))
      return tag_len;	// cgi
   char *c=strchr(link_target,'#');
   if(c)
      *c=0; // strip pointer inside document.
   if(*link_target==0)
      return tag_len;	// no target ?

   // netscape internal icons
   if(icon && !strncasecmp(link_target,"internal-gopher",15))
      return tag_len;

   if(link_target[0]=='/' && link_target[1]=='~')
      link_target++;

   bool base_href_applied=false;

parse_url_again:
   ParsedURL link_url(link_target,/*proto_required=*/true);
   if(link_url.proto)
   {
      if(!prefix)
	 return tag_len;	// no way

      if(xstrcmp(link_url.proto,prefix->proto+hftp)
      || xstrcmp(link_url.host,prefix->host)
      || xstrcmp(link_url.user,prefix->user)
      || xstrcmp(link_url.port,prefix->port))
	 return tag_len;	// no match
   }
   else
   {
      const char *scan_link=link_target;
      while(*scan_link)
      {
	 if(scan_link>link_target && *scan_link==':')
	    return tag_len;   // special url, like mailto:
	 if(!is_ascii_alpha(*scan_link))
	    break;
	 scan_link++;
      }
      if(*link_target!='/' && base_href && *base_href && !base_href_applied)
      {
	 char *base_end=strrchr(*base_href,'/');
	 if(base_end)
	 {
	    memmove(link_target+(base_end-*base_href+1),link_target,
	       strlen(link_target)+1);
	    memcpy(link_target,*base_href,(base_end-*base_href+1));
	    base_href_applied=true;
	    goto parse_url_again;
	 }
      }
   }

   // ok, it is good relative link
   if(link_url.path==0)
      strcpy(link_target,"/");
   else
      strcpy(link_target,link_url.path);

   if(link_target[0]=='/' && link_target[1]=='/' && hftp)
   {
      // workaround for apache proxy.
      link_target++;
   }

   int link_len=strlen(link_target);

   file_info info;
   info.is_directory=(link_len>0 && link_target[link_len-1]=='/');
   if(info.is_directory && link_len>1)
      link_target[--link_len]=0;

   if(prefix)
   {
      const char *p_path=prefix->path;
      if(p_path==0)
	 p_path="~";
      int p_len=strlen(p_path);
      if(p_len==1 && p_path[0]=='/' && link_target[0]=='/')
      {
	 if(link_len>1)
	 {
	    // strip leading slash
	    link_len--;
	    memmove(link_target,link_target+1,link_len+1);
	 }
      }
      else if(p_len>0 && !strncmp(link_target,p_path,p_len))
      {
	 if(link_target[p_len]=='/')
	 {
	    link_len=strlen(link_target+p_len+1);
	    memmove(link_target,link_target+p_len+1,link_len+1);
	 }
	 else if(link_target[p_len]==0)
	 {
	    strcpy(link_target,".");
	    link_len=1;
	 }
	 if(link_target[0]=='.' && link_target[1]=='/')
	 {
	    link_len-=2;
	    memmove(link_target,link_target+2,link_len+1);
	 }
      }
      else
      {
	 // try ..
	 const char *rslash=strrchr(p_path,'/');
	 if(rslash)
	 {
	    p_len=rslash-p_path;
	    if(p_len>0 && !strncmp(link_target,p_path,p_len)
	    && link_target[p_len]==0)
	    {
	       strcpy(link_target,"..");
	       link_len=2;
	    }
	 }
      }
   }

   char *type=strstr(link_target,";type=");
   if(type && type[6] && !type[7])
   {
      type[0]=0;
      if(!all_links || all_links->FindByName(link_target))
	 return tag_len;
      type[0]=';';
   }

   bool show_in_list=true;
   if(icon && (link_target[0]=='/' || link_target[0]=='~'))
      show_in_list=false;  // makes apache listings look better.

   skip_len=tag_len;
   if(list && show_in_list)
   {
      const char *more1;
      char *str,*str_with_tags, *str2;
      char *line_add=(char*)alloca(link_len+128+2*1024);
      const char *info_string=0;
      int         info_string_len=0;
      int type;

      if(!a_href)
	 goto add_file_no_info;	// only <a href> tags can have useful info.

      // try to extract file information
      more1=more;
   find_a_end:
      for(;;)
      {
	 more1++;
	 more1=find_char(more1,end-more1,'>');
	 if(!more1)
	 {
	    if(eof)
	       goto add_file_no_info;
	    if(end-more>2*1024) // too long a-href
	       goto add_file_no_info;
	    return 0;  // no full a-href yet
	 }
	 if(!strncasecmp(more1-3,"</a",3))
	    break;
      }
      // get a whole line in buffer if possible.
      eol=find_eol(more1+1,end-more1-1,eof,&eol_len);
      if(!eol)
      {
	 if(!eof && end-more<=2*1024)
	    return 0;  // no full line yet
	 eol=end;
	 eol_len=0;
      }

      // little workaround for squid's ftp listings
      if(more1[1]==' ' && eol-more1>more-less+10
      && !strncmp(more1+2,less,more-less+1))
      {
	 more1=more1+2+(more-less);
	 goto find_a_end;
      }
      if(more1[1]==' ')
	 more1++;
      while(more1+1+2<eol && more1[1]=='.' && more1[2]==' ')
	 more1+=2;

      // the buffer is not null-terminated, so we need this
      str=string_alloca(eol-more1);
      memcpy(str,more1+1,eol-more1-1);
      str[eol-more1-1]=0;
      str_with_tags=alloca_strdup(str);
      remove_tags(str);

      if(try_apache_listing(info,str)		&& info.validate()) goto got_info;
      if(try_apache_listing_unusual(info,str)	&& info.validate()) goto got_info;
      if(try_netscape_proxy(info,str)		&& info.validate()) goto got_info;
      if(try_squid_eplf(info,str) && info.validate())
      {
	 // skip rest of line, because there may be another href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      if(try_mini_proxy(info,str)		&& info.validate()) goto got_info;
      if(try_apache_unixlike(info,str,more,more1,&info_string,&info_string_len)
      && info.validate())
	 goto got_info;
      if(try_roxen(info,str)			&& info.validate()) goto got_info;
      if(try_squid_ftp(info,str,str_with_tags) && info.validate())
      {
	 // skip rest of line, because there may be href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      // wwwoffle
      str2=string_alloca(less-buf+1);
      memcpy(str2,buf,less-buf);
      str2[less-buf]=0;
      if(try_wwwoffle_ftp(info,str2,str,&info_string,&info_string_len)
      && info.validate())
      {
	 // skip rest of line, because there may be href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      if(try_csm_proxy(info,str) && info.validate()) goto got_info;

   add_file_no_info:
      sprintf(line_add,"%s  --  %s",
	    info.is_directory?"drwxr-xr-x":"-rw-r--r--",link_target);
      goto append_type_maybe;

   got_info:
      if(info_string)
      {
	 sprintf(line_add,"%.*s %s",info_string_len,info_string,link_target);
	 goto append_symlink_maybe;
      }
      if(info.month==-1)
	 info.month=parse_month(info.month_name);
      if(info.month>=0)
      {
	 sprintf(info.month_name,"%02d",info.month+1);
	 if(info.year==-1)
	    info.year=guess_year(info.month,info.day,info.hour,info.minute);
      }
      if(info.perms[0]==0)
      {
	 if(info.is_directory)
	    strcpy(info.perms,"drwxr-xr-x");
	 else if(info.is_sym_link)
	    strcpy(info.perms,"lrwxrwxrwx");
	 else
	    strcpy(info.perms,"-rw-r--r--");
      }

      sprintf(line_add,"%s  %11s  %04d-%s-%02d",
	 info.perms,info.size_str,info.year,info.month_name,info.day);

      if (info.hour >= 0 || info.minute >= 0) {
	  if (info.hour >= 0) {
	      char hour[6];
	      sprintf(hour, " %02d:", info.hour);
	      strcat(line_add, hour);
	  } else {
	      strcat(line_add, " --:");
	  }

	  if (info.minute >= 0) {
	      char minute[4];
	      sprintf(minute, "%02d", info.minute);
	      strcat(line_add, minute);
	  } else {
	      strcat(line_add, "--");
	  }
      } else {
	  // neither hour nor minute are given
	  strcat(line_add, "      ");
      }

      strcat(line_add, "  ");

      type = FileInfo::NORMAL;

      if (color) {
	  if(info.is_directory)
	      type = FileInfo::DIRECTORY;
	  else if(info.is_sym_link && !info.sym_link)
	      type = FileInfo::SYMLINK;
      }

      if (color && FileInfo::NORMAL != type && all_links && !all_links->FindByName(link_target)) {
	  list->Put(line_add);
	  DirColors::GetInstance()->PutColored(list, link_target, type);
	  line_add[0] = '\0'; // reset
      } else {
	  strcat(line_add, link_target);
      }

   append_symlink_maybe:
      if(info.sym_link)
	 sprintf(line_add+strlen(line_add)," -> %s",info.sym_link);

   append_type_maybe:
      if(lsopt && lsopt->append_type)
      {
	 if(info.is_directory)
	    strcat(line_add,"/");
	 if(info.is_sym_link && !info.sym_link)
	    strcat(line_add,"@");
      }
      strcat(line_add,"\n");

      if(!all_links->FindByName(link_target))
      {
	 list->Put(line_add);
	 FileInfo *fi=new FileInfo;
	 fi->SetName(link_target);
	 all_links->Add(fi);
      }
   }

   if(set && link_target[0]!='/' && link_target[0]!='~')
   {
      char *slash=strchr(link_target,'/');
      if(slash)
      {
	 *slash=0;
	 info.is_directory=true;
      }

      FileInfo *fi=new FileInfo;
      fi->SetName(link_target);
      if(info.sym_link)
	 fi->SetSymlink(info.sym_link);
      else
	 fi->SetType(info.is_directory ? fi->DIRECTORY : fi->NORMAL);
      if(info.nlink>0)
	 fi->SetNlink(info.nlink);
      if(info.user[0])
	 fi->SetUser(info.user);
      if(info.group[0])
	 fi->SetGroup(info.group);
      if(info.size!=-1)
	 fi->SetSize(info.size);
      if(info.perms[0])
      {
	 int m=parse_perms(info.perms);
	 if(m>=0)
	    fi->SetMode(m);
      }
      if(info.date_prec!=-1 && info.date!=NO_DATE)
	 fi->SetDate(info.date,info.date_prec);

      set->Add(fi);
   }

   return skip_len;
}


// HttpDirList implementation
#define super DirList

int HttpDirList::Do()
{
   if(done)
      return STALL;

   if(buf->Eof())
   {
      done=true;
      return MOVED;
   }

   if(!ubuf)
   {
      curr=args->getnext();
      if(!curr)
      {
	 buf->PutEOF();
	 done=true;
	 return MOVED;
      }
      if(args->count()>2)
      {
	 if(args->getindex()>1)
	    buf->Put("\n");
	 buf->Put(curr);
	 buf->Put(":\n");
      }

      const char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,curr,mode,
				    &cache_buffer,&cache_buffer_size))
      {
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open(curr,mode);
	 session->UseCache(use_cache);
	 ubuf=new IOBufferFileAccess(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
      if(curr_url)
	 delete curr_url;
      curr_url=new ParsedURL(session->GetFileURL(curr));
      if(mode==FA::RETRIEVE)
      {
	 // strip file name, directory remains.
	 char *slash=strrchr(curr_url->path,'/');
	 if(slash && slash>curr_url->path)
	    *slash=0;
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      LsCache::Add(session,curr,mode,ubuf);

      Delete(ubuf);
      ubuf=0;
      return MOVED;
   }

   int m=STALL;

   int n=parse_html(b,len,ubuf->Eof(),buf,0,&all_links,curr_url,&base_href,&ls_options, color);
   if(n>0)
   {
      ubuf->Skip(n);
      m=MOVED;
   }

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

HttpDirList::HttpDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   mode=FA::LONG_LIST;
   args->rewind();
   int opt;
   while((opt=args->getopt("faCFl"))!=EOF)
   {
      switch(opt)
      {
      case('f'):
	 mode=FA::RETRIEVE;
	 break;
      case('a'):
	 ls_options.show_all=true;
	 break;
      case('C'):
	 ls_options.multi_column=true;
	 break;
      case('F'):
	 ls_options.append_type=true;
	 break;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);	// remove options.
   if(args->count()<2)
      args->Append("");
   args->rewind();
   curr=0;
   curr_url=0;
   base_href=0;
}

HttpDirList::~HttpDirList()
{
   Delete(ubuf);
   if(curr_url)
      delete curr_url;
   xfree(base_href);
}

const char *HttpDirList::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
}

void HttpDirList::Suspend()
{
   if(ubuf)
      ubuf->Suspend();
   super::Suspend();
}
void HttpDirList::Resume()
{
   super::Resume();
   if(ubuf)
      ubuf->Resume();
}


// HttpListInfo implementation
FileSet *HttpListInfo::Parse(const char *b,int len)
{
   if(mode==FA::MP_LIST)
   {
      FileSet *fs=ParseProps(b,len,session->GetCwd());
      if(!fs)
	 mode=FA::LONG_LIST;
      return fs;
   }
   return session->ParseLongList(b,len);
}

FileSet *Http::ParseLongList(const char *b,int len,int *err)
{
   if(err)
      *err=0;

   FileSet *set=new FileSet;
   ParsedURL prefix(GetConnectURL());
   char *base_href=0;
   for(;;)
   {
      int n=parse_html(b,len,true,0,set,0,&prefix,&base_href);
      if(n==0)
	 break;
      b+=n;
      len-=n;
   }
   xfree(base_href);
   return set;
}
