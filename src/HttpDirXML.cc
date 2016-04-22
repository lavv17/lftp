/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "HttpDir.h"
#include "log.h"
#include "url.h"
#include "misc.h"

#if USE_EXPAT
#include <expat.h>

struct xml_context
{
   xarray_s<xstring_c> stack;
   Ref<FileSet> fs;
   Ref<FileInfo> fi;
   xstring base_dir;
   xstring chardata;

   void push(const char *);
   void pop();
   void process_chardata();

   void set_base_dir(const char *d) {
      base_dir.set(d);
      if(base_dir.length()>1)
	 base_dir.chomp('/');
   }
   const xstring_c& top(int i=0) const {
      return stack.count()>i ? stack[stack.count()-i-1] : xstring_c::null;
   }

   bool in(const char *tag) const {
      return top().eq(tag);
   }
   bool in(const char *tag0,const char *tag1) const {
      return top(0).eq(tag0) && top(1).eq(tag1);
   }
   bool has_chardata() const {
      return chardata.length()>0;
   }

   void log_tag(const char *end="") const {
      const char *tag=top();
      Log::global->Format(10,"XML: %*s<%s%s>\n",stack.length()*2,"",end,tag);
   }
   void log_tag_end() const { log_tag("/"); }
   void log_data() const {
      Log::global->Format(10,"XML: %*s`%s'\n",stack.length()*2+2,"",chardata.get());
   }
};

void xml_context::push(const char *s)
{
   stack.append(s);
   log_tag();

   if(in("DAV:response"))
   {
      delete fi;
      fi=new FileInfo;
   }
   else if(in("DAV:collection"))
   {
      fi->SetType(fi->DIRECTORY);
      fi->SetMode(0755);
   }
   chardata.truncate();
}

void xml_context::pop()
{
   if(has_chardata())
      process_chardata();
   if(in("DAV:response"))
   {
      if(fi && fi->name)
      {
	 if(!fs)
	    fs=new FileSet;
	 fs->Add(fi.borrow());
      }
   }
   log_tag_end();
   stack.chop();
}

void xml_context::process_chardata()
{
   log_data();
   if(in("DAV:href","DAV:response"))
   {
      ParsedURL u(chardata,true);
      xstring& s=u.path;
      bool is_directory=false;
      if(s.last_char()=='/')
      {
	 is_directory=true;
	 s.chomp('/');
	 fi->SetType(fi->DIRECTORY);
	 fi->SetMode(0755);
      }
      else
      {
	 fi->SetType(fi->NORMAL);
	 fi->SetMode(0644);
      }
      if(s.begins_with("/~"))
	 s.set_substr(0,1,0,0);
      fi->SetName(base_dir.eq(s) && is_directory ? "." : basename_ptr(s));
   }
   else if(in("DAV:getcontentlength"))
   {
      long long size_ll=0;
      if(sscanf(chardata,"%lld",&size_ll)==1)
	 fi->SetSize(size_ll);
   }
   else if(in("DAV:getlastmodified"))
   {
      time_t tm=Http::atotm(chardata);
      if(tm!=Http::ATOTM_ERROR)
	 fi->SetDate(tm,0);
   }
   else if(in("DAV:creator-displayname"))
   {
      fi->SetUser(chardata);
   }
   else if(in("http://apache.org/dav/props/executable"))
   {
      if(chardata[0]=='T')
	 fi->SetMode(0755);
      else if(chardata[0]=='F')
	 fi->SetMode(0644);
   }
}

static void start_handle(void *data, const char *el, const char **attr)
{
   xml_context *ctx=(xml_context*)data;
   ctx->push(el);
}
static void end_handle(void *data, const char *el)
{
   xml_context *ctx=(xml_context*)data;
   ctx->pop();
}
static void chardata_handle(void *data, const char *chardata, int len)
{
   xml_context *ctx=(xml_context*)data;
   if(!ctx->fi)
      return;
   ctx->chardata.append(chardata,len);
}

FileSet *HttpListInfo::ParseProps(const char *b,int len,const char *base_dir)
{
   XML_Parser p = XML_ParserCreateNS(0,0);
   if(!p)
      return 0;
   xml_context ctx;
   ctx.set_base_dir(base_dir);
   XML_SetUserData(p,&ctx);
   XML_SetElementHandler(p, start_handle, end_handle);
   XML_SetCharacterDataHandler(p, chardata_handle);

   if(!XML_Parse(p, b, len, /*eof*/1))
   {
      Log::global->Format(0, "XML Parse error at line %d: %s\n",
	      (int)XML_GetCurrentLineNumber(p),
	      XML_ErrorString(XML_GetErrorCode(p)));
      XML_ParserFree(p);
      return 0;
   }
   XML_ParserFree(p);
   return ctx.fs.borrow();
}

void HttpDirList::ParsePropsFormat(const char *b,int len,bool eof)
{
   if(len==0)
      goto end;
   if(!xml_p)
   {
      xml_p=XML_ParserCreateNS(0,0);
      xml_ctx=new xml_context;
      xml_ctx->set_base_dir(curr_url->path);
      XML_SetUserData(xml_p,xml_ctx);
      XML_SetElementHandler(xml_p, start_handle, end_handle);
      XML_SetCharacterDataHandler(xml_p, chardata_handle);
   }
   if(!XML_Parse(xml_p, b, len, eof))
   {
      Log::global->Format(0, "XML Parse error at line %d: %s\n",
	      (int)XML_GetCurrentLineNumber(xml_p),
	      XML_ErrorString(XML_GetErrorCode(xml_p)));
      parse_as_html=true;
      return;
   }
   if(!xml_ctx->fs)
      goto end;
   xml_ctx->fs->rewind();
   for(;;)
   {
      FileInfo *info=xml_ctx->fs->curr();
      if(!info)
	 break;
      info->MakeLongName();
      buf->Put(info->longname);
      if(ls_options.append_type)
      {
	 if(info->filetype==info->DIRECTORY)
	    buf->Put("/");
	 else if(info->filetype==info->SYMLINK && !info->symlink)
	    buf->Put("@");
      }
      buf->Put("\n");
      xml_ctx->fs->next();
   }
   xml_ctx->fs->Empty();
end:
   if(eof && xml_p)
   {
      XML_ParserFree(xml_p);
      xml_p=0;
      delete xml_ctx;
      xml_ctx=0;
   }
}
#else // !USE_EXPAT
FileSet *HttpListInfo::ParseProps(const char *b,int len,const char *base_dir) { return 0; }
void HttpDirList::ParsePropsFormat(const char *b,int len,bool eof) {}
#endif // !USE_EXPAT
