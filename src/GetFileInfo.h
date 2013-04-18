/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef GETFILEINFO_H
#define GETFILEINFO_H

#include "trio.h"

#include "SMTask.h"
#include "FileAccess.h"

class GetFileInfo : public ListInfo
{
   const FileAccessRef& session;

   SMTaskRef<ListInfo> li;

   /* file or dir we're listing: */
   xstring_c dir;

   /* directory we've actually listed: */
   xstring_c path_to_prefix;

   /* directory we started in: */
   FileAccess::Path origdir;

   /* In showdir mode, we make sure the path actually exists; this is
    * the filename to look for. */
   xstring verify_fn;

   bool showdir;

   enum state_t { INITIAL, CHANGE_DIR, CHANGING_DIR, GETTING_LIST, GETTING_INFO_ARRAY, DONE } state;
   /* whether we've tried to cd to the whole dir (treating it as a dir): */
   bool tried_dir;
   /* and whether we've tried to cd to the basename (treating it as a file): */
   bool tried_file;
   /* and the last-ditch GetInfoArray */
   bool tried_info;
   /* whether we found out the file type from cache */
   bool from_cache;
   /* whether the given path was a file or directory. */
   bool was_directory;
   /* if true, prepend the appropriate relative path to the result */
   bool prepend_path;

   xstring_c saved_error_text;

   FileSet get_info;

   void PrepareToDie();

public:
   GetFileInfo(const FileAccessRef& a, const char *path, bool showdir);
   virtual ~GetFileInfo();

   int Do();
   const char *Status();
   bool WasDirectory() const { return was_directory; }
   void DontPrependPath() { prepend_path=false; }
};

#endif /* GETFILEINFO_H */
