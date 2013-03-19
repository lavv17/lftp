/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

   void push(const char *);
   void pop();
   void set_base_dir(const char *d) {
      base_dir.set(d);
      if(base_dir.length()>1)
	 base_dir.chomp('/');
   }
   const char *top(int i=0) { return stack.count()>i ? stack[stack.count()-i-1].get() : 0; }
};

void xml_context::push(const char *s)
{
   stack.append(s);
}
void xml_context::pop()
{
   stack.chop();
}

static void start_handle(void *data, const char *el, const char **attr)
{
   xml_context *ctx=(xml_context*)data;
   ctx->push(el);
   if(!strcmp(ctx->top(), "DAV:response"))
   {
      delete ctx->fi;
      ctx->fi=new FileInfo;
   }
   else if(!strcmp(ctx->top(), "DAV:collection"))
   {
      ctx->fi->SetType(ctx->fi->DIRECTORY);
      ctx->fi->SetMode(0755);
   }
}
static void end_handle(void *data, const char *el)
{
   xml_context *ctx=(xml_context*)data;
   if(!strcmp(ctx->top(), "DAV:response"))
   {
      if(ctx->fi && ctx->fi->name)
      {
	 if(!ctx->fs)
	    ctx->fs=new FileSet;
	 ctx->fs->Add(ctx->fi.borrow());
      }
   }
   ctx->pop();
}
static void chardata_handle(void *data, const char *chardata, int len)
{
   xml_context *ctx=(xml_context*)data;
   if(!ctx->fi)
      return;

   char *s=string_alloca(len+1);
   memcpy(s,chardata,len);
   s[len]=0;
   const char *tag=ctx->top();
   if(!strcmp(tag, "DAV:href") && !xstrcmp(ctx->top(1), "DAV:response"))
   {
      ParsedURL u(s,true);
      s=alloca_strdup(u.path);
      int s_len=strlen(s);
      if(s_len>0 && s[s_len-1]=='/')
      {
	 if(s_len>1)
	    s[--s_len]=0;
	 ctx->fi->SetType(ctx->fi->DIRECTORY);
	 ctx->fi->SetMode(0755);
      }
      else
      {
	 ctx->fi->SetType(ctx->fi->NORMAL);
	 ctx->fi->SetMode(0644);
      }
      if(s[0]=='/' && s[1]=='~')
	 s++;
      ctx->fi->SetName(ctx->base_dir.eq(s) ? "." : basename_ptr(s));
   }
   else if(!strcmp(tag,"DAV:getcontentlength"))
   {
      long long size_ll=0;
      if(sscanf(s,"%lld",&size_ll)==1)
	 ctx->fi->SetSize(size_ll);
   }
   else if(!strcmp(tag,"DAV:getlastmodified"))
   {
      ctx->fi->SetDate(Http::atotm(s),0);
   }
   else if(!strcmp(tag,"DAV:creator-displayname"))
   {
      ctx->fi->SetUser(s);
   }
   else if(!strcmp(tag,"http://apache.org/dav/props/executable"))
   {
      if(s[0]=='T')
	 ctx->fi->SetMode(0755);
      else if(s[0]=='F')
	 ctx->fi->SetMode(0644);
   }
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
