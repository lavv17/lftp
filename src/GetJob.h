/*
 * lftp and utils
 *
 * Copyright (c) 1996-2004 by Alexander V. Lukyanov (lav@yars.free.net)
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
   FileCopyPeer *NoProtoSrcLocal(const char *src);
   FileCopyPeer *NoProtoDstLocal(const char *dst);
   FileCopyPeer *NoProtoPeer(FileAccess *session,const char *file,FA::open_mode mode);

protected:
   FileCopyPeer *CreateCopyPeer(FileAccess *session,const char *path,FA::open_mode mode);
   void	 NextFile();

   bool delete_files;
   bool remove_target_first;
   bool truncate_target_first;
   char *backup_file;
   mode_t file_mode;
   FileStream *local;
   bool reverse;

   void RemoveBackupFile();

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
