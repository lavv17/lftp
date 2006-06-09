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

#ifndef LSCACHE_H
#define LSCACHE_H

#include <time.h>
#include "FileAccess.h"

class Buffer;

class LsCache : public Timer
{
   int	 err_code;
   char	 *data;
   int	 data_len;
   FileSet *afset;    // associated file set

   char	 *arg;
   FileAccess *loc;
   int	 mode;

   static void Trim();
   int EstimateMemory() const;
   static LsCache *Find(FileAccess *p_loc,const char *a,int m);

protected:
   ~LsCache();

public:
   static void Add(FileAccess *p_loc,const char *a,int m,int err,const char *d,int l,const FileSet *f=0);
   static void Add(FileAccess *p_loc,const char *a,int m,int err,const Buffer *ubuf,const FileSet *f=0);
   static int Find(FileAccess *p_loc,const char *a,int m,int *err,const char **d, int *l,FileSet **f=0);
   static FileSet *FindFileSet(FileAccess *p_loc,const char *a,int m);

   static int IsDirectory(FileAccess *p_loc,const char *dir);
   static void SetDirectory(FileAccess *p_loc, const char *path, bool dir);

   enum change_mode { FILE_CHANGED, DIR_CHANGED, TREE_CHANGED };
   static void Changed(change_mode m,FileAccess *f,const char *what);
   static void FileChanged(FileAccess *f,const char *file)
      {
	 Changed(FILE_CHANGED,f,file);
      }
   static void DirectoryChanged(FileAccess *f,const char *dir)
      {
	 Changed(DIR_CHANGED,f,dir);
      }
   static void TreeChanged(FileAccess *f,const char *dir)
      {
	 Changed(TREE_CHANGED,f,dir);
      }

   static void List();
   static void Flush();
   static bool IsEnabled(const char *closure);
   static long SizeLimit();
};

#endif//LSCACHE_H
