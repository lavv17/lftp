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

#ifndef MIRRORJOB_H
#define MIRRORJOB_H

#include "FileAccess.h"
#include "FileSet.h"
#include "Job.h"

class MirrorJob : public Job
{
   enum state_t
   {
      INITIAL_STATE,
      MAKE_TARGET_DIR,
      CHANGING_DIR,
      GETTING_LIST_INFO,
      WAITING_FOR_TRANSFER,
      WAITING_FOR_SUBMIRROR,
      TARGET_REMOVE_OLD,
      TARGET_CHMOD,
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
   FileSet *old_files_set;
   FileSet *new_files_set;
   void	 InitSets(FileSet *src,FileSet *dst);

   FileInfo *file;
   void	 HandleFile(int);

   bool cont_this; // try to continue transfer of current file.
   bool create_target_dir;

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
      Statistics();
      void Add(const Statistics &);
   };
   Statistics stats;

   int	 flags;

   char  *rx_include,*rx_exclude;
   regex_t rxc_include,rxc_exclude;

   bool	 dir_made;
   bool	 create_remote_dir;

   void	 Report(const char *fmt,...) PRINTF_LIKE(2,3);
   void	 va_Report(const char *fmt,va_list v);
   int	 verbose_report;
   MirrorJob *parent_mirror;

   time_t newer_than;

   const char *SetRX(const char *s,char **rx,regex_t *rxc);

   FILE *script;
   bool script_only;
   bool script_needs_closing;

   bool use_cache;

   bool remove_source_files;

   int parallel;

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
      ALLOW_SUID=1,
      DELETE=2,
      NO_RECURSION=4,
      ONLY_NEWER=8,
      NO_PERMS=16,
      CONTINUE=32,
      REPORT_NOT_DELETED=128,
      RETR_SYMLINKS=256,
      NO_UMASK=512,
      ALLOW_CHOWN=1024
   };

   void SetFlags(int f,int v)
   {
      if(v)
	 flags|=f;
      else
	 flags&=~f;
   }

   MirrorJob(FileAccess *f,FileAccess *target,
      const char *new_source_dir,const char *new_target_dir);
   ~MirrorJob();

   int	 Do();
   int	 Done() { return state==DONE; }
   void	 ShowRunStatus(StatusLine *);
   void	 PrintStatus(int v);
   void	 SayFinal() { PrintStatus(-1); }
   int	 ExitCode() { return stats.error_count; }

   const char *SetInclude(const char *s)
      {
	 return SetRX(s,&rx_include,&rxc_include);
      }
   const char *SetExclude(const char *s)
      {
	 return SetRX(s,&rx_exclude,&rxc_exclude);
      }

   void	 SetVerbose(int v) { verbose_report=v; }

   void	 CreateRemoteDir() { create_remote_dir=true; }

   void	 SetNewerThan(const char *file);

   void  UseCache(bool u) { use_cache=u; }
   void	 RemoveSourceFiles() { remove_source_files=true; }

   void  SetParallel(int p) { parallel=p; }

   void Fg();
   void Bg();
};

#endif//MIRRORJOB_H
