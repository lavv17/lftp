/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef GETJOB_H
#define GETJOB_H

#include "CopyJob.h"

class GetJob : public CopyJobEnv, ResClient
{
   FileCopyPeer *SrcLocal(const char *src);
   FileCopyPeer *DstLocal(const char *dst);
   FileCopyPeer *CreateCopyPeer(const ParsedURL &url,const char *path,FA::open_mode mode);
   FileCopyPeer *CreateCopyPeer(const char *path,FA::open_mode mode);
   bool IsRemoteNonURL(const ParsedURL &url,FA::open_mode mode);
   bool IsLocalNonURL(const ParsedURL &url,FA::open_mode mode);
   static bool IsLocal(const ParsedURL &url);

protected:
   FileCopyPeer *CreateCopyPeer(FileAccess *session,const char *path,FA::open_mode mode);
   FileCopyPeer *CreateCopyPeer(const FileAccessRef& session,const char *path,FA::open_mode mode);
   void	NextFile();

   bool make_dirs;
   bool delete_files;
   bool remove_target_first;
   bool truncate_target_first;
   bool reverse;

public:
   GetJob(FileAccess *s,ArgV *a,bool c=false);

   void DeleteFiles() { delete_files=true; }
   void RemoveSourceLater() { delete_files=true; }
   void Reverse() { reverse=true; } // put
   void RemoveTargetFirst() { remove_target_first=true; }
};

#endif /* GETJOB_H */
