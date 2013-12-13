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

/* $Id: DummyProto.cc,v 1.12 2009/03/20 12:28:04 lav Exp $ */

#include <config.h>

#include "DummyProto.h"
#include <stddef.h>

DummyProto::DummyProto() {}
DummyProto::~DummyProto() {}
int DummyProto::Do() { return STALL; }
int DummyProto::Done() { return NO_HOST; }
const char *DummyProto::GetProto() const { return ""; }
FileAccess *DummyProto::Clone() const { return new DummyProto; }
int DummyProto::Read(Buffer *buf,int size) { return NO_HOST; };
int DummyProto::Write(const void *buf,int size) { return NO_HOST; };
int DummyProto::StoreStatus() { return NO_HOST; }

class DummyDirList : public DirList
{
public:
   DummyDirList(FA *p1,ArgV *a) : DirList(p1,a) {}
   int Do() { SetError(session->StrError(FA::NO_HOST)); return STALL; }
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

const char *DummyNoProto::GetProto() const { return proto; }
FileAccess *DummyNoProto::Clone() const { return new DummyNoProto(proto); }
const char *DummyNoProto::StrError(int)
{
   static xstring str;
   str.vset(proto.get(),_(" - not supported protocol"),NULL);
   return str;
}
