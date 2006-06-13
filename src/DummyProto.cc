/*
 * lftp and utils
 *
 * Copyright (c) 1998-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "DummyProto.h"

int DummyProto::Do() { return STALL; }
int DummyProto::Done() { return NO_HOST; }
const char *DummyProto::GetProto() const { return ""; }
FileAccess *DummyProto::Clone() const { return new DummyProto; }
int DummyProto::Read(void *buf,int size) { return NO_HOST; };
int DummyProto::Write(const void *buf,int size) { return NO_HOST; };
int DummyProto::StoreStatus() { return NO_HOST; }

class DummyDirList : public DirList
{
   FA *p;
public:
   DummyDirList(FA *p1,ArgV *a) : DirList(a) { p=p1; }
   int Do() { SetError(p->StrError(FA::NO_HOST)); return STALL; }
   const char *Status() { return ""; }
};
class DummyListInfo : public ListInfo
{
public:
   DummyListInfo(FA *p1) : ListInfo(p1,0) {}
   int Do() { SetError(session->StrError(FA::NO_HOST)); return STALL; }
   const char *Status() { return ""; }
};
DirList *DummyProto::MakeDirList(ArgV *a)
{
   return new DummyDirList(this,a);
}
ListInfo *DummyProto::MakeListInfo(const char *path)
{
   return new DummyListInfo(this);
}


DummyNoProto::DummyNoProto(const char *p)
{
   proto=xstrdup(p);
}
DummyNoProto::~DummyNoProto()
{
   xfree(proto);
}
const char *DummyNoProto::GetProto() const { return proto; }
FileAccess *DummyNoProto::Clone() const { return new DummyNoProto(proto); }
const char *DummyNoProto::StrError(int)
{
   static char str[128];
   sprintf(str,"%.32s%s",proto,_(" - not supported protocol"));
   return str;
}
