/*
 * lftp - file transfer program
 *
 * Copyright (c) 2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "HttpDir.h"
#include "log.h"
#include "url.h"
#include "misc.h"

#ifdef HAVE_EXPAT_H
#include <expat.h>

struct xml_context
{
   int depth;
   char **stack;
   FileSet *fs;
   FileInfo *fi;
   char *base_dir;

   xml_context() { depth=0; stack=0; fs=0; fi=0; base_dir=0; }
   ~xml_context();
   void push(const char *);
   void pop();
   void set_base_dir(const char *d) { base_dir=xstrdup(d); }
   const char *top(int i=0) { return depth>i?stack[depth-i-1]:0; }
};

xml_context::~xml_context()
{
   for(int i=0; i<depth; i++)
      xfree(stack[i]);
   xfree(stack);
   xfree(base_dir);
   delete fs;
   delete fi;
}
void xml_context::push(const char *s)
{
   depth++;
   if(!(depth&(depth-1))) {
      stack=(char**)xrealloc(stack,depth*2*sizeof(*stack));
   }
   stack[depth-1]=xstrdup(s);
}
void xml_context::pop()
{
   depth--;
   xfree(stack[depth]);
   stack[depth]=0;
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
	 ctx->fs->Add(ctx->fi);
	 ctx->fi=0;
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
      s=u.path;
      int s_len=strlen(s);
      if(s_len>0 && s[s_len-1]=='/')
      {
	 s[s_len-1]=0;
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
      const char *name;
      if(ctx->base_dir && !strcmp(s,ctx->base_dir))
	 name=".";
      else
	 name=basename_ptr(s);
      ctx->fi->SetName(name);
   }
   else if(!strcmp(tag,"DAV:getcontentlength"))
   {
      ctx->fi->SetSize(atoll(s));
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
	      XML_GetCurrentLineNumber(p),
	      XML_ErrorString(XML_GetErrorCode(p)));
      XML_ParserFree(p);
      return 0;
   }
   FileSet *result=ctx.fs;
   ctx.fs=0;
   XML_ParserFree(p);
   return result;
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
	      XML_GetCurrentLineNumber(xml_p),
	      XML_ErrorString(XML_GetErrorCode(xml_p)));
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
#else // !HAVE_EXPAT_H
FileSet *HttpListInfo::ParseProps(const char *b,int len,const char *base_dir) { return 0; }
void HttpDirList::ParsePropsFormat(const char *b,int len,bool eof) {}
#endif // !HAVE_EXPAT_H
