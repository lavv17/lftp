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


static int parse_html(const char *buf,int len,bool eof,Buffer *list,
      FileSet *set,FileSet *all_links,ParsedURL *prefix)
{
   const char *end=buf+len;
   const char *less=find_char(buf,len,'<');
   if(less==0)
      return len;
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
   if(!token_eq(less+1,end-less-1,"a"))
      return tag_len;   // not interesting
   const char *scan=less+2;
   while(is_ascii_space(*scan))
      scan++;
   assert(scan<end);
   if(!token_eq(scan,end-scan,"href"))
      return tag_len;   // not interesting
   scan+=4;
   while(is_ascii_space(*scan))
      scan++;
   assert(scan<end);
   if(*scan!='=')
      return tag_len;	// syntax error
   scan++;
   char quote=0;
   if(*scan=='"' || *scan=='\'')
      quote=*scan++;

   char *link_target=(char*)alloca(more-scan+1+2);
   char *store=link_target;
   while(scan<more && (quote ? *scan!=quote : !is_ascii_space(*scan)))
      *store++=*scan++;
   *store=0;
   // ok, found the target.

   // check if the target is a relative and not a cgi
   if(strchr(link_target,'?'))
      return tag_len;	// cgi
   char *c=strchr(link_target,'#');
   if(c)
      *c=0; // strip pointer inside document.
   if(*link_target==0)
      return tag_len;	// no target ?

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
	 // strip leading slash
	 link_len--;
	 memmove(link_target,link_target+1,link_len+1);
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

   if(list)
   {
      int year,month,day,hour,minute;
      char month_name[32];
      char size_str[32];
      const char *more1;
      int n;
      char *line_add=(char*)alloca(link_len+128);
      bool data_available=false;
      // try to extract file information
      const char *eol=find_char(more+1,end-more-1,'\n');
      if(!eol)
      {
	 if(eof)
	    goto add_file;
	 return less-buf;  // not full line yet
      }

      more1=more;
      for(;;)
      {
	 more1++;
	 more1=find_char(more1,eol-more1,'>');
	 if(!more1)
	    goto add_file;
	 if(more1[-1]!='.')   // apache bug?
	    break;
      }

      n=sscanf(more1+1,"%2d-%3s-%4d %2d:%2d %30s",
		     &day,month_name,&year,&hour,&minute,size_str);
      if(n!=6)
	 goto add_file;

      // y2000 problem :)
      if(year<37)
	 year+=2000;
      else if(year<100)
	 year+=1900;

      data_available=true;

   add_file:
      if(data_available)
      {
	 month=parse_month(month_name);
	 if(month>=0)
	    sprintf(month_name,"%02d",month+1);
	 sprintf(line_add,"%s  %10s  %04d-%s-%02d %02d:%02d  %s\n",
	    is_directory?"drwxr-xr-x":"-rw-r--r--",
	    size_str,year,month_name,day,hour,minute,link_target);
      }
      else
      {
	 sprintf(line_add,"%s    %s\n",
	    is_directory?"drwxr-xr-x":"-rw-r--r--",link_target);
      }

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
      fi->SetType(is_directory ? fi->DIRECTORY : fi->NORMAL);
      set->Add(fi);
   }

   return tag_len;
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
      if(use_cache && LsCache::Find(session,curr,FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size))
      {
	 from_cache=true;
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(curr,FA::LONG_LIST);
	 ubuf=new FileInputBuffer(session);
      }
      if(curr_url)
	 delete curr_url;
      curr_url=new ParsedURL(session->GetFileURL(curr));
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(ubuf->Eof() && len==upos) // eof
   {
      if(!from_cache)
      {
	 const char *cache_buffer;
	 int cache_buffer_size;
	 ubuf->Get(&cache_buffer,&cache_buffer_size);
	 if(cache_buffer && cache_buffer_size>0)
	 {
	    LsCache::Add(session,curr,FA::LONG_LIST,
			   cache_buffer,cache_buffer_size);
	 }
      }
      delete ubuf;
      ubuf=0;
      upos=0;
      return MOVED;
   }
   int m=STALL;
   b+=upos;
   len-=upos;

   int n=parse_html(b,len,ubuf->Eof(),buf,0,&all_links,curr_url);
   if(n>0)
   {
      upos+=n;
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
   upos=0;
   from_cache=false;
   if(args->count()<2)
      args->Append("");
   args->rewind();
   curr=0;
   curr_url=0;
}

HttpDirList::~HttpDirList()
{
   delete ubuf;
   if(curr_url)
      delete curr_url;
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
   from_cache=false;

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
	 from_cache=true;
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open(curr_dir,FA::LONG_LIST);
	 ubuf=new FileInputBuffer(session);
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
   for(;;)
   {
      int n=parse_html(b,len,true,0,&set,0,&prefix);
      if(n==0)
	 break;
      b+=n;
      len-=n;
   }

   if(!from_cache)
   {
      const char *cache_buffer;
      int cache_buffer_size;
      ubuf->Get(&cache_buffer,&cache_buffer_size);
      if(cache_buffer && cache_buffer_size>0)
      {
	 LsCache::Add(session,curr_dir,FA::LONG_LIST,
			cache_buffer,cache_buffer_size);
      }
   }
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
	 from_cache=true;
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 xfree(cache_buffer);
      }
      else
      {
	 session->Open("",FA::LONG_LIST);
	 ubuf=new FileInputBuffer(session);
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
      for(;;)
      {
	 int n=parse_html(b,len,true,0,result,0,&prefix);
	 if(n==0)
	    break;
	 b+=n;
	 len-=n;
      }

      if(!from_cache)
      {
	 const char *cache_buffer;
	 int cache_buffer_size;
	 ubuf->Get(&cache_buffer,&cache_buffer_size);
	 if(cache_buffer && cache_buffer_size>0)
	 {
	    LsCache::Add(session,"",FA::LONG_LIST,
			   cache_buffer,cache_buffer_size);
	 }
      }
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
	    if(file->filetype==file->DIRECTORY)
	    {
	       continue; // FIXME: directories need slash.
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
