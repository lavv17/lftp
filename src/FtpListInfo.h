/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FTPLISTINFO_H
#define FTPLISTINFO_H

#include "NetAccess.h"

class FtpListInfo : public GenericParseListInfo
{
   typedef FileInfo *(*FtpLineParser)(char *line,int *err,const char *tz);
   static FtpLineParser line_parsers[];
   FileSet *ParseLongList(const char *buf,int len);
   FileSet *ParseShortList(const char *buf,int len);
public:
   virtual FileSet *Parse(const char *buf,int len);
   FtpListInfo(FileAccess *session) : GenericParseListInfo(session) {}
};

#endif//FTPLISTINFO_H
