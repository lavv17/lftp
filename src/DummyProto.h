/*
 * lftp and utils
 *
 * Copyright (c) 1998-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#ifndef DUMMYPROTO_H
#define DUMMYPROTO_H

#include "FileAccess.h"

class DummyProto : public FileAccess
{
public:
   int Do();
   int Done();
   const char *GetProto() const;
   FileAccess *Clone() const;
   int Read(void *buf,int size);
   int Write(const void *buf,int size);
   int StoreStatus();
   void Reconfig(const char *) {}

   ListInfo *MakeListInfo(const char *path);
   DirList *MakeDirList(ArgV *);
};

class DummyNoProto : public DummyProto
{
   xstring_c proto;
public:
   DummyNoProto(const char *p) : proto(p) {}

   const char *GetProto() const;
   FileAccess *Clone() const;
   const char *StrError(int err);
};

#endif
