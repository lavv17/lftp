/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef GLOB_H
#define GLOB_H

#include "FileAccess.h"

class Glob : public FileAccessOperation
{
protected:
   char  *pattern;
   FileSet list;
   bool	 dirs_only;
   bool	 files_only;
   bool	 match_period;
   bool	 inhibit_tilde;
   bool	 casefold;
   void	 add(const FileInfo *info);
   void	 add_force(const FileInfo *info);
   virtual ~Glob();
public:
   const char *GetPattern() { return pattern; }
   FileSet *GetResult() { return &list; }
   Glob(const char *p);
   void DirectoriesOnly() { dirs_only=true; }
   void FilesOnly() { files_only=true; }
   void NoMatchPeriod() { match_period=false; }
   void NoInhibitTilde() { inhibit_tilde=false; }
   void CaseFold() { casefold=true; }

   static bool HasWildcards(const char *);
   static void UnquoteWildcards(char *);
};
class NoGlob : public Glob
{
public:
   NoGlob(const char *p);
   const char *Status() { return ""; }
   int Do();
};
class GlobURL
{
   FileAccess *orig_session;
   FileAccess *session;
   char *url_prefix;
public:
   Glob *glob;

   enum type_select
   {
      ALL,
      FILES_ONLY,
      DIRS_ONLY
   };

   GlobURL(FileAccess *s,const char *p,type_select t=ALL);
   ~GlobURL();
   FileSet *GetResult();
   bool Done()  { return glob->Done(); }
   bool Error() { return glob->Error(); }
   const char *ErrorText() { return glob->ErrorText(); }
   const char *Status() { return glob->Status(); }

   void NewGlob(const char *p);
   const char *GetPattern() { return glob->GetPattern(); }

private:
   type_select type;
};

class GenericGlob : public Glob
{
   FileAccess  *session;

   const char *curr_dir;
   FileSet *dir_list;
   Glob *updir_glob;

   ListInfo *li;

public:
   int	 Do();
   const char *Status();

   GenericGlob(FileAccess *session,const char *n_pattern);
   virtual ~GenericGlob();
};

#endif
