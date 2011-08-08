/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id: HttpDir.h,v 1.22 2008/11/27 05:56:19 lav Exp $ */

#ifndef HTTPDIR_H
#define HTTPDIR_H

#include "Http.h"
#if USE_EXPAT
# include <expat.h>
#endif

class HttpListInfo : public GenericParseListInfo
{
   FileSet *Parse(const char *buf,int len);
public:
   HttpListInfo(Http *session,const char *path)
      : GenericParseListInfo(session,path)
      {
	 get_time_for_dirs=false;
      }
   static FileSet *ParseProps(const char *buf,int len,const char *base_dir);
};

class ParsedURL;

class HttpDirList : public DirList
{
   Ref<IOBuffer> ubuf;
   const char *curr;
   Ref<ParsedURL> curr_url;
   FileSet all_links;
   int mode;
   bool parse_as_html;
   xstring_c base_href;

#if USE_EXPAT
   XML_Parser xml_p;
   struct xml_context *xml_ctx;
#endif

   LsOptions ls_options;

   void ParsePropsFormat(const char *b,int len,bool eof);

public:
   HttpDirList(FileAccess *s,ArgV *a);
   ~HttpDirList();
   const char *Status();
   int Do();

   void SuspendInternal();
   void ResumeInternal();
};

#endif//HTTPDIR_H
