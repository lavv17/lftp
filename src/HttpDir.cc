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

static bool find_value(const char *scan,const char *more,const char *name,xstring& store)
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

      if(match)
	 store.set("");
      while(scan<more && (quote ? *scan!=quote : !is_ascii_space(*scan)))
      {
	 if(match)
	    store.append(*scan);
	 scan++;
      }
      if(match)
	 return true;
      if(scan>=more)
	 return false;
      if(quote)
	 scan++;  // skip closing quotation mark.
   }
   return false;
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
static void decode_amps(xstring& s)
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

   for(const char *a=s; a; a=strchr(a,'&'))
   {
      for(scan=table; scan->ch; scan++)
      {
	 int len=strlen(scan->str);
	 if(!strncmp(a,scan->str,len))
	 {
	    s.set_substr(a-s,len,&scan->ch,1);
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
   xstring_c sym_link;
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
   sym_link.set(0);
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


#undef debug
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
static bool try_apache_listing_iso(file_info &info,const char *str)
{
   info.clear();

   // apache listing with ISO time: YYYY-MM-DD hh:mm size
   int n=sscanf(str,"%4d-%2d-%2d %2d:%2d %30s",
	       &info.year,&info.month,&info.day,
	       &info.hour,&info.minute,info.size_str);
   if(n==6 && (info.size_str[0]=='-' || is_ascii_digit(info.size_str[0])))
   {
      debug("apache listing matched (ISO time)");
      info.month--;
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
	 snprintf(info.size_str,sizeof(info.size_str),"%lld",info.size);
      else
      {
	 snprintf(info.size_str,sizeof(info.size_str),"%lld%s",info.size,size_unit);
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
   const char *more,const char *more1,xstring& info_string)
{
   info.clear();

   // Apache Unix-like listing (from apache proxy):
   //   Perms Nlnk user [group] size Mon DD (YYYY or hh:mm)
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
   if(n>=7 && -1!=parse_perms(info.perms+1)
   && -1!=(info.month=parse_month(info.month_name))
   && -1!=parse_year_or_time(year_or_time,&info.year,&info.hour,&info.minute))
   {
      snprintf(info.size_str,sizeof(info.size_str),"%lld",info.size);
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
	    info.sym_link.set(str+4);
      }
      info_string.nset(buf,consumed);
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
	 const char *old_size_str=alloca_strdup(info.size_str);
	 snprintf(info.size_str,sizeof(info.size_str),"%s%s",old_size_str,size_mod);
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
      char *sym_link=ptr+13;
      ptr=strchr(sym_link,'"');
      if(!ptr)
	 info.sym_link.unset();
      else
      {
	 *ptr=0;
	 info.sym_link.set(url::decode(sym_link));
      }
   }
   debug("squid ftp listing matched");
   return true;
}

static bool try_wwwoffle_ftp(file_info &info,const char *buf,
   const char *ext,xstring& info_string)
{
   info.clear();

   //   Perms Nlnk user [group] size Mon DD (YYYY or hh:mm)
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
   if(n>=7 && -1!=parse_perms(info.perms+1)
   && -1!=(info.month=parse_month(info.month_name))
   && -1!=parse_year_or_time(year_or_time,&info.year,&info.hour,&info.minute))
   {
      snprintf(info.size_str,sizeof(info.size_str),"%lld",info.size);
      if(info.perms[0]=='d')
	 info.is_directory=true;
      else if(info.perms[0]=='l')
      {
	 info.is_sym_link=true;
	 const char *p=strstr(ext,"-&gt; ");
	 if(p)
	    info.sym_link.set(p+6);
      }
      info_string.nset(buf,consumed);
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
      snprintf(info.size_str,sizeof(info.size_str),"%lld",info.size);
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
	      return false;
	  }
      }
   }
   return status;
}

// 2004-Oct-19 02:10:26	0.2K	application/octet-stream
static bool try_lighttpd_listing(file_info &info,char *str_with_tags)
{
   info.clear();

   if(str_with_tags[0]=='/') {
      info.is_directory=true;
      str_with_tags++;
   }

   const char *next=strstr(str_with_tags,"\"m\">");
   if(!next)
      return false;
   next+=4;
   const char *end=strchr(next,'<');
   if(!end)
      return false;
   xstring datetime(next,end-next);

   next=strstr(end,"\"s\">");
   if(!next)
      return false;
   next+=4;
   end=strchr(next,'<');
   if(!end)
      return false;
   xstring size(next,end-next);

   int n=sscanf(datetime,"%4d-%3s-%2d %2d:%2d:%2d",
	       &info.year,info.month_name,&info.day,
	       &info.hour,&info.minute,&info.second);
   if(n!=6)
      return false;

   if(is_ascii_digit(size[0])) {
      strncpy(info.size_str,size,sizeof(info.size_str));
      info.size_str[sizeof(info.size_str)-1]=0;
   }

   debug("lighttpd listing matched");

   return true;
}

// this procedure is highly inefficient in some cases,
// esp. when it has to return for more data many times.
static int parse_html(const char *buf,int buf_len,bool eof,const Ref<Buffer>& list,
      FileSet *set,FileSet *all_links,const ParsedURL *prefix,xstring_c *base_href,
      LsOptions *lsopt=0, int color = 0)
{
   const char *end=buf+buf_len;
   const char *less=find_char(buf,buf_len,'<');
   int eol_len=0;
   int skip_len=0;
   const char *eol;

   eol=find_eol(buf,buf_len,eof,&eol_len);
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
	       return buf_len;
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
	 return buf_len;
      return 0;
   }
   // we have found a tag
   int tag_len=more-buf+1;
   if(more-less<3)
      return tag_len;   // too small

   if(less[1]=='/' || less[1]=='!')
      return tag_len;

   xstring link_target;

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
	 { "source", "src" },
	 { "embed", "src" },
	 { "bgsound", "src" },
	 { "area", "href" },
	 { "img", "lowsrc" },
	 { "input", "src" },
	 { "layer", "src" },
	 { "table", "background" },
	 { "th", "background" },
	 { "td", "background" },
	 { "link", "href" },
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

   const char *prefix_proto=0;
   if(prefix)
      prefix_proto=prefix->proto;
   if(!xstrcmp(prefix_proto,"hftp"))
      prefix_proto++;
   bool hftp=!xstrcmp(prefix_proto,"ftp");

   // ok, found the target.

   decode_amps(link_target);  // decode all &amp; and similar

   // inherit the protocol if omitted
   if(link_target.begins_with("//") && prefix && prefix->proto) {
      xstring& new_link=xstring::get_tmp("")
	 .append_url_encoded(prefix->proto,URL_UNSAFE,0)
	 .append(':').append(link_target);
      link_target.swap(new_link);
   }

   if(hftp)
   {
      // workaround proxy bugs.
      const char *t=strstr(link_target,";type=");
      if(t && t[6] && t[7]=='/' && t[8]==0)
	 link_target.truncate(t-link_target);
      const char *p=link_target+url::path_index(link_target);
      if(p[0]=='/' && p[1]=='/')
	 link_target.set_substr(p-link_target+1,1,"%2F");
   }

   Log::global->Format(10,"Found tag %s, link_target=%s\n",tag_scan->tag,link_target.get());

   if(!strcasecmp(tag_scan->tag,"base"))
   {
      if(base_href)
      {
	 base_href->set(link_target);
	 Log::global->Format(10,"Using base href=%s\n",base_href->get());
      }
      return tag_len;
   }
   if(!strcasecmp(tag_scan->tag,"meta"))
   {
      // skip 0; URL=
      link_target.rtrim();
      const char *scan=link_target;
      while(*scan && is_ascii_digit(*scan))
	 scan++;
      if(*scan!=';')
	 return tag_len;
      scan++;
      while(*scan && is_ascii_space(*scan))
	 scan++;
      if(strncasecmp(scan,"URL=",4))
	 return tag_len;
      scan+=4;
      int len=link_target.length()-(scan-link_target);
      if(link_target[0]=='\'')
      {
	 // FIXME: maybe a more complex value parser is required.
	 scan++;
	 len--;
	 if(len>0 && scan[len-1]=='\'')
	    len--;
      }
      link_target.nset(scan,len);
      Log::global->Format(10,"Extracted `%s' from META tag\n",link_target.get());
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
   link_target.truncate_at('#'); // strip the anchor
   if(link_target.length()==0)
      return tag_len;	// no target ?

   // netscape internal icons
   if(icon && !strncasecmp(link_target,"internal-gopher",15))
      return tag_len;

   if(link_target[0]=='/' && link_target[1]=='~')
      link_target.set_substr(0,1,0,0);

   bool base_href_applied=false;

parse_url_again:
   ParsedURL link_url(link_target,/*proto_required=*/true);
   if(link_url.proto)
   {
      if(!prefix)
	 return tag_len;	// no way

      if(xstrcmp(link_url.proto,prefix_proto)
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
	 const char *base_end=strrchr(*base_href,'/');
	 if(base_end)
	 {
	    link_target.set_substr(0,0,*base_href,(base_end+1-*base_href));
	    base_href_applied=true;
	    goto parse_url_again;
	 }
      }
   }

   // ok, it is good relative link
   if(link_url.path==0)
      link_target.set("/");
   else
      link_target.set(link_url.path);

   if(link_target[0]=='/' && link_target[1]=='/' && hftp)
   {
      // workaround for apache proxy.
      link_target.set_substr(0,1,0,0);
   }

   file_info info;
   info.is_directory=(link_target.last_char()=='/');
   if(link_target.length()>1)
      link_target.chomp('/');

   FileAccess::Path::Optimize(link_target,(link_target[0]=='/' && link_target[1]=='~'));

   if(prefix)
   {
      const char *p_path_c=prefix->path;
      if(p_path_c==0)
	 p_path_c="~";
      char *p_path=alloca_strdup(p_path_c);
      int p_len=strlen(p_path);
      if(p_len>1 && p_path[p_len-1]=='/')
	 p_path[--p_len]=0;
      if(p_len==1 && p_path[0]=='/' && link_target[0]=='/')
      {
	 if(link_target.length()>1)
	 {
	    // strip leading slash
	    link_target.set_substr(0,1,0,0);
	 }
      }
      else if(p_len>0 && !strncmp(link_target,p_path,p_len))
      {
	 if(link_target[p_len]=='/')
	    link_target.set_substr(0,p_len+1,0,0);
	 else if(link_target[p_len]==0)
	    link_target.set(".");
	 if(link_target[0]=='.' && link_target[1]=='/')
	    link_target.set_substr(0,2,0,0);
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
	       link_target.set("..");
	 }
      }
   }

   const char *type=strstr(link_target,";type=");
   if(type && type[6] && !type[7])
   {
      if(!all_links || all_links->FindByName(xstring::get_tmp(link_target,type-link_target)))
	 return tag_len;
   }

   if(link_target.length()==0)
   {
      link_target.set(".");
      info.is_directory=true;
   }

   bool show_in_list=true;
   if(icon && (link_target[0]=='/' || link_target[0]=='~'))
      show_in_list=false;  // makes apache listings look better.

   skip_len=tag_len;

   // try to find file info
   {
      const char *more1;
      xstring str, str_with_tags;
      char  *str2;
      xstring line_add;
      xstring info_string;
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
      str.nset(more1 + 1, eol - more1 - 1);
      str_with_tags.nset(more1 + 1, eol - more1 - 1);
      str.set_length(remove_tags(str.get_non_const()));

      if(try_apache_listing(info,str)		&& info.validate()) goto got_info;
      if(try_apache_listing_iso(info,str)	&& info.validate()) goto got_info;
      if(try_apache_listing_unusual(info,str)	&& info.validate()) goto got_info;
      if(try_netscape_proxy(info,str)		&& info.validate()) goto got_info;
      if(try_squid_eplf(info,str) && info.validate())
      {
	 // skip rest of line, because there may be another href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      if(try_lighttpd_listing(info,str_with_tags.get_non_const()) && info.validate())
	 goto got_info;
      if(try_mini_proxy(info,str)		&& info.validate()) goto got_info;
      if(try_apache_unixlike(info,str,more,more1,info_string)
      && info.validate())
	 goto got_info;
      if(try_roxen(info,str)			&& info.validate()) goto got_info;
      if(try_squid_ftp(info,str,str_with_tags.get_non_const()) && info.validate())
      {
	 // skip rest of line, because there may be href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      // wwwoffle
      str2=string_alloca(less-buf+1);
      memcpy(str2,buf,less-buf);
      str2[less-buf]=0;
      if(try_wwwoffle_ftp(info,str2,str,info_string)
      && info.validate())
      {
	 // skip rest of line, because there may be href to link target.
	 skip_len=eol-buf+eol_len;
	 goto got_info;
      }
      if(try_csm_proxy(info,str) && info.validate()) goto got_info;

   add_file_no_info:
      if(!list || !show_in_list)
	 goto info_done;
      line_add.vset(info.is_directory?"drwxr-xr-x":"-rw-r--r--","  --  ",link_target.get(),NULL);
      goto append_type_maybe;

   got_info:
      if(info.month==-1)
	 info.month=parse_month(info.month_name);
      if(info.month>=0)
      {
	 snprintf(info.month_name,sizeof(info.month_name),"%02d",info.month+1);
	 if(info.year==-1)
	    info.year=guess_year(info.month,info.day,info.hour,info.minute);
      }
      if(info.year!=-1 && info.month!=-1 && info.day!=-1)
      {
	 struct tm tm;
	 memset(&tm,0,sizeof(tm));
	 tm.tm_year=info.year-1900;
	 tm.tm_mon=info.month;
	 tm.tm_mday=info.day;
	 tm.tm_hour=12;
	 info.date_prec=43200;
	 if(info.hour!=-1 && info.minute!=-1)
	 {
	    tm.tm_hour=info.hour;
	    tm.tm_min=info.minute;
	    tm.tm_sec=30;
	    info.date_prec=30;
	    if(info.second!=-1)
	    {
	       tm.tm_sec=info.second;
	       info.date_prec=0;
	    }
	 }
	 info.date=mktime_from_utc(&tm);
      }
      if(info.size==-1)
      {
	 if(strspn(info.size_str,"0123456789")==strlen(info.size_str))
	 {
	    long long size_ll=0;
	    if(sscanf(info.size_str,"%lld",&size_ll)!=1)
	       size_ll=0;
	    info.size=size_ll;
	 }
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
      if(!list || !show_in_list)
	 goto info_done;

      if(info_string)
      {
	 line_add.vset(info_string.get()," ",link_target.get(),NULL);
	 goto append_symlink_maybe;
      }

      line_add.setf("%s  %11s  %04d-%s-%02d",
	 info.perms,info.size_str,info.year,info.month_name,info.day);

      if (info.hour >= 0 || info.minute >= 0) {
	  if (info.hour >= 0) {
	      line_add.appendf(" %02d:",info.hour);
	  } else {
	      line_add.append(" --:");
	  }

	  if (info.minute >= 0) {
	      line_add.appendf("%02d", info.minute);
	  } else {
	      line_add.append("--");
	  }
      } else {
	  // neither hour nor minute are given
	  line_add.append("      ");
      }

      line_add.append("  ");

      type = FileInfo::NORMAL;

       if(info.is_directory)
	   type = FileInfo::DIRECTORY;
       else if(info.is_sym_link && !info.sym_link)
	   type = FileInfo::SYMLINK;

      if (color && FileInfo::NORMAL != type && all_links && !all_links->FindByName(link_target)) {
	  list->Put(line_add);
	  DirColors::GetInstance()->PutColored(list, link_target, type);
	  line_add.truncate(0);	 // reset
      } else {
	  line_add.append(link_target);
      }

   append_symlink_maybe:
      if(info.sym_link)
	 line_add.vappend(" -> ",info.sym_link.get(),NULL);

   append_type_maybe:
      if(lsopt && lsopt->append_type)
      {
	 if(info.is_directory)
	    line_add.append('/');
	 if(info.is_sym_link && !info.sym_link)
	    line_add.append('@');
      }
      line_add.append('\n');

      if(!all_links->FindByName(link_target))
      {
	 list->Put(line_add);
	 FileInfo *fi=new FileInfo(link_target);
	 all_links->Add(fi);
      }
   }
info_done:
   if(set && link_target[0]!='/' && link_target[0]!='~')
   {
      const char *slash=strchr(link_target,'/');
      if(slash)
      {
	 link_target.truncate(slash-link_target);
	 info.is_directory=true;
      }

      FileInfo *fi=new FileInfo(link_target);
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
	 int m=parse_perms(info.perms+1);
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
      curr_url=new ParsedURL(session->GetFileURL(curr));
      if(mode==FA::RETRIEVE)
      {
	 // strip file name, directory remains.
	 const char *slash=strrchr(curr_url->path,'/');
	 if(slash && slash>curr_url->path)
	    curr_url->path.truncate(slash-curr_url->path);
      }

   retry:
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      int err;
      if(use_cache && FileAccess::cache->Find(session,curr,mode,&err,
				    &cache_buffer,&cache_buffer_size))
      {
	 if(err)
	 {
	    if(mode==FA::MP_LIST)
	    {
	       mode=FA::LONG_LIST;
	       goto retry;
	    }
	    SetErrorCached(cache_buffer);
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 if(mode==FA::MP_LIST && !*curr && session->GetCwd().is_file)
	 {
	    mode=FA::LONG_LIST;
	    goto retry;
	 }
	 session->Open(curr,mode);
	 session->UseCache(use_cache);
	 ubuf=new IOBufferFileAccess(session);
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      FileAccess::cache->Add(session,curr,mode,FA::OK,ubuf);
      ubuf=0;
      return MOVED;
   }

   int m=STALL;

reparse:
   if(mode!=FA::MP_LIST || parse_as_html)
   {
      int n=parse_html(b,len,ubuf->Eof(),buf,0,&all_links,curr_url,&base_href,&ls_options, color);
      if(n>0)
      {
	 ubuf->Skip(n);
	 m=MOVED;
      }
   }
   else
   {
      ParsePropsFormat(b,len,ubuf->Eof());
      if(parse_as_html)
	 goto reparse;
      ubuf->Skip(len);
   }

   if(ubuf->Error())
   {
      FileAccess::cache->Add(session,curr,mode,session->GetErrorCode(),ubuf);
      if(mode==FA::MP_LIST)
      {
	 mode=FA::LONG_LIST;
	 ubuf=0;
	 goto retry;
      }
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

HttpDirList::HttpDirList(FileAccess *s,ArgV *a)
   : DirList(s,a)
{
   mode=FA::MP_LIST;
   parse_as_html=false;
#if USE_EXPAT
   xml_p=0;
   xml_ctx=0;
#endif
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
}

HttpDirList::~HttpDirList()
{
   ParsePropsFormat(0,0,true);
}

const char *HttpDirList::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
      return xstring::format(_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
   return "";
}

void HttpDirList::SuspendInternal()
{
   super::SuspendInternal();
   if(ubuf)
      ubuf->SuspendSlave();
}
void HttpDirList::ResumeInternal()
{
   if(ubuf)
      ubuf->ResumeSlave();
   super::ResumeInternal();
}
#undef super

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

FileSet *Http::ParseLongList(const char *b,int len,int *err) const
{
   if(err)
      *err=0;

   FileSet *set=0;

   if(len>5 && !strncmp(b,"<?xml",5))
      set=HttpListInfo::ParseProps(b,len,GetCwd());

   if(!set)
      set=new FileSet;
   if(set->count()>0)
      return set;

   ParsedURL prefix(GetConnectURL());
   xstring_c base_href;
   for(;;)
   {
       int clen = len;
       if(clen > 1000)clen = 1000;
      int n=parse_html(b,clen,true,Ref<Buffer>::null,set,0,&prefix,&base_href);
      if(n==0)
	 break;
      b+=n;
      len-=n;
   }
   return set;
}
