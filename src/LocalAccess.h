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

/* $Id$ */

#ifndef LOCALACCESS_H
#define LOCALACCESS_H

#include "FileAccess.h"
#include "Filter.h"

class LocalAccess : public FileAccess
{
   FDStream *stream;
   bool done;
   void errno_handle();
   void fill_array_info();

public:
   void Init();
   LocalAccess();
   LocalAccess(const LocalAccess *);
   ~LocalAccess();

   void Connect(const char *host,const char *port) {}
   void AnonymousLogin() {}
   void Login(const char *u,const char *p) {}

   const char *GetProto() { return "file"; }
   FileAccess *Clone() { return new LocalAccess(this); }
   static FileAccess *New();
   bool SameLocationAs(FileAccess *fa);

   int Read(void *buf,int size);
   int Write(const void *buf,int size);
   int StoreStatus();
   int Do();
   int Done();
   void Close();

   const char *CurrentStatus();

   static void ClassInit();

   ListInfo *MakeListInfo(const char *path);
   Glob	    *MakeGlob(const char *pattern);
   DirList  *MakeDirList(ArgV *a);
};

class LocalListInfo : public ListInfo
{
public:
   LocalListInfo(FileAccess *s,const char *d) : ListInfo(s,d) {}
   const char *Status() { return "..."; }
   int Do();
};

class LocalDirList : public DirList
{
   IOBuffer *ubuf;
   FgData *fg_data;
public:
   LocalDirList(ArgV *a,const char *cwd);
   ~LocalDirList();
   const char *Status() { return "..."; }
   int Do();
};

#endif//LOCALACCESS_H
