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

   const char *GetProto() { return "file"; }
   FileAccess *Clone() { return new LocalAccess(this); }
   static FileAccess *New() { return new LocalAccess(); }
   bool SameLocationAs(FileAccess *fa);

   int Read(void *buf,int size);
   int Write(const void *buf,int size);
   int StoreStatus();
   int Do();
   int Done();
   void Close();

   static void ClassInit();

   ListInfo *MakeListInfo();
   Glob	    *MakeGlob(const char *pattern);
};

class LocalListInfo : public ListInfo
{
   const char *dir;
public:
   LocalListInfo(const char *d) { dir=d; }
   const char *Status() { return "..."; }
   int Do();
};

class LocalGlob : public Glob
{
public:
   LocalGlob(const char *cwd,const char *pattern);
   const char *Status() { return "..."; }
   int Do();
};

#endif//LOCALACCESS_H
