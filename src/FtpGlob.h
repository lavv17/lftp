/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FTPGLOB_H
#define FTPGLOB_H

#include "FileAccess.h"

class FtpGlob : public Glob
{
   char	 *dir;
   char  **dir_list;
   FtpGlob *updir_glob;
   char	 *base_dir;

   ListInfo *li;

   FileAccess  *f;

   void Init(FileAccess *session);

public:
   int	 Do();
   const char *Status();

   FtpGlob(FileAccess *session,const char *n_pattern);
   virtual ~FtpGlob();
};

#endif
