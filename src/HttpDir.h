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

#ifndef HTTPDIR_H
#define HTTPDIR_H

#include "Http.h"

class HttpListInfo : public ListInfo
{
   Http *session;

   FA::fileinfo *get_info;
   int get_info_cnt;

   Buffer *ubuf;
   bool from_cache;

public:
   HttpListInfo(Http *session);
   virtual ~HttpListInfo();
   int Do();
   const char *Status();
};

class ParsedURL;

class HttpDirList : public DirList
{
   FileAccess *session;
   Buffer *ubuf;
   int upos;
   bool from_cache;
   const char *curr;
   ParsedURL *curr_url;
   FileSet all_links;
   int mode;

public:
   HttpDirList(ArgV *a,FileAccess *fa);
   ~HttpDirList();
   const char *Status();
   int Do();

   void Suspend();
   void Resume();
};

class HttpGlob : public Glob
{
   FileAccess *session;
   char	 *dir;
   const char *curr_dir;
   char  **dir_list;
   int   dir_index;
   HttpGlob *updir_glob;

   Buffer *ubuf;
   bool	from_cache;

public:
   int	 Do();
   const char *Status();

   HttpGlob(FileAccess *s,const char *n_pattern);
   virtual ~HttpGlob();
};

#endif//HTTPDIR_H
