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

#ifndef GETJOB_H
#define GETJOB_H

#include "CopyJob.h"

class GetJob : public CopyJobEnv
{
protected:
   void	 NextFile();

   bool delete_files;
   bool remove_target_first;
   char *backup_file;
   mode_t file_mode;
   FileStream *local;
   bool reverse;

   void RemoveBackupFile();

   FileCopyPeer *NoProtoSrc(const char *src,bool from_local);
   FileCopyPeer *NoProtoDst(const char *dst,bool to_local);
   FileCopyPeer *CreateCopyPeer(const char *path,FA::open_mode mode);

public:
   GetJob(FileAccess *s,ArgV *a,bool c=false);
   ~GetJob();

   int	 Do();

   void DeleteFiles() { delete_files=true; }
   void RemoveSourceLater() { delete_files=true; }
   void Reverse() { reverse=true; } // put
   void RemoveTargetFirst() { remove_target_first=true; }
};

#endif /* GETJOB_H */
