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

#include "ListInfo.h"
#include "GetJob.h"


/*
 * // this is obsolete
 * Sequentially do the following steps:
 * 1. retrieve long listing of current remote directory
 * 2. make FileSet of it
 * 2.1. get short list and merge thes lists.
 * 2.2. get date for those files which don't have prec date
 * 2.3. get size ^^^
 * 3. make FileSet of current local dir
 * 4. `subtract' them, thus getting list of newer/different remote files
 *    and list of local files to be deleted.
 * 5. download these files
 * 6. make local directories/links, delete files.
 * 7. for each directory, start recursive mirror job
 * 8. wait for them to finish (or wait for each after starting)
 * 9. Done.
 */

class MirrorJob : public SessionJob
{
   enum state_t
   {
      INITIAL_STATE,
      MAKE_REMOTE_DIR,
      CHANGING_REMOTE_DIR,
      GETTING_LIST_INFO,
      WAITING_FOR_SUBGET,
      WAITING_FOR_SUBMIRROR,
      WAITING_FOR_RM_BEFORE_PUT,
      WAITING_FOR_MKDIR_BEFORE_SUBMIRROR,
      REMOTE_REMOVE_OLD,
      DONE
   };
   state_t state;

   FileSet *remote_set;
   FileSet *local_set;
   FileSet *to_transfer;
   FileSet *same;
   FileSet *to_rm;
   void	 InitSets(FileSet *src,FileSet *dst);

   FileInfo *file;
   void	 HandleFile(int);

   ListInfo *list_info;
   FileAccess *local_session;

   char	 *local_dir;
   char	 *local_relative_dir;
   char	 *remote_dir;
   char	 *remote_relative_dir;

   int	 tot_files,new_files,mod_files,del_files;
   int	 dirs,del_dirs;
   int	 tot_symlinks,new_symlinks,mod_symlinks,del_symlinks;

   int	 flags;

   char  *rx_include,*rx_exclude;
   regex_t rxc_include,rxc_exclude;

   time_t   prec;

   bool	 dir_made;
   bool	 create_remote_dir;

   void	 Report(const char *fmt,...) PRINTF_LIKE(2,3);
   void	 va_Report(const char *fmt,va_list v);
   int	 verbose_report;
   MirrorJob *parent_mirror;

   time_t newer_than;

   const char *SetRX(const char *s,char **rx,regex_t *rxc);

public:
   enum
   {
      ALLOW_SUID=1,
      DELETE=2,
      NO_RECURSION=4,
      ONLY_NEWER=8,
      NO_PERMS=16,
      CONTINUE=32,
      REVERSE=64,
      REPORT_NOT_DELETED=128,
      RETR_SYMLINKS=256,
      NO_UMASK=512
   };

   void SetFlags(int f,int v)
   {
      if(v)
	 flags|=f;
      else
	 flags&=~f;
   }

   MirrorJob(FileAccess *f,const char *new_local_dir,const char *new_remote_dir);
   ~MirrorJob();

   int	 Do();
   int	 Done() { return state==DONE; }
   void	 ShowRunStatus(StatusLine *);
   void	 PrintStatus(int v);
   void	 SayFinal() { PrintStatus(-1); }

   const char *SetInclude(const char *s)
      {
	 return SetRX(s,&rx_include,&rxc_include);
      }
   const char *SetExclude(const char *s)
      {
	 return SetRX(s,&rx_exclude,&rxc_exclude);
      }

   void	 SetVerbose(int v) { verbose_report=v; }

   void	 SetPrec(time_t p) { prec=p; }
   void	 CreateRemoteDir() { create_remote_dir=true; }

   void	 SetNewerThan(const char *file);
};

#endif//MIRRORJOB_H
