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

#ifndef FILECOPYFTP_H
#define FILECOPYFTP_H

#include "FileCopy.h"
#include "ftpclass.h"

class FileCopyFtp : public FileCopy
{
   Ftp *ftp_src;
   Ftp *ftp_dst;
   bool no_rest;
   bool passive_source;
   bool orig_passive_source;
   bool disable_fxp;
#if USE_SSL
   bool protect;
   bool passive_ssl_connect;
   bool orig_passive_ssl_connect;
#endif
   int src_retries;
   int dst_retries;
   time_t src_try_time;
   time_t dst_try_time;

   void Close();

public:
   void Init();
   FileCopyFtp(FileCopyPeer *src,FileCopyPeer *dst,bool cont,bool rp);
   ~FileCopyFtp();

   int Do();

   static FileCopy *New(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
};

#endif // FILECOPYFTP_H
