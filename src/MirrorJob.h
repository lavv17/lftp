/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef MIRRORJOB_H
#define MIRRORJOB_H

#include "FileAccess.h"
#include "FileSet.h"
#include "Job.h"
#include "PatternSet.h"
#include "misc.h"

class MirrorJob : public Job
{
public:
   enum recursion_mode_t {
      RECURSION_ALWAYS,
      RECURSION_NEVER,
      RECURSION_MISSING,
      RECURSION_NEWER,
   };

private:
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
      TARGET_MKDIR,
      SOURCE_REMOVING_SAME,
      FINISHING,
      LAST_EXEC,
      DONE
   };
   state_t state;

   FileAccessRef source_session;
   FileAccessRef target_session;
   bool target_is_local;
   bool source_is_local;

   long long bytes_transferred;
   long long bytes_to_transfer;

   Ref<FileSet> target_set;
   Ref<FileSet> target_set_excluded;
   Ref<FileSet> source_set;
   Ref<FileSet> target_set_recursive;
   Ref<FileSet> source_set_recursive;

   Ref<FileSet> to_transfer;
   Ref<FileSet> to_mkdir;
   Ref<FileSet> same;
   Ref<FileSet> to_rm;
   Ref<FileSet> to_rm_mismatched;
   Ref<FileSet> old_files_set;
   Ref<FileSet> new_files_set;
   Ref<FileSet> to_rm_src;
   void InitSets(); // deduce above sets from source_set and target_set
   void ExcludeEmptyDir(const char *target_rel_dir);
   bool only_dirs;  // to_transfer (or to_mkdir) contains directories only

   void RemoveSourceLater(const FileInfo *fi) {
      if(!remove_source_files)
	 return;
      if(!to_rm_src)
	 to_rm_src=new FileSet();
      to_rm_src->Add(new FileInfo(*fi));
   }

   void AddBytesTransferred(long long b) {
      bytes_transferred+=b;
      if(parent_mirror)
	 parent_mirror->AddBytesTransferred(b);
   }
   void AddBytesToTransfer(long long b) {
      bytes_to_transfer+=b;
      if(parent_mirror)
	 parent_mirror->AddBytesToTransfer(b);
   }

   void	 HandleFile(FileInfo *);

   bool create_target_dir;
   bool	no_target_dir;	   // target directory does not exist (for script_only)
   bool remove_this_source_dir;

   SMTaskRef<ListInfo> source_list_info;
   SMTaskRef<ListInfo> target_list_info;

   xstring_c source_dir;
   xstring_c source_relative_dir;
   xstring_c target_dir;
   xstring_c target_relative_dir;

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
      bool HaveSomethingDone(unsigned mirror_flags);
   };
   Statistics stats;

   double transfer_time_elapsed;
   TimeDate transfer_start_ts;

   /* root_transfer_count is the global counter in the root mirror,
    * and weight of a non-root mirror in global transfer_count otherwise. */
   int	 root_transfer_count;

   unsigned flags;
   recursion_mode_t recursion_mode;
   int	 max_error_count;

   Ref<PatternSet> top_exclude;
   Ref<PatternSet> my_exclude;
   const PatternSet *exclude;

   bool	 create_remote_dir;

   void	 Report(const char *fmt,...) PRINTF_LIKE(2,3);
   void	 va_Report(const char *fmt,va_list v);
   int	 verbose_report;
   MirrorJob *parent_mirror;
   MirrorJob *root_mirror;

   time_t newer_than;
   time_t older_than;
   Ref<Range> my_size_range;
   const Range *size_range;

   xstring_c script_name;
   FILE *script;
   bool script_only;
   bool script_needs_closing;
   bool use_cache;
   bool remove_source_files;
   bool remove_source_dirs;
   bool skip_noaccess;

   int parallel;
   int pget_n;
   int pget_minchunk;

   xstring_c on_change;

   mode_t get_mode_mask();

   int source_redirections;
   int target_redirections;

   void HandleChdir(FileAccessRef& session, int &redirections);
   void HandleListInfoCreation(const FileAccessRef& session,SMTaskRef<ListInfo>& list_info,
	    const char *relative_dir);
   void HandleListInfo(SMTaskRef<ListInfo>& list_info,Ref<FileSet>& set,Ref<FileSet> *fsx=0);

   void MirrorStarted();
   void MirrorFinished();
   void TransferStarted(class CopyJob *cp);
   void JobStarted(Job *j);
   void TransferFinished(Job *j);
   void JobFinished(Job *j);

   off_t GetBytesCount();
   double GetTimeSpent();

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
      LOOP=1<<14,
      ONLY_EXISTING=1<<15,
      NO_EMPTY_DIRS=1<<16,
      DEPTH_FIRST=1<<17,
      ASCII=1<<18,
      SCAN_ALL_FIRST=1<<19,
      OVERWRITE=1<<20,
      UPLOAD_OLDER=1<<21,
      TRANSFER_ALL=1<<22,
      TARGET_FLAT=1<<23,
      DELETE_EXCLUDED=1<<24,
      REVERSE=1<<25,
   };
   void SetFlags(unsigned f,bool v)
   {
      if(v)
	 flags|=f;
      else
	 flags&=~f;
   }
   bool FlagsSet(unsigned f)   const { return (flags&f)==f; }
   bool FlagSet(unsigned f)    const { return (flags&f); }
   bool AnyFlagSet(unsigned f) const { return (flags&f); }

   MirrorJob(MirrorJob *parent,FileAccess *f,FileAccess *target,
      const char *new_source_dir,const char *new_target_dir);
   ~MirrorJob();

   int	 Do();
   int	 Done() { return state==DONE; }
   void	 ShowRunStatus(const SMTaskRef<StatusLine>&);
   xstring& FormatStatus(xstring&,int v,const char *);
   xstring& FormatShortStatus(xstring&);
   void	 SayFinal() { PrintStatus(0,""); }
   int	 ExitCode() { return stats.error_count!=0; }

   void	 SetExclude(PatternSet *x) { my_exclude=x; exclude=my_exclude; }
   void	 SetExclude(const PatternSet *x) { exclude=x; }
   void	 SetSizeRange(Range *r) { my_size_range=r; size_range=my_size_range; }
   void	 SetSizeRange(const Range *r) { size_range=r; }
   void	 SetTopExclude(PatternSet *x) { top_exclude=x; }

   void	 SetVerbose(int v) { verbose_report=v; }

   void	 CreateRemoteDir() { create_remote_dir=true; }

   void	 SetNewerThan(const char *file);
   void	 SetOlderThan(const char *file);

   void  UseCache(bool u) { use_cache=u; }
   void	 RemoveSourceFiles() { remove_source_files=true; }
   void	 RemoveSourceDirs() { remove_source_files=remove_source_dirs=true; }
   void	 SkipNoAccess() { skip_noaccess=true; }

   void  SetParallel(int p) { parallel=p; }
   void  SetPGet(int n) { pget_n=n; }

   void Fg();
   void Bg();

   const char *SetRecursionMode(const char *r);
   const char *SetScriptFile(const char *n);
   void	 ScriptOnly(bool yes=true)
      {
	 script_only=yes;
      }
   void SetMaxErrorCount(int ec) { max_error_count=ec; }
   void SetOnChange(const char *oc);
   static const char *AddPattern(Ref<PatternSet>& exclude,char opt,const char *optarg);
   static const char *AddPatternsFrom(Ref<PatternSet>& exclude,char opt,const char *file);

   FileAccess const& GetExecSession()
   {
      return FlagSet(REVERSE)?*target_session:*source_session;
   }
};

#endif//MIRRORJOB_H
