/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef GETFILEINFO_H
#define GETFILEINFO_H

#include <stdio.h>

#include "SMTask.h"
#include "FileAccess.h"

class GetFileInfo: public ListInfo
{
   FileAccess *session;
   ListInfo *li;
   
   /* file or dir we're listing: */
   char *dir;

   /* directory we've actually listed: */
   char *realdir;

   char *origdir;
   
   bool showdir;
   
   enum state_t { CHANGE_DIR, CHANGING_DIR, GETTING_LIST, DONE } state;
   /* whether we've tried to cd to the whole dir (treating it as a dir): */
   bool tried_dir;
   /* and whether we've tried to cd to the basename (treating it as a file): */
   bool tried_file;

   char *saved_error_text;

public:
   GetFileInfo(FileAccess *a, const char *path, bool showdir);
   virtual ~GetFileInfo();

   int Do();
   const char *Status();
};

#endif /* GETFILEINFO_H */
