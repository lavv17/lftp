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

#ifndef MIRRORJOB_H
#define MIRRORJOB_H

#include "FileAccess.h"
#include "FileSet.h"
#include "Job.h"
#include "PatternSet.h"

class MirrorJob : public Job
{
   enum state_t
   {
      INITIAL_STATE,
      MAKE_TARGET_DIR,
      CHANGING_DIR_SOURCE,
      CHANGING_DIR_TARGET,
      GETTING_LIST_INFO,
      WAITING_FOR_TRANSFER,
      TARGET_REMOVE_OLD,
      TARGET_REMOVE_OLD_FIRST,
      TARGET_CHMOD,
      FINISHING,
      DONE
   };
   state_t state;

   FileAccess *source_session;
   FileAccess *target_session;
   bool target_is_local;
   bool source_is_local;

   FileSet *target_set;
   FileSet *source_set;
   FileSet *to_transfer;
   FileSet *same;
   FileSet *to_rm;
   FileSet *to_rm_mismatched;
   FileSet *old_files_set;
   FileSet *new_files_set;
   void	 InitSets(FileSet *src,FileSet *dst);

   void	 HandleFile(FileInfo *);

   bool create_target_dir;
   bool	no_target_dir;	   // target directory does not exist (for script_only)

   ListInfo *source_list_info;
   ListInfo *target_list_info;

   char	 *source_dir;
   char	 *source_relative_dir;
   char	 *target_dir;
   char	 *target_relative_dir;

   struct Statistics
   {
      int tot_files,new_files,mod_files,del_files;
      int dirs,del_dirs;
      int tot_symlinks,new_symlinks,mod_symlinks,del_symlinks;
      int error_count;
      long long bytes;
      double time;
      Statistics();
      void Reset();
      void Add(const Statistics &);
      bool HaveSomethingDone(int);
   };
   Statistics stats;

   int	 root_transfer_count;
   int	 &transfer_count;

   int	 flags;

   PatternSet *exclude;

   bool	 create_remote_dir;

   void	 Report(const char *fmt,...) PRINTF_LIKE(2,3);
   void	 va_Report(const char *fmt,va_list v);
   int	 verbose_report;
   MirrorJob *parent_mirror;

   time_t newer_than;
   time_t older_than;
   Range *size_range;

   char *script_name;
   FILE *script;
   bool script_only;
   bool script_needs_closing;

   bool use_cache;

   bool remove_source_files;

   int parallel;
   int pget_n;
   int pget_minchunk;

   mode_t get_mode_mask();

   int source_redirections;
   int target_redirections;

   void HandleChdir(FileAccess * &session, int &redirections);
   void HandleListInfoCreation(FileAccess * &session,ListInfo * &list_info,
	    const char *relative_dir);
   void HandleListInfo(ListInfo * &list_info, FileSet * &set);

public:
   enum
   {
      ALLOW_SUID=1<<0,
      DELETE=1<<1,
      NO_RECURSION=1<<2,
      ONLY_NEWER=1<<3,
      NO_PERMS=1<<4,
      CONTINUE=1<<5,
      REPORT_NOT_DELETED=1<<6,
      RETR_SYMLINKS=1<<7,
      NO_UMASK=1<<8,
      ALLOW_CHOWN=1<<9,
      IGNORE_TIME=1<<10,
      REMOVE_FIRST=1<<11,
      IGNORE_SIZE=1<<12,
      NO_SYMLINKS=1<<13,
      LOOP=1<<14
   };

   void SetFlags(int f,int v)
   {
      if(v)
	 flags|=f;
      else
	 flags&=~f;
   }

   MirrorJob(MirrorJob *parent,FileAccess *f,FileAccess *target,
      const char *new_source_dir,const char *new_target_dir);
   ~MirrorJob();

   int	 Do();
   int	 Done() { return state==DONE; }
   void	 ShowRunStatus(StatusLine *);
   void	 PrintStatus(int v,const char *);
   void	 SayFinal() { PrintStatus(0,""); }
   int	 ExitCode() { return stats.error_count; }

   void	 SetExclude(PatternSet *x) { exclude=x; }
   void	 SetSizeRange(Range *r) { size_range=r; }

   void	 SetVerbose(int v) { verbose_report=v; }

   void	 CreateRemoteDir() { create_remote_dir=true; }

   void	 SetNewerThan(const char *file);
   void	 SetOlderThan(const char *file);

   void  UseCache(bool u) { use_cache=u; }
   void	 RemoveSourceFiles() { remove_source_files=true; }

   void  SetParallel(int p) { parallel=p; }
   void  SetPGet(int n) { pget_n=n; }

   void Fg();
   void Bg();

   char	 *SetScriptFile(const char *n);
   void	 ScriptOnly(bool yes=true)
      {
	 script_only=yes;
      }
};

#endif//MIRRORJOB_H
