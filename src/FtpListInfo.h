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

#include "ftpclass.h"
#include "FtpGlob.h"

class FtpListInfo : public ListInfo
{
   enum state_t
      {
	 INITIAL,
	 GETTING_LONG_LIST,
	 GETTING_SHORT_LIST,
	 GETTING_INFO,
	 DONE
      };
   state_t state;
   Ftp *session;

   Ftp::fileinfo *get_info;
   int get_info_cnt;

   FtpGlob *glob;

public:
   static FileSet *ParseFtpLongList(const char * const *lines,int *err);

public:
   FtpListInfo(Ftp *session);
   virtual ~FtpListInfo();
   int Do();
   const char *Status();
};

#endif//FTPLISTINFO_H
