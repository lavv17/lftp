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

#ifndef FTPCOPY_H
#define FTPCOPY_H

#include "ftpclass.h"
#include "Job.h"
#include "ArgV.h"
#include "StatusLine.h"

class FtpCopy : public Job
{
   Ftp *src;
   Ftp *dst;
   char *src_file;
   char *dst_file;
   enum state_t { INIT, GET_SIZE, WAIT, ERROR, DONE };
   state_t state;
   FileAccess::fileinfo info;
   bool cont;
   bool no_rest;
   bool reverse_passive;
   long dst_size;
   int src_retries;
   int dst_retries;

   ArgV *args;
   const char *op;
   const char *src_url;
   const char *dst_url;

   void Close();
   int ProcessURL(const char *u,Ftp **s,char **file,FileAccess *def);

public:
   void Init();
   FtpCopy(ArgV *a,FileAccess *def);
   ~FtpCopy();

   int Do();
   int Done();
   int ExitCode();

   void ShowRunStatus(StatusLine *sl);
};

#endif // FTPCOPY_H
