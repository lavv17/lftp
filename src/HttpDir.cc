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

static const char *find_char(const char *buf,int len,char ch)
{
   return (const char *)memchr(buf,ch,len);
}

static bool token_eq(const char *buf,int len,const char *token)
{
   int token_len=strlen(token);
   if(len<token_len)
      return false;
   return !strncasecmp(buf,token,token_len)
	    && (token_len==len || !is_ascii_alnum(buf[token_len]));
}

static int parse_month(const char *m)
{
   static const char *months[]={
      "Jan","Feb","Mar","Apr","May","Jun",
      "Jul","Aug","Sep","Oct","Nov","Dec",0
   };
   for(int i=0; months[i]; i++)
      if(!strcasecmp(months[i],m))
	 return(i%12);
   return -1;
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

static int parse_html(const char *buf,int len,bool eof,Buffer *list,
      FileSet *set,FileSet *all_links,ParsedURL *prefix,char **base_href,
      LsOptions *lsopt=0)
{
   const char *end=buf+len;
   const char *less=find_char(buf,len,'<');
   if(less==0)
      return len;
   // FIXME: a > sign can be inside quoted value. (?)
   const char *more=find_char(less+1,end-less-1,'>');
   if(more==0)
   {
      if(eof)
	 return len;
      return less-buf;
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

   // ok, found the target.

   if(!strcasecmp(tag_scan->tag,"base"))
   {
      if(base_href)
      {
	 xfree(*base_href);
	 *base_href=xstrdup(link_target);
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

      if(xstrcmp(link_url.proto,prefix->proto)
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

   int link_len=strlen(link_target);
   bool is_directory=(link_len>0 && link_target[link_len-1]=='/');
   if(is_directory && link_len>1)
      link_target[--link_len]=0;

   if(prefix && prefix->path)
   {
      const char *p_path=prefix->path;
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
   if(icon && link_target[0]=='/')
      show_in_list=false;  // makes apache listings look better.

   int skip_len=tag_len;
   char *sym_link=0;
   bool is_sym_link=false;

   if(list && show_in_list)
   {
      int year=-1,month=0,day=0,hour=0,minute=0;
      char month_name[32]="";
      char size_str[32]="";
      const char *more1;
      char *str;
      int n;
      char *line_add=(char*)alloca(link_len+128+2*1024);
      bool data_available=false;

      if(!a_href)
	 goto add_file;	// only <a href> tags can have useful info.

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
	       goto add_file;
	    if(end-more>2*1024) // too long a-href
	       goto add_file;
	    return less-buf;  // no full a-href yet
	 }
	 if(!strncasecmp(more1-3,"</a",3))
	    break;
      }
      const char *eol;	// get a whole line in buffer if possible.
      eol=find_char(more1+1,end-more1-1,'\n');
      if(!eol)
      {
	 if(!eof && end-more<=2*1024)
	    return less-buf;  // no full line yet
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

      // usual apache listing: DD-Mon-YYYY hh:mm size
      n=sscanf(str,"%2d-%3s-%4d %2d:%2d %30s",
		    &day,month_name,&year,&hour,&minute,size_str);
      if(n==6)
	 goto got_info;

      hour=0;
      minute=0;
      // unusual apache listing: size DD-Mon-YYYY
      n=sscanf(str,"%30s %2d-%3s-%4d",size_str,&day,month_name,&year);
      if(n==4)
	 goto got_info;

      char size_unit[7];
      long size;
      char week_day[4];
      int second;
      // Netscape-Proxy 2.53
      if(9==sscanf(str,"%ld %6s %3s %3s %d %2d:%2d:%2d %4d",&size,size_unit,
	       week_day,month_name,&day,&hour,&minute,&second,&year))
      {
	 if(!strcasecmp(size_unit,"bytes")
	 || !strcasecmp(size_unit,"byte"))
	    sprintf(size_str,"%ld",size);
	 else
	    sprintf(size_str,"%ld%s",size,size_unit);
	 goto got_info;
      }
      if(7==sscanf(str,"%3s %3s %d %2d:%2d:%2d %4d",
	       week_day,month_name,&day,&hour,&minute,&second,&year))
      {
	 strcpy(size_str,"-");
	 if(!is_directory)
	    is_sym_link=true;
	 goto got_info;
      }

      strcpy(size_str,"-");
      char year_or_time[6];
      // squid's ftp listing: Mon DD (YYYY or hh:mm) [size]
      n=sscanf(str,"%3s %2d %5s %30s",month_name,&day,year_or_time,size_str);
      if(n<3)
	 goto add_file;
      if(!is_ascii_digit(size_str[0]))
	 strcpy(size_str,"-");
      if(year_or_time[2]==':')
      {
	 if(2!=sscanf(year_or_time,"%2d:%2d",&hour,&minute))
	    goto add_file;
	 year=-1;
      }
      else
      {
	 if(1!=sscanf(year_or_time,"%d",&year))
	    goto add_file;
      }
      // skip rest of line, because there may be href to link target.
      skip_len=eol-buf+1;

      char *ptr;
      ptr=strstr(str," -> <A HREF=\"");
      if(ptr)
      {
	 is_sym_link=true;
	 sym_link=ptr+13;
	 ptr=strchr(sym_link,'"');
	 if(!ptr)
	    sym_link=0;
	 else
	 {
	    *ptr=0;
	    url::decode_string(sym_link);
	 }
      }

   got_info:
      if(year!=-1)
      {
	 // server's y2000 problem :)
	 if(year<37)
	    year+=2000;
	 else if(year<100)
	    year+=1900;
      }

      data_available=true;

   add_file:
      if(data_available)
      {
	 month=parse_month(month_name);
	 if(month>=0)
	 {
	    sprintf(month_name,"%02d",month+1);
	    if(year==-1)
	    {
	       time_t curr=time(0);
	       struct tm &now=*localtime(&curr);
	       year=now.tm_year+1900;
	       if(month*64+day>now.tm_mon*64+now.tm_mday)
		  year--;
	    }
	 }
	 sprintf(line_add,"%s  %10s  %04d-%s-%02d %02d:%02d  %s",
	    is_directory?"drwxr-xr-x":(is_sym_link?"lrwxrwxrwx":"-rw-r--r--"),
	    size_str,year,month_name,day,hour,minute,link_target);
	 if(sym_link)
	    sprintf(line_add+strlen(line_add)," -> %s",sym_link);
      }
      else
      {
	 sprintf(line_add,"%s    %s",
	    is_directory?"drwxr-xr-x":"-rw-r--r--",link_target);
      }
      if(lsopt && lsopt->append_type)
      {
	 if(is_directory)
	    strcat(line_add,"/");
	 if(is_sym_link && !sym_link)
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

   if(set && link_target[0]!='/')
   {
      char *slash=strchr(link_target,'/');
      if(slash)
      {
	 *slash=0;
	 is_directory=true;
      }

      FileInfo *fi=new FileInfo;
      fi->SetName(link_target);
      if(sym_link)
	 fi->SetSymlink(sym_link);
      else
	 fi->SetType(is_directory ? fi->DIRECTORY : fi->NORMAL);

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

      char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,curr,mode,
				    &cache_buffer,&cache_buffer_size))
      {
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(curr,mode);
	 session->UseCache(use_cache);
	 ubuf=new FileInputBuffer(session);
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

      delete ubuf;
      ubuf=0;
      return MOVED;
   }

   int m=STALL;

   int n=parse_html(b,len,ubuf->Eof(),buf,0,&all_links,curr_url,&base_href,&ls_options);
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
   delete ubuf;
   if(curr_url)
      delete curr_url;
   xfree(base_href);
}

const char *HttpDirList::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%ld) [%s]"),
		     session->GetPos(),session->CurrentStatus());
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



// HttpGlob implementation
#undef super
#define super Glob

HttpGlob::HttpGlob(FileAccess *s,const char *n_pattern)
   : Glob(n_pattern)
{
   curr_dir=0;
   dir_list=0;
   dir_index=0;
   updir_glob=0;
   ubuf=0;

   session=s;
   dir=xstrdup(pattern);
   char *slash=strrchr(dir,'/');
   if(!slash)
      dir[0]=0;	  // current directory
   else if(slash>dir)
      *slash=0;	  // non-root directory
   else
      dir[1]=0;	  // root directory

   if(pattern[0] && !HasWildcards(pattern))
   {
      // no need to glob, just unquote
      char *u=alloca_strdup(pattern);
      UnquoteWildcards(u);
      add(u);
      done=true;
      return;
   }

   if(dir[0])
   {
      updir_glob=new HttpGlob(session,dir);
   }
}

HttpGlob::~HttpGlob()
{
   if(updir_glob)
      delete updir_glob;
   if(ubuf)
      delete ubuf;
   xfree(dir);
}

int HttpGlob::Do()
{
   int   m=STALL;

   if(done)
      return m;

   if(updir_glob && !dir_list)
   {
      if(updir_glob->Error())
      {
	 SetError(updir_glob->ErrorText());
	 return MOVED;
      }
      if(!updir_glob->Done())
	 return m;
      dir_list=updir_glob->GetResult();
      dir_index=0;
      m=MOVED;
   }

   if(!ubuf)
   {
      curr_dir=0;
      if(dir_list)
	 curr_dir=dir_list[dir_index];
      else if(dir_index==0)
	 curr_dir=dir;
      if(curr_dir==0) // all done
      {
	 done=true;
	 return MOVED;
      }

      char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,curr_dir,FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size))
      {
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(curr_dir,FA::LONG_LIST);
	 session->UseCache(use_cache);
	 ubuf=new FileInputBuffer(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
      m=MOVED;
   }

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      delete ubuf;
      ubuf=0;
      return MOVED;
   }

   if(!ubuf->Eof())
      return m;

   // now we have all the index in ubuf; parse it.
   const char *b;
   int len;
   ubuf->Get(&b,&len);

   FileSet set;
   ParsedURL prefix(session->GetFileURL(curr_dir));
   char *base_href=0;
   for(;;)
   {
      int n=parse_html(b,len,true,0,&set,0,&prefix,&base_href);
      if(n==0)
	 break;
      b+=n;
      len-=n;
   }
   xfree(base_href);

   LsCache::Add(session,curr_dir,FA::LONG_LIST,ubuf);

   delete ubuf;
   ubuf=0;

   set.rewind();
   for(FileInfo *f=set.curr(); f; f=set.next())
      add(dir_file(curr_dir,f->name));

   dir_index++;
   m=MOVED;

   return m;
}

const char *HttpGlob::Status()
{
   if(updir_glob && !dir_list)
      return updir_glob->Status();

   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%ld) [%s]"),
		     session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
}


#undef super

// HttpListInfo implementation
#define super ListInfo

int HttpListInfo::Do()
{
#define need_size (need&FileInfo::SIZE)
#define need_time (need&FileInfo::DATE)

   FA::fileinfo *cur;
   FileInfo *file;
   int res;
   int m=STALL;

   if(done)
      return m;

   if(!ubuf && !get_info)
   {
      char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,"",FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size))
      {
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open("",FA::LONG_LIST);
	 session->UseCache(use_cache);
	 ubuf=new FileInputBuffer(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
      m=MOVED;
   }
   if(ubuf)
   {
      if(ubuf->Error())
      {
	 SetError(ubuf->ErrorText());
	 delete ubuf;
	 ubuf=0;
	 return MOVED;
      }

      if(!ubuf->Eof())
	 return m;

      // now we have all the index in ubuf; parse it.
      const char *b;
      int len;
      ubuf->Get(&b,&len);

      result=new FileSet;
      ParsedURL prefix(session->GetConnectURL());
      char *base_href=0;
      for(;;)
      {
	 int n=parse_html(b,len,true,0,result,0,&prefix,&base_href);
	 if(n==0)
	    break;
	 b+=n;
	 len-=n;
      }
      xfree(base_href);

      LsCache::Add(session,"",FA::LONG_LIST,ubuf);

      delete ubuf;
      ubuf=0;
      m=MOVED;

      get_info_cnt=result->get_fnum();
      if(get_info_cnt==0)
      {
	 done=true;
	 return m;
      }

      get_info=(FA::fileinfo*)xmalloc(sizeof(*get_info)*get_info_cnt);
      cur=get_info;

      get_info_cnt=0;
      result->rewind();
      for(file=result->curr(); file!=0; file=result->next())
      {
	 cur->get_size = !(file->defined & file->SIZE) && need_size;
	 cur->get_time = !(file->defined & file->DATE) && need_time;
	 cur->file=0;

	 if(file->defined & file->TYPE)
	 {
	    if(file->filetype==file->SYMLINK && follow_symlinks)
	    {
	       file->filetype=file->NORMAL;
	       file->defined &= ~(file->SIZE|file->SYMLINK_DEF|file->MODE|file->DATE_UNPREC);
	       cur->get_size=true;
	       cur->get_time=true;
	    }

	    if(file->filetype==file->SYMLINK)
	    {
	       // don't need these for symlinks
	       cur->get_size=false;
	       cur->get_time=false;
	    }
	    else if(file->filetype==file->DIRECTORY)
	    {
	       continue; // FIXME: directories need slash in GetInfoArray.
#if 0
	       // don't need size for directories
	       cur->get_size=false;
	       // and need slash
#endif
	    }
	 }

	 if(cur->get_size || cur->get_time)
	 {
	    cur->file=file->name;
	    if(!cur->get_size)
	       cur->size=-1;
	    if(!cur->get_time)
	       cur->time=(time_t)-1;
	    cur++;
	    get_info_cnt++;
	 }
      }
      if(get_info_cnt==0)
      {
	 xfree(get_info);
	 get_info=0;
	 done=true;
	 return m;
      }
      session->GetInfoArray(get_info,get_info_cnt);
   }
   if(get_info)
   {
      res=session->Done();
      if(res==FA::DO_AGAIN)
	 return m;
      if(res==FA::IN_PROGRESS)
	 return m;
      assert(res==FA::OK);
      session->Close();

      cur=get_info;
      for(cur=get_info; get_info_cnt-->0; cur++)
      {
	 if(cur->time!=(time_t)-1)
	    result->SetDate(cur->file,cur->time);
	 if(cur->size!=-1)
	    result->SetSize(cur->file,cur->size);
      }
      xfree(get_info);
      get_info=0;
      done=true;
      m=MOVED;
   }
   return m;
}

HttpListInfo::HttpListInfo(Http *s)
{
   session=s;

   get_info=0;
   get_info_cnt=0;

   ubuf=0;
}

HttpListInfo::~HttpListInfo()
{
   session->Close();
   xfree(get_info);
   if(ubuf)
      delete ubuf;
}

const char *HttpListInfo::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting directory contents (%ld)"),session->GetPos());
      return s;
   }
   if(get_info)
   {
      sprintf(s,_("Getting files information (%d%%)"),session->InfoArrayPercentDone());
      return s;
   }
   return "";
}
