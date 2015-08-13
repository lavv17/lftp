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

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <mbswidth.h>
#include "MirrorJob.h"
#include "CmdExec.h"
#include "rmJob.h"
#include "mvJob.h"
#include "ChmodJob.h"
#include "mkdirJob.h"
#include "misc.h"
#include "plural.h"
#include "FindJob.h"
#include "url.h"
#include "CopyJob.h"
#include "pgetJob.h"
#include "log.h"

#define set_state(s) do { state=(s); \
   Log::global->Format(11,"mirror(%p) enters state %s\n", this, #s); } while(0)
#define waiting_num waiting.count()

xstring& MirrorJob::FormatStatus(xstring& s,int v,const char *tab)
{
   if(Done())
      goto final;

   switch(state)
   {
   case(INITIAL_STATE):
   case(FINISHING):
   case(DONE):
   case(WAITING_FOR_TRANSFER):
   case(TARGET_REMOVE_OLD):
   case(TARGET_REMOVE_OLD_FIRST):
   case(TARGET_CHMOD):
   case(TARGET_MKDIR):
   case(SOURCE_REMOVING_SAME):
   case(LAST_EXEC):
      break;

   case(MAKE_TARGET_DIR):
      s.appendf("\tmkdir `%s' [%s]\n",target_dir.get(),target_session->CurrentStatus());
      break;

   case(CHANGING_DIR_SOURCE):
   case(CHANGING_DIR_TARGET):
      if(target_session->IsOpen())
	 s.appendf("\tcd `%s' [%s]\n",target_dir.get(),target_session->CurrentStatus());
      if(source_session->IsOpen())
	 s.appendf("\tcd `%s' [%s]\n",source_dir.get(),source_session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(target_list_info)
      {
	 if(target_relative_dir)
	    s.appendf("\t%s: %s\n",target_relative_dir.get(),target_list_info->Status());
	 else
	    s.appendf("\t%s\n",target_list_info->Status());
      }
      if(source_list_info)
      {
	 if(source_relative_dir)
	    s.appendf("\t%s: %s\n",source_relative_dir.get(),source_list_info->Status());
	 else
	    s.appendf("\t%s\n",source_list_info->Status());
      }
      break;
   }
   return s;

final:
   if(stats.dirs>0)
      s.appendf(plural("%sTotal: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
		     stats.dirs,stats.tot_files,stats.tot_symlinks),
	 tab,stats.dirs,stats.tot_files,stats.tot_symlinks);
   if(stats.new_files || stats.new_symlinks)
      s.appendf(plural("%sNew: %d file$|s$, %d symlink$|s$\n",
		     stats.new_files,stats.new_symlinks),
	 tab,stats.new_files,stats.new_symlinks);
   if(stats.mod_files || stats.mod_symlinks)
      s.appendf(plural("%sModified: %d file$|s$, %d symlink$|s$\n",
		     stats.mod_files,stats.mod_symlinks),
	 tab,stats.mod_files,stats.mod_symlinks);
   if(stats.bytes)
      s.appendf("%s%s\n",tab,CopyJob::FormatBytesTimeRate(stats.bytes,transfer_time_elapsed));
   if(stats.del_dirs || stats.del_files || stats.del_symlinks)
      s.appendf(plural(flags&DELETE ?
	       "%sRemoved: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n"
	      :"%sTo be removed: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
	      stats.del_dirs,stats.del_files,stats.del_symlinks),
	 tab,stats.del_dirs,stats.del_files,stats.del_symlinks);
   if(stats.error_count)
      s.appendf(plural("%s%d error$|s$ detected\n",stats.error_count),
	       tab,stats.error_count);
   return s;
}

void  MirrorJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   int w=s->GetWidthDelayed();
   switch(state)
   {
   case(INITIAL_STATE):
      break;

   // these have a sub-job
   case(WAITING_FOR_TRANSFER):
   case(TARGET_REMOVE_OLD):
   case(TARGET_REMOVE_OLD_FIRST):
   case(TARGET_CHMOD):
   case(TARGET_MKDIR):
   case(SOURCE_REMOVING_SAME):
   case(FINISHING):
   case(DONE):
   case(LAST_EXEC):
      Job::ShowRunStatus(s);
      break;

   case(MAKE_TARGET_DIR):
      s->Show("mkdir `%s' [%s]",target_dir.get(),target_session->CurrentStatus());
      break;

   case(CHANGING_DIR_SOURCE):
   case(CHANGING_DIR_TARGET):
      if(target_session->IsOpen() && (!source_session->IsOpen() || now%4>=2))
	 s->Show("cd `%s' [%s]",target_dir.get(),target_session->CurrentStatus());
      else if(source_session->IsOpen())
	 s->Show("cd `%s' [%s]",source_dir.get(),source_session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(target_list_info && (!source_list_info || now%4>=2))
      {
	 const char *status=target_list_info->Status();
	 int status_w=mbswidth(status, 0);
	 int dw=w-status_w;
	 if(dw<20)
	    dw=20;
	 if(target_relative_dir)
	    s->Show("%s: %s",squeeze_file_name(target_relative_dir,dw),status);
	 else
	    s->Show("%s",status);
      }
      else if(source_list_info)
      {
	 const char *status=source_list_info->Status();
	 int status_w=mbswidth(status, 0);
	 int dw=w-status_w;
	 if(dw<20)
	    dw=20;
	 if(source_relative_dir)
	    s->Show("%s: %s",squeeze_file_name(source_relative_dir,dw),status);
	 else
	    s->Show("%s",status);
      }
      break;
   }
}

xstring& MirrorJob::FormatShortStatus(xstring& s)
{
   if(bytes_to_transfer>0 && (!parent_mirror || parent_mirror->bytes_to_transfer!=bytes_to_transfer)) {
      long long curr_bytes_transferred=GetBytesCount();
      if(parent_mirror)
         curr_bytes_transferred+=bytes_transferred;
      s.appendf("%s/%s (%d%%)",
	 xhuman(curr_bytes_transferred),xhuman(bytes_to_transfer),
	 percent(curr_bytes_transferred,bytes_to_transfer));
      double rate=GetTransferRate();
      if(rate>=1)
	 s.append(' ').append(Speedometer::GetStrProper(rate));
   }
   return s;
}

void MirrorJob::TransferStarted(CopyJob *cp)
{
   if(transfer_count==0)
      root_mirror->transfer_start_ts=now;
   JobStarted(cp);
}
void MirrorJob::JobStarted(Job *j)
{
   AddWaiting(j);
   transfer_count++;
}
void MirrorJob::TransferFinished(Job *j)
{
   long long bytes_count=j->GetBytesCount();
   AddBytesTransferred(bytes_count);
   stats.bytes+=bytes_count;
   stats.time +=j->GetTimeSpent();
   if(j->ExitCode()==0 && verbose_report>=2) {
      xstring finished;
      const xstring& cmd=j->GetCmdLine();
      if(cmd[0]=='\\')
	 finished.append(cmd+1,cmd.length()-1);
      else
	 finished.append(cmd);
      const xstring& rate=Speedometer::GetStrProper(j->GetTransferRate());
      if(rate.length()>0)
	 finished.append(" (").append(rate).append(')');
      if(!(FlagSet(SCAN_ALL_FIRST) && finished.begins_with("mirror")))
	 Report(_("Finished %s"),finished.get());
   }
   JobFinished(j);
   if(transfer_count==0)
      root_mirror->transfer_time_elapsed += now-root_mirror->transfer_start_ts;
}
void MirrorJob::JobFinished(Job *j)
{
   if(j->ExitCode()!=0)
      stats.error_count++;
   RemoveWaiting(j);
   Delete(j);
   transfer_count--;
}

void  MirrorJob::HandleFile(FileInfo *file)
{
   int	 res;
   struct stat st;

   // TODO: get rid of local hacks.

   // dir_name returns pointer to static data - need to dup it.
   const char *source_name=dir_file(source_dir,file->name);
   source_name=alloca_strdup(source_name);
   const char *target_name=dir_file(target_dir,file->name);
   target_name=alloca_strdup(target_name);

   const char *source_name_rel=dir_file(source_relative_dir,file->name);
   source_name_rel=alloca_strdup(source_name_rel);
   const char *target_name_rel=dir_file(target_relative_dir,file->name);
   target_name_rel=alloca_strdup(target_name_rel);

   FileInfo::type filetype=FileInfo::NORMAL;
   if(file->Has(file->TYPE))
      filetype=file->filetype;
   else
   {
      FileInfo *target=target_set->FindByName(file->name);
      if(target && target->Has(target->TYPE))
	 filetype=target->filetype;
   }

   switch(filetype)
   {
      case(FileInfo::NORMAL):
      {
	 bool remove_target=false;
	 bool cont_this=false;
	 bool use_pget=(pget_n>1) && target_is_local;
	 if(file->Has(file->SIZE) && file->size<pget_minchunk*2)
	    use_pget=false;
	 if(target_is_local)
	 {
	    if(lstat(target_name,&st)!=-1)
	    {
	       // few safety checks.
	       FileInfo *old=new_files_set->FindByName(file->name);
	       if(old)
		  goto skip;  // file has appeared after mirror start
	       old=old_files_set->FindByName(file->name);
	       if(old && ((old->Has(old->SIZE) && old->size!=st.st_size)
			||(old->Has(old->DATE) && old->date!=st.st_mtime)))
		  goto skip;  // the file has changed after mirror start
	    }
	 }
	 FileInfo *old=target_set->FindByName(file->name);
	 if(old)
	 {
	    if((flags&CONTINUE)
	    && old->Has(file->TYPE) && old->filetype==old->NORMAL
	    && (flags&IGNORE_TIME ||
	    	(file->Has(file->DATE) && old->Has(old->DATE)
	    	&& file->date + file->date.ts_prec < old->date - old->date.ts_prec))
	    && file->Has(file->SIZE) && old->Has(old->SIZE)
	    && file->size >= old->size)
	    {
	       cont_this=true;
	       if(target_is_local && !script_only)
	       {
		  if(access(target_name,W_OK)==-1)
		  {
		     // try to enable write access.
		     chmod(target_name,st.st_mode|0200);
		  }
	       }
	       stats.mod_files++;
	    }
	    else if(!to_rm_mismatched->FindByName(file->name))
	    {
	       if(!FlagSet(OVERWRITE)) {
		  remove_target=true;
		  Report(_("Removing old file `%s'"),target_name_rel);
	       } else {
		  Report(_("Overwriting old file `%s'"),target_name_rel);
	       }
	       stats.mod_files++;
	    }
	    else
	       stats.new_files++;
	 }
	 else if(flags&ONLY_EXISTING)
	 {
	    Report(_("Skipping file `%s' (only-existing)"),source_name_rel);
	    goto skip;
	 }
	 else
	    stats.new_files++;

	 Report(_("Transferring file `%s'"),source_name_rel);

	 if(script)
	 {
	    ArgV args(use_pget?"pget":"get");
	    if(use_pget)
	    {
	       args.Append("-n");
	       args.Append(pget_n);
	    }
	    if(cont_this)
	       args.Append("-c");
	    if(remove_target)
	       args.Append("-e");
	    if(FlagSet(ASCII))
	       args.Append("-a");
	    if(remove_source_files)
	       args.Append("-E");
	    args.Append("-O");
	    args.Append(target_is_local?target_dir.get()
			:target_session->GetConnectURL());
	    args.Append(source_session->GetFileURL(file->name));
	    xstring_ca cmd(args.CombineQuoted());
	    fprintf(script,"%s\n",cmd.get());
	    if(script_only)
	       goto skip;
	 }

	 FileCopyPeer *src_peer=0;
	 if(source_is_local)
	    src_peer=new FileCopyPeerFDStream(new FileStream(source_name,O_RDONLY),FileCopyPeer::GET);
	 else
	    src_peer=new FileCopyPeerFA(source_session->Clone(),file->name,FA::RETRIEVE);

	 FileCopyPeer *dst_peer=0;
	 if(target_is_local)
	    dst_peer=new FileCopyPeerFDStream(new FileStream(target_name,O_WRONLY|O_CREAT|(cont_this?0:O_TRUNC)),FileCopyPeer::PUT);
	 else
	    dst_peer=new FileCopyPeerFA(target_session->Clone(),file->name,FA::STORE);

	 FileCopy *c=FileCopy::New(src_peer,dst_peer,cont_this);
	 if(remove_source_files)
	    c->RemoveSourceLater();
	 if(remove_target)
	    c->RemoveTargetFirst();
	 if(FlagSet(ASCII))
	    c->Ascii();
	 CopyJob *cp=(use_pget ? new pgetJob(c,file->name,pget_n) : new CopyJob(c,file->name,"mirror"));
	 if(file->Has(file->DATE))
	    cp->SetDate(file->date);
	 if(file->Has(file->SIZE))
	    cp->SetSize(file->size);
	 TransferStarted(cp);
	 cp->cmdline.vset("\\transfer `",source_name_rel,"'",NULL);

	 set_state(WAITING_FOR_TRANSFER);
	 break;
      }
      case(FileInfo::DIRECTORY):
      {
	 if(recursion_mode==RECURSION_NEVER || FlagSet(NO_RECURSION))
	    goto skip;

	 bool create_target_dir=true;
	 const FileInfo *old=0;
	 if(target_set)
	    old=target_set->FindByName(file->name);
	 if(!old)
	 {
	    if(flags&ONLY_EXISTING)
	    {
	       Report(_("Skipping directory `%s' (only-existing)"),target_name_rel);
	       goto skip;
	    }
	 }
	 else if(old->TypeIs(old->DIRECTORY))
	 {
	    create_target_dir=false;
	 }
	 if(target_is_local && !script_only)
	 {
	    if((flags&RETR_SYMLINKS?stat:lstat)(target_name,&st)!=-1)
	    {
	       if(S_ISDIR(st.st_mode))
	       {
		  chmod(target_name,st.st_mode|0700);
		  create_target_dir=false;
	       }
	       else
	       {
		  Report(_("Removing old local file `%s'"),target_name_rel);
		  if(remove(target_name)==-1)
		  {
		     eprintf("mirror: remove(%s): %s\n",target_name,strerror(errno));
		     goto skip;
		  }
		  create_target_dir=true;
	       }
	    }
	 }

	 // launch sub-mirror
	 MirrorJob *mj=new MirrorJob(this,
	    source_session->Clone(),target_session->Clone(),
	    source_name,target_name);
	 AddWaiting(mj);
	 mj->cmdline.vset("\\mirror `",source_name_rel,"'",NULL);

	 // inherit flags and other things
	 mj->SetFlags(flags,1);
	 mj->UseCache(use_cache);

	 mj->SetExclude(exclude);

	 mj->source_relative_dir.set(source_name_rel);
	 mj->target_relative_dir.set(target_name_rel);

	 mj->verbose_report=verbose_report;
	 mj->newer_than=newer_than;
	 mj->older_than=older_than;
	 mj->size_range=size_range;
	 mj->parallel=parallel;
	 mj->pget_n=pget_n;
	 mj->pget_minchunk=pget_minchunk;
	 mj->remove_source_files=remove_source_files;
	 mj->skip_noaccess=skip_noaccess;
	 mj->create_target_dir=create_target_dir;
	 mj->no_target_dir=no_target_dir;
	 mj->recursion_mode=recursion_mode;

	 mj->script=script;
	 mj->script_needs_closing=false;
	 mj->script_name.set(script_name);
	 mj->script_only=script_only;

	 mj->max_error_count=max_error_count;

	 if(verbose_report>=3) {
	    if(FlagSet(SCAN_ALL_FIRST))
	       Report(_("Scanning directory `%s'"),mj->target_relative_dir.get());
	    else
	       Report(_("Mirroring directory `%s'"),mj->target_relative_dir.get());
	 }

	 break;
      }
      case(FileInfo::SYMLINK):
      {
	 if(flags&NO_SYMLINKS)
	    goto skip;

	 if(!file->symlink)
	    goto skip;

	 if(!target_is_local)
	 {
	    if(script)
	    {
	       ArgV args("ln");
	       args.Append("-s");
	       args.Append(file->symlink);
	       args.Append(target_name);
	       xstring_ca cmd(args.CombineQuoted());
	       fprintf(script,"%s\n",cmd.get());
	       if(script_only)
		  goto skip;
	    }
	    bool remove_target=false;
	    FileInfo *old=target_set->FindByName(file->name);
	    if(old && !to_rm_mismatched->FindByName(file->name))
	    {
	       Report(_("Removing old file `%s'"),target_name_rel);
	       remove_target=true;
	       stats.mod_symlinks++;
	    }
	    else
	       stats.new_symlinks++;
	    mvJob *j=new mvJob(target_session->Clone(),file->symlink,target_name,FA::SYMLINK);
	    if(remove_target)
	       j->RemoveTargetFirst();
	    AddWaiting(j);
	    break;
	 }

	 if(script)
	 {
	    ArgV args("shell");
	    args.Append("ln");
	    args.Append("-sf");
	    args.Append(shell_encode(file->symlink));
	    args.Append(shell_encode(target_name));
	    xstring_ca cmd(args.CombineQuoted());
	    fprintf(script,"%s\n",cmd.get());
	    if(script_only)
	       goto skip;
	 }

	 if(file->Has(file->SYMLINK))
	 {
	    struct stat st;
	    if(lstat(target_name,&st)!=-1)
	    {
	       Report(_("Removing old local file `%s'"),target_name_rel);
	       stats.mod_symlinks++;
	       if(remove(target_name)==-1)
	       {
		  eprintf("mirror: remove(%s): %s\n",target_name,strerror(errno));
		  goto skip;
	       }
	    }
	    else
	    {
	       if(flags&ONLY_EXISTING)
	       {
		  Report(_("Skipping symlink `%s' (only-existing)"),target_name_rel);
		  goto skip;
	       }
	       stats.new_symlinks++;
	    }
	    Report(_("Making symbolic link `%s' to `%s'"),target_name_rel,file->symlink.get());
	    res=symlink(file->symlink,target_name);
	    if(res==-1)
	       eprintf("mirror: symlink(%s): %s\n",target_name,strerror(errno));
	 }
	 break;
      }
   case FileInfo::UNKNOWN:
      break;
   }
skip:
   return;
}

void  MirrorJob::InitSets(const FileSet *source,const FileSet *dest)
{
   source->Count(NULL,&stats.tot_files,&stats.tot_symlinks,&stats.tot_files);

   to_rm=new FileSet(dest);
   to_rm->SubtractAny(source);

   to_transfer=new FileSet(source);

   if(!FlagSet(TRANSFER_ALL)) {
      same=new FileSet(source);

      int ignore=0;
      if(flags&ONLY_NEWER)
	 ignore|=FileInfo::IGNORE_SIZE_IF_OLDER|FileInfo::IGNORE_DATE_IF_OLDER;
      if(!FlagSet(UPLOAD_OLDER) && strcmp(target_session->GetProto(),"file"))
	 ignore|=FileInfo::IGNORE_DATE_IF_OLDER;
      if(flags&IGNORE_TIME)
	 ignore|=FileInfo::DATE;
      if(flags&IGNORE_SIZE)
	 ignore|=FileInfo::SIZE;
      to_transfer->SubtractSame(dest,ignore);

      same->SubtractAny(to_transfer);
   }

   if(newer_than!=NO_DATE)
      to_transfer->SubtractNotNewerThan(newer_than);
   if(older_than!=NO_DATE)
      to_transfer->SubtractNotOlderThan(older_than);
   if(size_range)
      to_transfer->SubtractSizeOutside(size_range);

   if(FlagSet(SCAN_ALL_FIRST)) {
      to_mkdir=new FileSet(to_transfer);
      to_mkdir->SubtractNotDirs();
      to_mkdir->SubtractAny(dest);
   }

   switch(recursion_mode) {
   case RECURSION_NEVER:
      to_transfer->SubtractDirs();
      break;
   case RECURSION_MISSING:
      to_transfer->SubtractDirs(dest);
      break;
   case RECURSION_NEWER:
      to_transfer->SubtractNotOlderDirs(dest);
      break;
   case RECURSION_ALWAYS:
      break;
   }

   if(skip_noaccess)
      to_transfer->ExcludeUnaccessible(source_session->GetUser());

   new_files_set=new FileSet(to_transfer);
   new_files_set->SubtractAny(dest);
   old_files_set=new FileSet(dest);
   old_files_set->SubtractNotIn(to_transfer);

   to_rm_mismatched=new FileSet(old_files_set);
   to_rm_mismatched->SubtractSameType(to_transfer);
   to_rm_mismatched->SubtractNotDirs();

   if(!(flags&DELETE))
      to_transfer->SubtractAny(to_rm_mismatched);

   const char *sort_by=ResMgr::Query("mirror:sort-by",0);
   bool desc=strstr(sort_by,"-desc");
   if(!strncmp(sort_by,"name",4))
      to_transfer->SortByPatternList(ResMgr::Query("mirror:order",0));
   else if(!strncmp(sort_by,"date",4))
      to_transfer->Sort(FileSet::BYDATE);
   else if(!strncmp(sort_by,"size",4))
      to_transfer->Sort(FileSet::BYSIZE,false,true);
   if(desc)
      to_transfer->ReverseSort();

   int dir_count=0;
   if(to_mkdir) {
      to_mkdir->Count(&dir_count,NULL,NULL,NULL);
      only_dirs = (dir_count==to_mkdir->count());
   } else {
      to_transfer->Count(&dir_count,NULL,NULL,NULL);
      only_dirs = (dir_count==to_transfer->count());
   }
}

void MirrorJob::HandleChdir(FileAccessRef& session, int &redirections)
{
   if(!session->IsOpen())
      return;
   int res=session->Done();
   if(res<0)
   {
      if(res==FA::FILE_MOVED)
      {
	 // cd to another url.
	 const char *loc_c=session->GetNewLocation();
	 int max_redirections=ResMgr::Query("xfer:max-redirections",0);
	 if(loc_c && max_redirections>0)
	 {
	    if(++redirections>max_redirections)
	       goto cd_err_normal;
	    eprintf(_("%s: received redirection to `%s'\n"),"mirror",loc_c);

	    char *loc=alloca_strdup(loc_c);
	    ParsedURL u(loc,true);

	    bool is_file=(last_char(loc)!='/');
	    if(!u.proto)
	    {
	       FileAccess::Path new_cwd(session->GetNewCwd());
	       new_cwd.Change(0,is_file,loc);
	       session->PathVerify(new_cwd);
	       session->Roll();
	       return;
	    }
	    session->Close(); // loc_c is no longer valid.
	    session=FA::New(&u);
	    FileAccess::Path new_cwd(u.path,is_file,url::path_ptr(loc));
	    session->PathVerify(new_cwd);
	    return;
	 }
      }
   cd_err_normal:
      if(session==target_session && (script_only || FlagSet(SCAN_ALL_FIRST)))
      {
	 char *dir=alloca_strdup(session->GetFile());
	 session->Close();
	 session->Chdir(dir,false);
	 no_target_dir=true;
	 return;
      }
      eprintf("mirror: %s\n",session->StrError(res));
      stats.error_count++;
      transfer_count-=root_transfer_count;
      set_state(FINISHING);
      source_session->Close();
      target_session->Close();
      return;
   }
   if(res==FA::OK)
      session->Close();
}
void MirrorJob::HandleListInfoCreation(const FileAccessRef& session,SMTaskRef<ListInfo>& list_info,const char *relative_dir)
{
   if(state!=GETTING_LIST_INFO)
      return;

   if(session==target_session && no_target_dir)
   {
      target_set=new FileSet();
      return;
   }

   list_info=session->MakeListInfo();
   if(list_info==0)
   {
      eprintf(_("mirror: protocol `%s' is not suitable for mirror\n"),
	       session->GetProto());
      transfer_count-=root_transfer_count;
      set_state(FINISHING);
      return;
   }
   list_info->UseCache(use_cache);
   int need=FileInfo::ALL_INFO;
   if(flags&IGNORE_TIME)
      need&=~FileInfo::DATE;
   if(flags&IGNORE_SIZE)
      need&=~FileInfo::SIZE;
   list_info->Need(need);
   if(flags&RETR_SYMLINKS)
      list_info->FollowSymlinks();

   list_info->SetExclude(relative_dir,exclude);
   list_info->Roll();
}

void MirrorJob::HandleListInfo(SMTaskRef<ListInfo>& list_info, Ref<FileSet>& set)
{
   if(!list_info)
      return;
   if(!list_info->Done())
      return;
   if(list_info->Error())
   {
      eprintf("mirror: %s\n",list_info->ErrorText());
      stats.error_count++;
      transfer_count-=root_transfer_count;
      set_state(FINISHING);
      source_list_info=0;
      target_list_info=0;
      return;
   }
   set=list_info->GetResult();
   list_info=0;
   set->ExcludeDots(); // don't need .. and .
}

int   MirrorJob::Do()
{
   int	 res;
   int	 m=STALL;
   FileInfo *file;
   Job	 *j;

   switch(state)
   {
   case(INITIAL_STATE):
      source_session->Chdir(source_dir);
      source_redirections=0;
      source_session->Roll();
      set_state(CHANGING_DIR_SOURCE);
      m=MOVED;
      /*fallthrough*/
   case(CHANGING_DIR_SOURCE):
      HandleChdir(source_session,source_redirections);
      if(state!=CHANGING_DIR_SOURCE)
	 return MOVED;
      if(source_session->IsOpen())
	 return m;

      source_dir.set(source_session->GetCwd().GetDirectory());

   pre_MAKE_TARGET_DIR:
   {
      if(!strcmp(target_dir,".") || !strcmp(target_dir,"..") || (FlagSet(SCAN_ALL_FIRST) && parent_mirror))
	 create_target_dir=false;
      if(!create_target_dir)
	 goto pre_CHANGING_DIR_TARGET;
      if(target_is_local)
      {
	 struct stat st;
	 if((flags&RETR_SYMLINKS?stat:lstat)(target_dir,&st)!=-1)
	 {
	    if(S_ISDIR(st.st_mode))
	    {
	       if(!script_only)
		  chmod(target_dir,st.st_mode|0700);
	       create_target_dir=false;
	       goto pre_CHANGING_DIR_TARGET;
	    }
	    else
	    {
	       Report(_("Removing old local file `%s'"),target_dir.get());
	       if(script)
	       {
		  ArgV args("rm");
		  args.Append(target_session->GetFileURL(target_dir));
		  xstring_ca cmd(args.CombineQuoted());
		  fprintf(script,"%s\n",cmd.get());
	       }
	       if(!script_only)
	       {
		  if(remove(target_dir)==-1)
		     eprintf("mirror: remove(%s): %s\n",target_dir.get(),strerror(errno));
	       }
	    }
	 }
      }

      if(FlagSet(DEPTH_FIRST))
	 goto pre_GETTING_LIST_INFO;

      if(target_relative_dir)
	 Report(_("Making directory `%s'"),target_relative_dir.get());
      bool mkdir_p=(parent_mirror==0 || parent_mirror->create_target_dir);
      if(script)
      {
	 ArgV args("mkdir");
	 if(mkdir_p)
	    args.Append("-p");
	 args.Append(target_session->GetFileURL(target_dir));
	 xstring_ca cmd(args.CombineQuoted());
	 fprintf(script,"%s\n",cmd.get());
	 if(script_only)
	    goto pre_CHANGING_DIR_TARGET;
      }
      target_session->Mkdir(target_dir,mkdir_p);
      set_state(MAKE_TARGET_DIR);
      m=MOVED;
   }
      /*fallthrough*/
   case(MAKE_TARGET_DIR):
      res=target_session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      target_session->Close();
      create_target_dir=false;

   pre_CHANGING_DIR_TARGET:
      target_session->Chdir(target_dir);
      target_redirections=0;
      target_session->Roll();
      set_state(CHANGING_DIR_TARGET);
      m=MOVED;
      /*fallthrough*/
   case(CHANGING_DIR_TARGET):
      HandleChdir(target_session,target_redirections);
      if(state!=CHANGING_DIR_TARGET)
	 return MOVED;
      if(target_session->IsOpen())
	 return m;
      create_target_dir=false;

      target_dir.set(target_session->GetCwd().GetDirectory());

   pre_GETTING_LIST_INFO:
      set_state(GETTING_LIST_INFO);
      m=MOVED;
      if(!source_set)
	 HandleListInfoCreation(source_session,source_list_info,source_relative_dir);
      if(!target_set && !create_target_dir && (!FlagSet(DEPTH_FIRST) || FlagSet(ONLY_EXISTING)))
	 HandleListInfoCreation(target_session,target_list_info,target_relative_dir);
      if(state!=GETTING_LIST_INFO)
      {
	 source_list_info=0;
	 target_list_info=0;
      }
      return m;	  // give time to other tasks
   case(GETTING_LIST_INFO):
      HandleListInfo(source_list_info,source_set);
      HandleListInfo(target_list_info,target_set);
      if(state!=GETTING_LIST_INFO)
	 return MOVED;
      if(source_list_info || target_list_info)
	 return m;

      transfer_count-=root_transfer_count; // leave room for transfers.

      if(FlagSet(DEPTH_FIRST) && source_set && !target_set)
      {
	 // transfer directories first
	 InitSets(source_set,NULL);
	 to_transfer->Unsort();
	 to_transfer->SubtractNotDirs();
	 goto pre_WAITING_FOR_TRANSFER;
      }

      // now we have both target and source file sets.
      if(parent_mirror)
	 stats.dirs++;

      if(FlagSet(SCAN_ALL_FIRST) && parent_mirror)
      {
	 source_set->PrependPath(source_relative_dir);
	 if(root_mirror->source_set_recursive)
	    root_mirror->source_set_recursive->Merge(source_set);
	 else
	    root_mirror->source_set_recursive=source_set.borrow();
	 if(target_set) {
	    target_set->PrependPath(target_relative_dir);
	    if(root_mirror->target_set_recursive)
	       root_mirror->target_set_recursive->Merge(target_set);
	    else
	       root_mirror->target_set_recursive=target_set.borrow();
	 }
	 root_mirror->stats.dirs++;
	 goto pre_DONE;
      }

      if(source_set_recursive) {
	 source_set->Merge(source_set_recursive);
	 source_set_recursive=0;
      }
      if(target_set_recursive) {
	 target_set->Merge(target_set_recursive);
	 target_set_recursive=0;
      }
      InitSets(source_set,target_set);

      to_transfer->CountBytes(&bytes_to_transfer);
      if(parent_mirror)
	 parent_mirror->AddBytesToTransfer(bytes_to_transfer);

      to_rm->Count(&stats.del_dirs,&stats.del_files,&stats.del_symlinks,&stats.del_files);
      to_rm->rewind();
      to_rm_mismatched->Count(&stats.del_dirs,&stats.del_files,&stats.del_symlinks,&stats.del_files);
      to_rm_mismatched->rewind();

      set_state(TARGET_REMOVE_OLD_FIRST);
      goto TARGET_REMOVE_OLD_FIRST_label;

   pre_TARGET_MKDIR:
      if(!to_mkdir)
	 goto pre_WAITING_FOR_TRANSFER;
      to_mkdir->rewind();
      set_state(TARGET_MKDIR);
      m=MOVED;
      /*fallthrough*/
   case(TARGET_MKDIR):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 JobFinished(j);
	 m=MOVED;
      }
      if(max_error_count>0 && stats.error_count>=max_error_count)
	 goto pre_FINISHING;
      while(transfer_count<parallel && state==TARGET_MKDIR)
      {
	 file=to_mkdir->curr();
	 if(!file)
	    goto pre_WAITING_FOR_TRANSFER;
	 to_mkdir->next();
	 if(!file->TypeIs(file->DIRECTORY))
	    continue;
	 if(script)
	    fprintf(script,"mkdir %s\n",target_session->GetFileURL(file->name));
	 if(!script_only)
	 {
	    ArgV *a=new ArgV("mkdir");
	    a->Append(file->name);
	    mkdirJob *mkj=new mkdirJob(target_session->Clone(),a);
	    mkj->cmdline.set_allocated(a->Combine());
	    JobStarted(mkj);
	    m=MOVED;
	 }
      }
      break;

   pre_WAITING_FOR_TRANSFER:
      to_transfer->rewind();
      set_state(WAITING_FOR_TRANSFER);
      m=MOVED;
      /*fallthrough*/
   case(WAITING_FOR_TRANSFER):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 TransferFinished(j);
	 m=MOVED;
      }
      if(max_error_count>0 && stats.error_count>=max_error_count)
	 goto pre_FINISHING;
      while(transfer_count<parallel && state==WAITING_FOR_TRANSFER)
      {
	 file=to_transfer->curr();
      	 if(!file)
	 {
	    // go to the next step only when all transfers have finished
	    if(waiting_num>0)
	       break;
	    if(FlagSet(DEPTH_FIRST))
	    {
	       // we have been in the depth, don't go there again
	       SetFlags(DEPTH_FIRST,false);
	       SetFlags(NO_RECURSION,true);

	       // if we have not created any subdirs and there are only subdirs,
	       // then the directory would be empty - skip it.
	       if(FlagSet(NO_EMPTY_DIRS) && stats.dirs==0 && only_dirs)
		  goto pre_FINISHING_FIX_LOCAL;

	       transfer_count+=root_transfer_count;
	       goto pre_MAKE_TARGET_DIR;
	    }
	    goto pre_TARGET_REMOVE_OLD;
	 }
	 HandleFile(file);
	 to_transfer->next();
	 m=MOVED;
      }
      break;

   pre_TARGET_REMOVE_OLD:
      if(flags&REMOVE_FIRST)
	 goto pre_TARGET_CHMOD;
      set_state(TARGET_REMOVE_OLD);
      m=MOVED;
      /*fallthrough*/
   case(TARGET_REMOVE_OLD):
   case(TARGET_REMOVE_OLD_FIRST):
   TARGET_REMOVE_OLD_FIRST_label:
      while((j=FindDoneAwaitedJob())!=0)
      {
	 JobFinished(j);
	 m=MOVED;
      }
      if(max_error_count>0 && stats.error_count>=max_error_count)
	 goto pre_FINISHING;
      while(transfer_count<parallel && (state==TARGET_REMOVE_OLD || state==TARGET_REMOVE_OLD_FIRST))
      {
	 file=0;
	 if(!file && state==TARGET_REMOVE_OLD_FIRST)
	 {
	    file=to_rm_mismatched->curr();
	    to_rm_mismatched->next();
	 }
	 if(!file && (state==TARGET_REMOVE_OLD || (flags&REMOVE_FIRST)))
	 {
	    file=to_rm->curr();
	    to_rm->next();
	 }
	 if(!file)
	 {
	    if(waiting_num>0)
	       break;
	    if(state==TARGET_REMOVE_OLD)
	       goto pre_TARGET_CHMOD;
	    goto pre_TARGET_MKDIR;
	 }
	 const char *target_name_rel=dir_file(target_relative_dir,file->name);
	 target_name_rel=alloca_strdup(target_name_rel);
	 if(!(flags&DELETE))
	 {
	    if(flags&REPORT_NOT_DELETED)
	    {
	       if(file->TypeIs(file->DIRECTORY))
		  Report(_("Old directory `%s' is not removed"),target_name_rel);
	       else
		  Report(_("Old file `%s' is not removed"),target_name_rel);
	    }
	    continue;
	 }
	 if(script)
	 {
	    ArgV args("rm");
	    if(file->TypeIs(file->DIRECTORY))
	    {
	       if(recursion_mode==RECURSION_NEVER)
		  args.setarg(0,"rmdir");
	       else
		  args.Append("-r");
	    }
	    args.Append(target_session->GetFileURL(file->name));
	    xstring_ca cmd(args.CombineQuoted());
	    fprintf(script,"%s\n",cmd.get());
	 }
	 if(!script_only)
	 {
	    ArgV *args=new ArgV("rm");
	    args->Append(file->name);
	    args->seek(1);
	    rmJob *j=new rmJob(target_session->Clone(),args);
	    j->cmdline.set_allocated(args->Combine());
	    JobStarted(j);
	    if(file->TypeIs(file->DIRECTORY))
	    {
	       if(recursion_mode==RECURSION_NEVER)
	       {
		  args->setarg(0,"rmdir");
		  j->Rmdir();
	       }
	       else
		  j->Recurse();
	    }
	 }
	 if(file->TypeIs(file->DIRECTORY))
	    Report(_("Removing old directory `%s'"),target_name_rel);
	 else
	    Report(_("Removing old file `%s'"),target_name_rel);
      }
      break;

   pre_TARGET_CHMOD:
      if(flags&NO_PERMS)
	 goto pre_FINISHING_FIX_LOCAL;

      to_transfer->rewind();
      set_state(TARGET_CHMOD);
      m=MOVED;
      /*fallthrough*/
   case(TARGET_CHMOD):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 JobFinished(j);
	 m=MOVED;
      }
      if(max_error_count>0 && stats.error_count>=max_error_count)
	 goto pre_FINISHING;
      while(transfer_count<parallel && state==TARGET_CHMOD)
      {
	 file=to_transfer->curr();
	 if(!file)
	    goto pre_FINISHING_FIX_LOCAL;
	 to_transfer->next();
	 if(file->TypeIs(file->SYMLINK))
	    continue;
	 if(!file->Has(file->MODE))
	    continue;
	 mode_t mode_mask=get_mode_mask();
	 mode_t def_mode=(file->TypeIs(file->DIRECTORY)?0775:0664)&~mode_mask;
	 if(target_is_local && file->mode==def_mode)
	 {
	    struct stat st;
	    if(!target_is_local || lstat(dir_file(target_dir,file->name),&st)==-1)
	       continue;
	    if((st.st_mode&07777)==(file->mode&~mode_mask))
	       continue;
	 }
	 FileInfo *target=target_set->FindByName(file->name);
	 if(target && target->filetype==file->DIRECTORY && file->filetype==file->DIRECTORY
	 && target->mode==(file->mode&~mode_mask) && (target->mode&0200))
	    continue;
	 if(script)
	 {
	    ArgV args("chmod");
	    args.Append(xstring::format("%03lo",(unsigned long)(file->mode&~mode_mask)));
	    args.Append(target_session->GetFileURL(file->name));
	    xstring_ca cmd(args.CombineQuoted());
	    fprintf(script,"%s\n",cmd.get());
	 }
	 if(!script_only)
	 {
	    ArgV *a=new ArgV("chmod");
	    a->Append(file->name);
	    a->seek(1);
	    ChmodJob *cj=new ChmodJob(target_session->Clone(),
				 file->mode&~mode_mask,a);
	    cj->cmdline.set_allocated(a->Combine());
	    cj->BeQuiet(); // chmod is not supported on all servers; be quiet.
	    JobStarted(cj);
	    m=MOVED;
	 }
      }
      break;

   pre_FINISHING_FIX_LOCAL:
      if(target_is_local && !script_only)     // FIXME
      {
	 to_transfer->LocalUtime(target_dir,/*only_dirs=*/true);
	 if(flags&ALLOW_CHOWN)
	    to_transfer->LocalChown(target_dir);
	 if(!FlagSet(NO_PERMS) && same)
	    same->LocalChmod(target_dir,get_mode_mask());
	 if(FlagSet(ALLOW_CHOWN) && same)
	    same->LocalChown(target_dir);
      }
      if(remove_source_files && same)
	 goto pre_SOURCE_REMOVING_SAME;
   pre_FINISHING:
      set_state(FINISHING);
      m=MOVED;
      /*fallthrough*/
   case(FINISHING):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 JobFinished(j);
	 m=MOVED;
      }
      if(waiting_num>0)
	 break;

      transfer_count++; // parent mirror will decrement it.
      if(parent_mirror)
	 parent_mirror->stats.Add(stats);
      else
      {
	 if(stats.HaveSomethingDone(flags) && on_change)
	 {
	    CmdExec *exec=new CmdExec(source_session->Clone(),0);
	    AddWaiting(exec);
	    exec->FeedCmd(on_change);
	    exec->FeedCmd("\n");
	    set_state(LAST_EXEC);
	    break;
	 }
      }
      goto pre_DONE;

   pre_SOURCE_REMOVING_SAME:
      same->rewind();
      set_state(SOURCE_REMOVING_SAME);
      m=MOVED;
      /*fallthrough*/
   case(SOURCE_REMOVING_SAME):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 JobFinished(j);
	 m=MOVED;
      }
      if(max_error_count>0 && stats.error_count>=max_error_count)
	 goto pre_FINISHING;
      while(transfer_count<parallel && state==SOURCE_REMOVING_SAME)
      {
	 file=same->curr();
	 same->next();
	 if(!file)
	    goto pre_FINISHING;
	 if(file->TypeIs(file->DIRECTORY))
	    continue;
	 const char *source_name_rel=dir_file(source_relative_dir,file->name);
	 source_name_rel=alloca_strdup(source_name_rel);
	 if(script)
	 {
	    ArgV args("rm");
	    args.Append(source_session->GetFileURL(file->name));
	    xstring_ca cmd(args.CombineQuoted());
	    fprintf(script,"%s\n",cmd.get());
	 }
	 if(!script_only)
	 {
	    ArgV *args=new ArgV("rm");
	    args->Append(file->name);
	    args->seek(1);
	    rmJob *j=new rmJob(source_session->Clone(),args);
	    j->cmdline.set_allocated(args->Combine());
	    JobStarted(j);
	 }
	 Report(_("Removing source file `%s'"),source_name_rel);
      }
      break;

   case(LAST_EXEC):
      while((j=FindDoneAwaitedJob())!=0)
      {
	 RemoveWaiting(j);
	 Delete(j);
	 m=MOVED;
      }
      if(waiting_num>0)
	 break;
   pre_DONE:
      set_state(DONE);
      m=MOVED;
      bytes_transferred=0;
      if(!parent_mirror && (flags&LOOP) && stats.HaveSomethingDone(flags) && !stats.error_count)
      {
	 PrintStatus(0,"");
	 printf(_("Retrying mirror...\n"));
	 stats.Reset();
	 source_set=0;
	 target_set=0;
	 goto pre_GETTING_LIST_INFO;
      }
      /*fallthrough*/
   case(DONE):
      break;
   }
   // give direct parent priority over grand-parents.
   if(transfer_count<parallel && parent_mirror)
      m|=parent_mirror->Roll();
   return m;
}

MirrorJob::MirrorJob(MirrorJob *parent,
   FileAccess *source,FileAccess *target,
   const char *new_source_dir,const char *new_target_dir)
 :
   bytes_transferred(0), bytes_to_transfer(0),
   source_dir(new_source_dir), target_dir(new_target_dir),
   transfer_time_elapsed(0), root_transfer_count(0),
   transfer_count(parent?parent->transfer_count:root_transfer_count),
   verbose_report(0),
   parent_mirror(parent), root_mirror(parent?parent->root_mirror:this)
{

   source_session=source;
   target_session=target;
   // TODO: get rid of this.
   source_is_local=!strcmp(source_session->GetProto(),"file");
   target_is_local=!strcmp(target_session->GetProto(),"file");

   create_target_dir=true;
   no_target_dir=false;

   flags=0;
   max_error_count=0;

   exclude=0;

   set_state(INITIAL_STATE);

   newer_than=NO_DATE;
   older_than=NO_DATE;
   size_range=0;

   script=0;
   script_only=false;
   script_needs_closing=false;

   use_cache=false;
   remove_source_files=false;
   skip_noaccess=false;

   parallel=1;
   pget_n=1;
   pget_minchunk=0x10000;

   source_redirections=0;
   target_redirections=0;

   if(parent_mirror)
   {
      bool parallel_dirs=ResMgr::QueryBool("mirror:parallel-directories",0);
      // If parallel_dirs is true, allow parent mirror to continue
      // processing other directories, otherwise block it until we
      // get file sets and start transfers.
      root_transfer_count=parallel_dirs?1:1024;
      transfer_count+=root_transfer_count;
   }
}

MirrorJob::~MirrorJob()
{
   if(script && script_needs_closing)
      fclose(script);
}

void MirrorJob::va_Report(const char *fmt,va_list v)
{
   if(parent_mirror)
   {
      parent_mirror->va_Report(fmt,v);
      return;
   }

   if(verbose_report)
   {
      pid_t p=tcgetpgrp(fileno(stdout));
      if(p>0 && p!=getpgrp())
	 return;

      vfprintf(stdout,fmt,v);
      printf("\n");
      fflush(stdout);
   }
}

void MirrorJob::Report(const char *fmt,...)
{
   va_list v;
   va_start(v,fmt);

   va_Report(fmt,v);

   va_end(v);
}

extern "C" {
#include "parse-datetime.h"
}
void MirrorJob::SetNewerThan(const char *f)
{
   struct timespec ts;
   if(parse_datetime(&ts,f,0))
   {
      newer_than=ts.tv_sec;
      return;
   }
   struct stat st;
   if(stat(f,&st)==-1)
   {
      perror(f);
      return;
   }
   newer_than=st.st_mtime;
}
void MirrorJob::SetOlderThan(const char *f)
{
   struct timespec ts;
   if(parse_datetime(&ts,f,0))
   {
      older_than=ts.tv_sec;
      return;
   }
   struct stat st;
   if(stat(f,&st)==-1)
   {
      perror(f);
      return;
   }
   older_than=st.st_mtime;
}

mode_t MirrorJob::get_mode_mask()
{
   mode_t mode_mask=0;
   if(!(flags&ALLOW_SUID))
      mode_mask|=S_ISUID|S_ISGID;
   if(!(flags&NO_UMASK))
   {
      if(target_is_local)
      {
	 mode_t u=umask(022); // get+set
	 umask(u);	      // retore
	 mode_mask|=u;
      }
      else
	 mode_mask|=022;   // sane default.
   }
   return mode_mask;
}

void MirrorJob::Fg()
{
   Job::Fg();
   source_session->SetPriority(1);
   target_session->SetPriority(1);
}
void MirrorJob::Bg()
{
   source_session->SetPriority(0);
   target_session->SetPriority(0);
   Job::Bg();
}

MirrorJob::Statistics::Statistics()
{
   Reset();
   error_count=0;
   bytes=0;
   time=0;
}
void MirrorJob::Statistics::Reset()
{
   tot_files=new_files=mod_files=del_files=
   tot_symlinks=new_symlinks=mod_symlinks=del_symlinks=
   dirs=del_dirs=0;
}
void MirrorJob::Statistics::Add(const Statistics &s)
{
   tot_files   +=s.tot_files;
   new_files   +=s.new_files;
   mod_files   +=s.mod_files;
   del_files   +=s.del_files;
   tot_symlinks+=s.tot_symlinks;
   new_symlinks+=s.new_symlinks;
   mod_symlinks+=s.mod_symlinks;
   del_symlinks+=s.del_symlinks;
   dirs        +=s.dirs;
   del_dirs    +=s.del_dirs;
   error_count +=s.error_count;
   bytes       +=s.bytes;
   time	       +=s.time;
}
bool MirrorJob::Statistics::HaveSomethingDone(int flags)
{
   bool del=(flags&MirrorJob::DELETE);
   return new_files|mod_files|(del_files*del)|new_symlinks|mod_symlinks|(del_symlinks*del)|(del_dirs*del);
}

const char *MirrorJob::SetScriptFile(const char *n)
{
   script_name.set(n);
   if(strcmp(n,"-"))
   {
      script=fopen(n,"w");
      if(!script)
	 return xstring::format("%s: %s",n,strerror(errno));
      setvbuf(script,NULL,_IOLBF,0);
      script_needs_closing=true;
   }
   else
   {
      script=stdout;
      script_needs_closing=false;
   }
   return 0;
}

void MirrorJob::SetOnChange(const char *oc)
{
   on_change.set(oc);
}

const char *MirrorJob::AddPattern(Ref<PatternSet>& exclude,char opt,const char *optarg)
{
   PatternSet::Type type=
      (opt=='x'||opt=='X'||opt=='\0'?PatternSet::EXCLUDE:PatternSet::INCLUDE);
   PatternSet::Pattern *pattern=0;
   if(opt=='x' || opt=='i')
   {
      Ref<PatternSet::Regex> rx(new PatternSet::Regex(optarg));
      if(rx->Error())
	 return xstring::get_tmp(rx->ErrorText());
      pattern=rx.borrow();
   }
   else if(opt=='X' || opt=='I')
   {
      pattern=new PatternSet::Glob(optarg);
   }
   if(!exclude)
   {
      const char *default_exclude=ResMgr::Query("mirror:exclude-regex",0);
      const char *default_include=ResMgr::Query("mirror:include-regex",0);

      // don't create default pattern set if not needed
      if(!pattern && !(default_exclude && *default_exclude))
	 return NULL;

      exclude=new PatternSet;
      /* Make default_exclude the first pattern so that it can be
       * overridden by --include later, and do that only when first
       * explicit pattern is for exclusion - otherwise all files are
       * excluded by default and no default exclusion is needed. */
      if(type==PatternSet::EXCLUDE && default_exclude && *default_exclude)
      {
	 exclude->Add(type,new PatternSet::Regex(default_exclude));
	 if(default_include && *default_include)
	    exclude->Add(PatternSet::INCLUDE,new PatternSet::Regex(default_include));
      }
   }
   if(pattern)
      exclude->Add(type,pattern);

   return NULL; // no error
}

const char *MirrorJob::SetRecursionMode(const char *m)
{
   struct { const char name[8]; recursion_mode_t mode; } map[]={
      {"always", RECURSION_ALWAYS},
      {"never",  RECURSION_NEVER},
      {"missing",RECURSION_MISSING},
      {"newer",  RECURSION_NEWER},
   };
   unsigned i;
   for(i=0; i<sizeof(map)/sizeof(map[0]); i++) {
      if(!strcasecmp(m,map[i].name)) {
	 recursion_mode=map[i].mode;
	 return 0;
      }
   }
   xstring list(map[0].name);
   for(i=1; i<sizeof(map)/sizeof(map[0]); i++)
      list.append(", ").append(map[i].name);
   return xstring::format(_("%s must be one of: %s"),"--recursion",list.get());
}

CMD(mirror)
{
#define args (parent->args)
#define eprintf parent->eprintf
   enum {
      OPT_ALLOW_CHOWN,
      OPT_DELETE_FIRST,
      OPT_IGNORE_SIZE,
      OPT_IGNORE_TIME,
      OPT_LOOP,
      OPT_MAX_ERRORS,
      OPT_NO_DEREFERENCE,
      OPT_NO_SYMLINKS,
      OPT_NO_UMASK,
      OPT_OLDER_THAN,
      OPT_ONLY_MISSING,
      OPT_ONLY_EXISTING,
      OPT_PERMS,
      OPT_REMOVE_SOURCE_FILES,
      OPT_SCRIPT,
      OPT_SCRIPT_ONLY,
      OPT_SIZE_RANGE,
      OPT_USE_CACHE,
      OPT_USE_PGET_N,
      OPT_SKIP_NOACCESS,
      OPT_ON_CHANGE,
      OPT_NO_EMPTY_DIRS,
      OPT_DEPTH_FIRST,
      OPT_ASCII,
      OPT_SCAN_ALL_FIRST,
      OPT_OVERWRITE,
      OPT_NO_OVERWRITE,
      OPT_RECURSION,
      OPT_UPLOAD_OLDER,
      OPT_TRANSFER_ALL,
   };
   static const struct option mirror_opts[]=
   {
      {"delete",no_argument,0,'e'},
      {"allow-suid",no_argument,0,'s'},
      {"allow-chown",no_argument,0,OPT_ALLOW_CHOWN},
      {"include",required_argument,0,'i'},
      {"exclude",required_argument,0,'x'},
      {"include-glob",required_argument,0,'I'},
      {"exclude-glob",required_argument,0,'X'},
      {"only-newer",no_argument,0,'n'},
      {"no-recursion",no_argument,0,'r'},
      {"no-perms",no_argument,0,'p'},
      {"perms",no_argument,0,OPT_PERMS},
      {"no-umask",no_argument,0,OPT_NO_UMASK},
      {"continue",no_argument,0,'c'},
      {"reverse",no_argument,0,'R'},
      {"verbose",optional_argument,0,'v'},
      {"newer-than",required_argument,0,'N'},
      {"file",required_argument,0,'f'},
      {"older-than",required_argument,0,OPT_OLDER_THAN},
      {"size-range",required_argument,0,OPT_SIZE_RANGE},
      {"dereference",no_argument,0,'L'},
      {"no-dereference",no_argument,0,OPT_NO_DEREFERENCE},
      {"use-cache",no_argument,0,OPT_USE_CACHE},
      {"Remove-source-files",no_argument,0,OPT_REMOVE_SOURCE_FILES},
      {"parallel",optional_argument,0,'P'},
      {"ignore-time",no_argument,0,OPT_IGNORE_TIME},
      {"ignore-size",no_argument,0,OPT_IGNORE_SIZE},
      {"only-missing",no_argument,0,OPT_ONLY_MISSING},
      {"only-existing",no_argument,0,OPT_ONLY_EXISTING},
      {"log",required_argument,0,OPT_SCRIPT},
      {"script",    required_argument,0,OPT_SCRIPT_ONLY},
      {"just-print",optional_argument,0,OPT_SCRIPT_ONLY},
      {"dry-run",   optional_argument,0,OPT_SCRIPT_ONLY},
      {"delete-first",no_argument,0,OPT_DELETE_FIRST},
      {"use-pget-n",optional_argument,0,OPT_USE_PGET_N},
      {"no-symlinks",no_argument,0,OPT_NO_SYMLINKS},
      {"loop",no_argument,0,OPT_LOOP},
      {"max-errors",required_argument,0,OPT_MAX_ERRORS},
      {"skip-noaccess",no_argument,0,OPT_SKIP_NOACCESS},
      {"on-change",required_argument,0,OPT_ON_CHANGE},
      {"no-empty-dirs",no_argument,0,OPT_NO_EMPTY_DIRS},
      {"depth-first",no_argument,0,OPT_DEPTH_FIRST},
      {"ascii",no_argument,0,OPT_ASCII},
      {"target-directory",required_argument,0,'O'},
      {"destination-directory",required_argument,0,'O'},
      {"scan-all-first",no_argument,0,OPT_SCAN_ALL_FIRST},
      {"overwrite",no_argument,0,OPT_OVERWRITE},
      {"no-overwrite",no_argument,0,OPT_NO_OVERWRITE},
      {"recursion",required_argument,0,OPT_RECURSION},
      {"upload-older",no_argument,0,OPT_UPLOAD_OLDER},
      {"transfer-all",no_argument,0,OPT_TRANSFER_ALL},
      {0}
   };

   int opt;
   int flags=0;
   int max_error_count=0;

   bool use_cache=false;

   FileAccessRef source_session;
   FileAccessRef target_session;

   int	 verbose=0;
   const char *newer_than=0;
   const char *older_than=0;
   Ref<Range> size_range;
   bool  remove_source_files=false;
   bool	 skip_noaccess=ResMgr::QueryBool("mirror:skip-noaccess",0);
   int	 parallel=ResMgr::Query("mirror:parallel-transfer-count",0);
   int	 use_pget=ResMgr::Query("mirror:use-pget-n",0);
   bool	 reverse=false;
   bool	 script_only=false;
   bool	 no_empty_dirs=ResMgr::QueryBool("mirror:no-empty-dirs",0);
   const char *script_file=0;
   const char *on_change=0;
   const char *recursion_mode=0;

   Ref<PatternSet> exclude;

   if(!ResMgr::QueryBool("mirror:set-permissions",0))
      flags|=MirrorJob::NO_PERMS;
   if(ResMgr::QueryBool("mirror:dereference",0))
      flags|=MirrorJob::RETR_SYMLINKS;
   if(ResMgr::QueryBool("mirror:overwrite",0))
      flags|=MirrorJob::OVERWRITE;

   const char *source_dir=NULL;
   const char *target_dir=NULL;

   args->rewind();
   while((opt=args->getopt_long("esi:x:I:X:nrpcRvN:LP:af:O:",mirror_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 flags|=MirrorJob::DELETE;
	 break;
      case('s'):
	 flags|=MirrorJob::ALLOW_SUID;
	 break;
      case(OPT_ALLOW_CHOWN):
	 flags|=MirrorJob::ALLOW_CHOWN;
	 break;
      case('a'):
	 flags|=MirrorJob::ALLOW_SUID|MirrorJob::ALLOW_CHOWN|MirrorJob::NO_UMASK;
	 break;
      case('r'):
	 recursion_mode="never";
	 break;
      case('n'):
	 flags|=MirrorJob::ONLY_NEWER;
	 break;
      case('p'):
	 flags|=MirrorJob::NO_PERMS;
	 break;
      case(OPT_PERMS):
	 flags&=~MirrorJob::NO_PERMS;
	 break;
      case('c'):
	 flags|=MirrorJob::CONTINUE;
	 break;
      case('x'):
      case('i'):
      case('X'):
      case('I'):
      {
	 const char *err=MirrorJob::AddPattern(exclude,opt,optarg);
	 if(err)
	 {
	    eprintf(_("%s: regular expression `%s': %s\n"),
	       args->a0(),optarg,err);
	    goto no_job;
	 }
	 break;
      }
      case('R'):
	 reverse=true;
	 break;
      case('L'):
	 flags|=MirrorJob::RETR_SYMLINKS;
	 break;
      case(OPT_NO_DEREFERENCE):
	 flags&=~MirrorJob::RETR_SYMLINKS;
	 break;
      case('v'):
	 if(optarg)
	    verbose=atoi(optarg);
	 else
	    verbose++;
	 if(verbose>1)
	    flags|=MirrorJob::REPORT_NOT_DELETED;
	 break;
      case('N'):
	 newer_than=optarg;
	 break;
      case('f'):
      {
	 // mirror for a single file (or glob pattern).
	 recursion_mode="never";
	 MirrorJob::AddPattern(exclude,'I',basename_ptr(optarg));
	 source_dir=dirname(optarg);
	 source_dir=alloca_strdup(source_dir); // save the temp string
	 break;
      }
      case('O'):
	 target_dir=optarg;
	 break;
      case(OPT_OLDER_THAN):
	 older_than=optarg;
	 break;
      case(OPT_SIZE_RANGE):
	 size_range=new Range(optarg);
	 if(size_range->Error())
	 {
	    eprintf("%s: --size-range \"%s\": %s\n",
	       args->a0(),optarg,size_range->ErrorText());
	    goto no_job;
	 }
	 break;
      case(OPT_NO_UMASK):
	 flags|=MirrorJob::NO_UMASK;
	 break;
      case(OPT_USE_CACHE):
	 use_cache=true;
	 break;
      case(OPT_REMOVE_SOURCE_FILES):
	 remove_source_files=true;
	 break;
      case(OPT_IGNORE_TIME):
	 flags|=MirrorJob::IGNORE_TIME;
	 break;
      case(OPT_IGNORE_SIZE):
	 flags|=MirrorJob::IGNORE_SIZE;
	 break;
      case(OPT_ONLY_MISSING):
	 flags|=MirrorJob::IGNORE_TIME|MirrorJob::IGNORE_SIZE;
	 break;
      case('P'):
	 if(optarg)
	    parallel=atoi(optarg);
	 else
	    parallel=3;
	 break;
      case(OPT_USE_PGET_N):
	 if(optarg)
	    use_pget=atoi(optarg);
	 else
	    use_pget=3;
	 break;
      case(OPT_SCRIPT_ONLY):
	 script_only=true;
      case(OPT_SCRIPT):
	 script_file=optarg;
	 if(script_file==0)
	    script_file="-";
	 break;
      case(OPT_DELETE_FIRST):
	 flags|=MirrorJob::REMOVE_FIRST|MirrorJob::DELETE;
	 break;
      case(OPT_NO_SYMLINKS):
	 flags|=MirrorJob::NO_SYMLINKS;
	 break;
      case(OPT_LOOP):
	 flags|=MirrorJob::LOOP;
	 break;
      case(OPT_MAX_ERRORS):
	 max_error_count=atoi(optarg);
	 break;
      case(OPT_SKIP_NOACCESS):
	 skip_noaccess=true;
	 break;
      case(OPT_ON_CHANGE):
	 on_change=optarg;
	 break;
      case(OPT_ONLY_EXISTING):
	 flags|=MirrorJob::ONLY_EXISTING;
	 break;
      case(OPT_NO_EMPTY_DIRS):
	 no_empty_dirs=true;
	 flags|=MirrorJob::NO_EMPTY_DIRS|MirrorJob::DEPTH_FIRST;
	 break;
      case(OPT_DEPTH_FIRST):
	 flags|=MirrorJob::DEPTH_FIRST;
	 break;
      case(OPT_SCAN_ALL_FIRST):
	 flags|=MirrorJob::SCAN_ALL_FIRST|MirrorJob::DEPTH_FIRST;
	 break;
      case(OPT_ASCII):
	 flags|=MirrorJob::ASCII|MirrorJob::IGNORE_SIZE;
	 break;
      case(OPT_OVERWRITE):
	 flags|=MirrorJob::OVERWRITE;
	 break;
      case(OPT_NO_OVERWRITE):
	 flags&=~MirrorJob::OVERWRITE;
	 break;
      case(OPT_RECURSION):
	 recursion_mode=optarg;
	 break;
      case(OPT_UPLOAD_OLDER):
	 flags|=MirrorJob::UPLOAD_OLDER;
	 break;
      case(OPT_TRANSFER_ALL):
	 flags|=MirrorJob::TRANSFER_ALL;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
      no_job:
	 return 0;
      }
   }

   if(exclude && xstrcasecmp(recursion_mode,"never"))
   {
      /* Users usually don't want to exclude all directories when recursing */
      if(exclude->GetFirstType()==PatternSet::INCLUDE)
	 exclude->AddFirst(PatternSet::INCLUDE,new PatternSet::Regex("/$"));
   }

   /* add default exclusion if no explicit patterns were specified */
   if(!exclude)
      MirrorJob::AddPattern(exclude,'\0',0);

   args->back();

   const char *arg=args->getnext();
   if(arg)
   {
      if(source_dir)
      {
	 eprintf(_("%s: ambiguous source directory (`%s' or `%s'?)\n"),args->a0(),
	    source_dir,arg);
	 goto no_job;
      }
      source_dir=arg;
      ParsedURL source_url(source_dir);
      if(source_url.proto && source_url.path)
      {
	 source_session=FileAccess::New(&source_url);
	 if(!source_session)
	 {
	    eprintf("%s: %s%s\n",args->a0(),source_url.proto.get(),
		     _(" - not supported protocol"));
	    goto no_job;
	 }
	 source_dir=alloca_strdup(source_url.path);
      }
      arg=args->getnext();
      if(arg)
      {
	 if(target_dir)
	 {
	    eprintf(_("%s: ambiguous target directory (`%s' or `%s'?)\n"),args->a0(),
	       target_dir,arg);
	    goto no_job;
	 }
	 target_dir=arg;
	 ParsedURL target_url(target_dir);
	 if(target_url.proto && target_url.path)
	 {
	    target_session=FileAccess::New(&target_url);
	    if(!target_session)
	    {
	       eprintf("%s: %s%s\n",args->a0(),target_url.proto.get(),
			_(" - not supported protocol"));
	       goto no_job;
	    }
	    target_dir=alloca_strdup(target_url.path);
	 }
	 if(last_char(arg)=='/' && basename_ptr(arg)[0]!='/')
	 {
	    // user wants source dir name appended.
	    const char *base=basename_ptr(source_dir);
	    if(base[0]!='/' && strcmp(base,basename_ptr(arg))) {
	       target_dir=xstring::cat(target_dir,base,NULL);
	       target_dir=alloca_strdup(target_dir); // save the buffer
	    }
	 }
      }
      else
      {
	 target_dir=basename_ptr(source_dir);
	 if(target_dir[0]=='/')
	    target_dir=".";
	 else if(target_dir[0]=='~') {
	    target_dir=dir_file(".",target_dir);
	    target_dir=alloca_strdup(target_dir); // save the buffer
	 }
      }
   }

   if(!source_dir) {
      if(ResMgr::QueryBool("mirror:require-source",0)) {
	 eprintf(_("%s: source directory is required (mirror:require-source is set)\n"),args->a0());
	 return 0;
      }
      source_dir=".";
   }
   if(!target_dir)
      target_dir=".";

   if(!reverse)
   {
      if(!source_session)
	 source_session=parent->session->Clone();
      if(!target_session)
	 target_session=FileAccess::New("file");
   }
   else //reverse
   {
      if(!source_session)
	 source_session=FileAccess::New("file");
      if(!target_session)
	 target_session=parent->session->Clone();
   }

   if(no_empty_dirs)
      flags|=MirrorJob::NO_EMPTY_DIRS|MirrorJob::DEPTH_FIRST;

   JobRef<MirrorJob> j(new MirrorJob(0,source_session.borrow(),target_session.borrow(),source_dir,target_dir));
   j->SetFlags(flags,1);
   j->SetVerbose(verbose);
   j->SetExclude(exclude.borrow());

   if(newer_than)
      j->SetNewerThan(newer_than);
   if(older_than)
      j->SetOlderThan(older_than);
   if(size_range)
      j->SetSizeRange(size_range.borrow());
   j->UseCache(use_cache);
   if(remove_source_files)
      j->RemoveSourceFiles();
   if(skip_noaccess)
      j->SkipNoAccess();
   if(parallel<0)
      parallel=0;
   if(parallel>64)
      parallel=64;   // a (in)sane limit.
   if(parallel)
      j->SetParallel(parallel);
   if(use_pget>1 && !(flags&MirrorJob::ASCII))
      j->SetPGet(use_pget);

   if(recursion_mode) {
      const char *err=j->SetRecursionMode(recursion_mode);
      if(err) {
	 eprintf("%s: %s\n",args->a0(),err);
	 return 0;
      }
   }
   if(script_file)
   {
      const char *err=j->SetScriptFile(script_file);
      if(err)
      {
	 eprintf("%s: %s\n",args->a0(),err);
	 return 0;
      }
   }
   if(script_only)
   {
      j->ScriptOnly();
      if(!script_file)
	 j->SetScriptFile("-");
   }
   j->SetMaxErrorCount(max_error_count);
   if(on_change)
      j->SetOnChange(on_change);

   return j.borrow();

#undef args
}

#include "modconfig.h"
#ifndef MODULE_CMD_MIRROR
# define module_init cmd_mirror_module_init
#endif
CDECL void module_init()
{
   CmdExec::RegisterCommand("mirror",cmd_mirror,0,
	 N_("\nMirror specified remote directory to local directory\n\n"
	 " -c, --continue         continue a mirror job if possible\n"
	 " -e, --delete           delete files not present at remote site\n"
	 "     --delete-first     delete old files before transferring new ones\n"
	 " -s, --allow-suid       set suid/sgid bits according to remote site\n"
	 "     --allow-chown      try to set owner and group on files\n"
	 "     --ignore-time      ignore time when deciding whether to download\n"
	 " -n, --only-newer       download only newer files (-c won't work)\n"
	 " -r, --no-recursion     don't go to subdirectories\n"
	 " -p, --no-perms         don't set file permissions\n"
	 "     --no-umask         don't apply umask to file modes\n"
	 " -R, --reverse          reverse mirror (put files)\n"
	 " -L, --dereference      download symbolic links as files\n"
	 " -N, --newer-than=SPEC  download only files newer than specified time\n"
	 " -P, --parallel[=N]     download N files in parallel\n"
	 " -i RX, --include RX    include matching files\n"
	 " -x RX, --exclude RX    exclude matching files\n"
	 "                        RX is extended regular expression\n"
	 " -v, --verbose[=N]      verbose operation\n"
	 "     --log=FILE         write lftp commands being executed to FILE\n"
	 "     --script=FILE      write lftp commands to FILE, but don't execute them\n"
	 "     --just-print, --dry-run    same as --script=-\n"
	 "\n"
	 "When using -R, the first directory is local and the second is remote.\n"
	 "If the second directory is omitted, basename of first directory is used.\n"
	 "If both directories are omitted, current local and remote directories are used.\n")
   );
}
