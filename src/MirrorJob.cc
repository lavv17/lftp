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

#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include "MirrorJob.h"
#include "CmdExec.h"
#include "rmJob.h"
#include "mkdirJob.h"
#include "ChmodJob.h"
#include "misc.h"
#include "xalloca.h"
#include "plural.h"
#include "getopt.h"
#include "FindJob.h"
#include "url.h"
#include "CopyJob.h"

void  MirrorJob::PrintStatus(int v)
{
   const char *tab="\t";

   if(v==-1)
      tab="";

   if(Done())
      goto final;

   switch(state)
   {
   case(INITIAL_STATE):
   case(DONE):
   case(WAITING_FOR_TRANSFER):
   case(TARGET_REMOVE_OLD):
   case(TARGET_CHMOD):
      break;

   case(MAKE_TARGET_DIR):
      printf("\tmkdir `%s' [%s]\n",target_dir,target_session->CurrentStatus());
      break;

   case(CHANGING_DIR):
      printf("\tcd `%s' [%s]\n",target_dir,target_session->CurrentStatus());
      printf("\tcd `%s' [%s]\n",source_dir,source_session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(target_list_info)
      {
	 if(target_relative_dir)
	    printf("\t%s: %s\n",target_relative_dir,target_list_info->Status());
	 else
	    printf("\t%s\n",target_list_info->Status());
      }
      if(source_list_info)
      {
	 if(source_relative_dir)
	    printf("\t%s: %s\n",source_relative_dir,source_list_info->Status());
	 else
	    printf("\t%s\n",source_list_info->Status());
      }
      break;
   }
   return;

final:
   if(stats.dirs>0)
      printf(plural("%sTotal: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
		     stats.dirs,stats.tot_files,stats.tot_symlinks),
	 tab,stats.dirs,stats.tot_files,stats.tot_symlinks);
   if(stats.new_files || stats.new_symlinks)
      printf(plural("%sNew: %d file$|s$, %d symlink$|s$\n",
		     stats.new_files,stats.new_symlinks),
	 tab,stats.new_files,stats.new_symlinks);
   if(stats.mod_files || stats.mod_symlinks)
      printf(plural("%sModified: %d file$|s$, %d symlink$|s$\n",
		     stats.mod_files,stats.mod_symlinks),
	 tab,stats.mod_files,stats.mod_symlinks);
   if(stats.del_dirs || stats.del_files || stats.del_symlinks)
      printf(plural(flags&DELETE ?
	       "%sRemoved: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n"
	      :"%sTo be removed: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
	      stats.del_dirs,stats.del_files,stats.del_symlinks),
	 tab,stats.del_dirs,stats.del_files,stats.del_symlinks);
   return;
}

void  MirrorJob::ShowRunStatus(StatusLine *s)
{
   switch(state)
   {
   case(INITIAL_STATE):
   case(DONE):
      break;

   // these have a sub-job
   case(WAITING_FOR_TRANSFER):
   case(TARGET_REMOVE_OLD):
   case(TARGET_CHMOD):
      Job::ShowRunStatus(s);
      break;

   case(MAKE_TARGET_DIR):
      s->Show("mkdir `%s' [%s]",target_dir,target_session->CurrentStatus());
      break;

   case(CHANGING_DIR):
      if(target_session->IsOpen() && (!source_session->IsOpen() || now%4>=2))
	 s->Show("cd `%s' [%s]",target_dir,target_session->CurrentStatus());
      else if(source_session->IsOpen())
	 s->Show("cd `%s' [%s]",source_dir,source_session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(target_list_info && (!source_list_info || now%4>=2))
      {
	 if(target_relative_dir)
	    s->Show("%s: %s",squeeze_file_name(target_relative_dir,20),
	       target_list_info->Status());
	 else
	    s->Show("%s",target_list_info->Status());
      }
      else if(source_list_info)
      {
	 if(source_relative_dir)
	    s->Show("%s: %s",squeeze_file_name(source_relative_dir,20),
	       source_list_info->Status());
	 else
	    s->Show("%s",source_list_info->Status());
      }
      break;
   }
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

   FileInfo::type filetype=FileInfo::NORMAL;
   if(file->defined&file->TYPE)
      filetype=file->filetype;
   else
   {
      FileInfo *target=target_set->FindByName(file->name);
      if(target && (target->defined&target->TYPE))
	 filetype=target->filetype;
   }

   switch(filetype)
   {
      case(FileInfo::NORMAL):
      {
	 bool remove_target=false;
	 bool cont_this=false;
	 if(target_is_local)
	 {
	    if(lstat(target_name,&st)!=-1)
	    {
	       // few safety checks.
	       FileInfo *old=new_files_set->FindByName(file->name);
	       if(old)
		  goto skip;  // file has appeared after mirror start
	       old=old_files_set->FindByName(file->name);
	       if(old && ((old->defined&old->SIZE && old->size!=st.st_size)
			||(old->defined&old->DATE && old->date!=st.st_mtime)))
		  goto skip;  // the file has changed after mirror start
	    }
	 }
	 FileInfo *old=target_set->FindByName(file->name);
	 if(old)
	 {
	    if((flags&CONTINUE)
	    && (old->defined&file->TYPE) && old->filetype==old->NORMAL
	    && (file->defined&file->DATE)
	    && (old->defined&old->DATE)
	    && file->date + file->date_prec < old->date - old->date_prec
	    && (file->defined&file->SIZE) && (old->defined&old->SIZE)
	    && file->size >= old->size)
	    {
	       cont_this=true;
	    }
	    else
	    {
	       Report(_("Removing old file `%s'"),
			dir_file(target_relative_dir,file->name));
	       remove_target=true;
	    }
	    stats.mod_files++;
	 }
	 else
	    stats.new_files++;

	 Report(_("Transferring file `%s'"),
		  dir_file(source_relative_dir,file->name));

   #if 0 // unfinished
	 if(script)
	 {
	    args=new ArgV("put1");
	    if(cont_this)
	       args->Append("-c");
	    args->Append(local_name);
	    args->Append("-o");
	    args->Append(file->name);
	    char *cmd=args->CombineQuoted();
	    fprintf(script,"%s",cmd);
	    xfree(cmd);
	    delete args; args=0;
	    if(script_only)
	       goto skip;
	 }
   #endif

	 FileCopyPeerFA *src_peer=
	    new FileCopyPeerFA(source_session->Clone(),file->name,FA::RETRIEVE);
	 FileCopyPeerFA *dst_peer=
	    new FileCopyPeerFA(target_session->Clone(),file->name,FA::STORE);

	 FileCopy *c=FileCopy::New(src_peer,dst_peer,cont_this);
	 if(remove_source_files)
	    c->RemoveSourceLater();
	 if(remove_target)
	    c->RemoveTargetFirst();
	 CopyJob *cp=
	    new CopyJob(c,file->name,"mirror");
	 if(file->defined&file->DATE && file->date_prec<1)
	    cp->SetDate(file->date);
	 if(file->defined&file->SIZE)
	    cp->SetSize(file->size);
	 AddWaiting(cp);
	 transfer_count++;
	 cp->SetParentFg(this);
	 cp->cmdline=xasprintf("\\transfer %s",file->name);
	 state=WAITING_FOR_TRANSFER;
	 break;
      }
      case(FileInfo::DIRECTORY):
      {
	 if(flags&NO_RECURSION)
	    goto skip;
	 bool create_target_dir=true;
	 FileInfo *old=target_set->FindByName(file->name);
	 if(old && old->defined&old->TYPE && old->filetype==old->DIRECTORY)
	    create_target_dir=false;
	 if(target_is_local)
	 {
	    if(lstat(target_name,&st)!=-1)
	    {
	       if(S_ISDIR(st.st_mode))
	       {
		  chmod(target_name,st.st_mode|0700);
		  create_target_dir=false;
	       }
	       else
	       {
		  Report(_("Removing old local file `%s'"),
			   dir_file(target_relative_dir,file->name));
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
	 mj->SetParentFg(this);
	 mj->cmdline=xasprintf("\\mirror %s",file->name);

	 // inherit flags and other things
	 mj->SetFlags(flags,1);
	 mj->UseCache(use_cache);

	 mj->SetExclude(exclude);

	 mj->source_relative_dir=
	       xstrdup(dir_file(source_relative_dir,file->name));
	 mj->target_relative_dir=
	       xstrdup(dir_file(target_relative_dir,file->name));

	 mj->verbose_report=verbose_report;
	 mj->newer_than=newer_than;
	 mj->parallel=parallel;
	 mj->remove_source_files=remove_source_files;
	 mj->create_target_dir=create_target_dir;

	 if(verbose_report>=3)
	    Report(_("Mirroring directory `%s'"),mj->target_relative_dir);

	 break;
      }
      case(FileInfo::SYMLINK):
      {
	 if(!target_is_local)
	 {
	    // can't create symlink remotely
	    goto skip;
	 }

#ifdef HAVE_LSTAT
	 if(file->defined&file->SYMLINK)
	 {
	    struct stat st;
	    if(lstat(target_name,&st)!=-1)
	    {
	       Report(_("Removing old local file `%s'"),
			dir_file(target_relative_dir,file->name));
	       stats.mod_symlinks++;
	       if(remove(target_name)==-1)
	       {
		  eprintf("mirror: remove(%s): %s\n",target_name,strerror(errno));
		  goto skip;
	       }
	    }
	    else
	    {
	       stats.new_symlinks++;
	    }
	    Report(_("Making symbolic link `%s' to `%s'"),
		     dir_file(target_relative_dir,file->name),file->symlink);
	    res=symlink(file->symlink,target_name);
	    if(res==-1)
	       eprintf("mirror: symlink(%s): %s\n",target_name,strerror(errno));
	 }
#endif /* LSTAT */
	 break;
      }
   }
skip:
   return;
}

void  MirrorJob::InitSets(FileSet *source,FileSet *dest)
{
   source->Count(NULL,&stats.tot_files,&stats.tot_symlinks,&stats.tot_files);

   to_rm=new FileSet(dest);
   to_rm->SubtractAny(source);

   same=new FileSet(source);
   to_transfer=new FileSet(source);

   int ignore=0;
   if(flags&ONLY_NEWER)
      ignore|=FileInfo::IGNORE_SIZE_IF_OLDER|FileInfo::IGNORE_DATE_IF_OLDER;
   if(strcmp(target_session->GetProto(),"file"))
      ignore|=FileInfo::IGNORE_DATE_IF_OLDER;
   to_transfer->SubtractSame(dest,ignore);

   same->SubtractAny(to_transfer);

   if(newer_than!=(time_t)-1)
      to_transfer->SubtractOlderThan(newer_than);

   new_files_set=new FileSet(to_transfer);
   new_files_set->SubtractAny(dest);
   old_files_set=new FileSet(dest);
   old_files_set->SubtractNotIn(to_transfer);

   to_transfer->SortByPatternList(ResMgr::Query("mirror:order",0));
}

void MirrorJob::HandleChdir(FileAccess * &session, int &redirections)
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
	 if(loc_c && loc_c[0] && max_redirections>0
	 && loc_c[strlen(loc_c)-1]=='/')
	 {
	    if(++redirections>max_redirections)
	       goto cd_err_normal;
	    eprintf(_("%s: received redirection to `%s'\n"),"mirror",loc_c);

	    char *loc=alloca_strdup(loc_c);
	    session->Close(); // loc_c is no longer valid.

	    ParsedURL u(loc,true);

	    if(!u.proto)
	    {
	       url::decode_string(loc);
	       session->Chdir(loc);
	       return;
	    }
	    SessionPool::Reuse(session);
	    session=FA::New(&u);
	    session->Chdir(u.path);
	    return;
	 }
      }
   cd_err_normal:
      eprintf("mirror: %s\n",session->StrError(res));
      stats.error_count++;
      transfer_count-=root_transfer_count;
      state=DONE;
      source_session->Close();
      target_session->Close();
      return;
   }
   if(res==FA::OK)
      session->Close();
}
void MirrorJob::HandleListInfoCreation(FileAccess * &session,ListInfo * &list_info,const char *relative_dir)
{
   list_info=session->MakeListInfo();
   if(list_info==0)
   {
      eprintf(_("mirror: protocol `%s' is not suitable for mirror\n"),
	       session->GetProto());
      transfer_count-=root_transfer_count;
      state=DONE;
      return;
   }
   list_info->UseCache(use_cache);
   list_info->Need(FileInfo::ALL_INFO);
   if(flags&RETR_SYMLINKS)
      list_info->FollowSymlinks();

   list_info->SetExclude(relative_dir,exclude);
   Roll(list_info);
}

void MirrorJob::HandleListInfo(ListInfo * &list_info, FileSet * &set)
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
      state=DONE;
      Delete(source_list_info);
      source_list_info=0;
      Delete(target_list_info);
      target_list_info=0;
      return;
   }
   set=list_info->GetResult();
   Delete(list_info);
   list_info=0;
   set->ExcludeDots(); // don't need .. and .
}

int   MirrorJob::Do()
{
   int	 res;
   int	 m=STALL;
   FileInfo *fi;
   Job	 *j;

   switch(state)
   {
   case(INITIAL_STATE):
      if(!create_target_dir || !strcmp(target_dir,".") || !strcmp(target_dir,".."))
	 goto pre_CHANGING_DIR;
      if(target_relative_dir)
	 Report(_("Making directory `%s'"),target_relative_dir);
      target_session->Mkdir(target_dir);
      state=MAKE_TARGET_DIR;
      m=MOVED;
   case(MAKE_TARGET_DIR):
      res=target_session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      target_session->Close();

   pre_CHANGING_DIR:
      source_session->Chdir(source_dir);
      target_session->Chdir(target_dir);
      source_redirections=0;
      target_redirections=0;
      Roll(source_session);
      Roll(target_session);
      state=CHANGING_DIR;
      m=MOVED;
   case(CHANGING_DIR):
      HandleChdir(source_session,source_redirections);
      HandleChdir(target_session,target_redirections);
      if(state!=CHANGING_DIR)
	 return MOVED;
      if(source_session->IsOpen() || target_session->IsOpen())
	 return m;

      xfree(target_dir);
      target_dir=xstrdup(target_session->GetCwd());
      xfree(source_dir);
      source_dir=xstrdup(source_session->GetCwd());

      HandleListInfoCreation(source_session,source_list_info,source_relative_dir);
      HandleListInfoCreation(target_session,target_list_info,target_relative_dir);
      if(state!=CHANGING_DIR)
      {
	 Delete(source_list_info); source_list_info=0;
	 Delete(target_list_info); target_list_info=0;
	 return MOVED;
      }
      state=GETTING_LIST_INFO;
      m=MOVED;

   case(GETTING_LIST_INFO):
      HandleListInfo(source_list_info,source_set);
      HandleListInfo(target_list_info,target_set);
      if(state!=GETTING_LIST_INFO)
	 return MOVED;
      if(source_list_info || target_list_info)
	 return m;

      // now we have both target and source file sets.
      stats.dirs++;

      InitSets(source_set,target_set);

      to_transfer->rewind();
      transfer_count-=root_transfer_count; // leave room for transfers.
      state=WAITING_FOR_TRANSFER;
      m=MOVED;
   case(WAITING_FOR_TRANSFER):
      j=FindDoneAwaitedJob();
      if(j)
      {
	 if(j->ExitCode()!=0)
	    stats.error_count++;
	 RemoveWaiting(j);
	 Delete(j);
	 transfer_count--;
      }
      while(transfer_count<parallel && state==WAITING_FOR_TRANSFER)
      {
	 FileInfo *file=to_transfer->curr();
      	 if(!file)
	 {
	    if(waiting_num>0)
	       return m;
	    goto pre_TARGET_REMOVE_OLD;
	 }
	 HandleFile(file);
	 to_transfer->next();
	 m=MOVED;
      }
      return m;

   pre_TARGET_REMOVE_OLD:
      to_rm->Count(&stats.del_dirs,&stats.del_files,&stats.del_symlinks,&stats.del_files);
      to_rm->rewind();
      state=TARGET_REMOVE_OLD;
      m=MOVED;
   case(TARGET_REMOVE_OLD):
      if(waiting_num==0)
      {
	 if(flags&DELETE)
	 {
	    ArgV *args=new ArgV("rm");
	    FileInfo *file=to_rm->curr();
	    if(file)
	    {
	       args->Append(file->name);
	       to_rm->next();
	       if(file->defined&file->TYPE && file->filetype==file->DIRECTORY)
	       {
		  Report(_("Removing old directory `%s'"),
			   dir_file(target_relative_dir,file->name));
		  args->getnext(); // prepare args position.
	       	  j=new FinderJob_Cmd(target_session->Clone(),args,FinderJob_Cmd::RM);
	       }
	       else
	       {
		  Report(_("Removing old file `%s'"),
			   dir_file(target_relative_dir,file->name));
		  j=new rmJob(target_session->Clone(),args);
	       }
	       j->SetParentFg(this);
	       j->cmdline=args->Combine();
	       AddWaiting(j);
	    }
	 }
	 else if(flags&REPORT_NOT_DELETED)
	 {
	    for(FileInfo *file=to_rm->curr(); file; file=to_rm->next())
	    {
	       Report(_("Old file `%s' is not removed"),
			dir_file(target_relative_dir,file->name));
	    }
	 }
	 if(waiting_num==0)
	    goto pre_TARGET_CHMOD;
      }
      j=FindDoneAwaitedJob();
      if(j)
      {
	 RemoveWaiting(j);
	 Delete(j);
	 return MOVED;
      }
      return m;

   pre_TARGET_CHMOD:
      if(flags&NO_PERMS)
	 goto pre_DONE;

      to_transfer->rewind();
      state=TARGET_CHMOD;
      m=MOVED;
      goto target_chmod_next;
   case(TARGET_CHMOD):
      j=FindDoneAwaitedJob();
      if(j)
      {
	 RemoveWaiting(j);
	 Delete(j);

      target_chmod_next:
	 fi=to_transfer->curr();
	 if(!fi)
	    goto pre_DONE;
	 to_transfer->next();
	 if(!(fi->defined&fi->MODE))
	    goto target_chmod_next;
	 ArgV *a=new ArgV("chmod");
	 a->Append(fi->name);
	 ChmodJob *cj=new ChmodJob(target_session->Clone(),fi->mode&~get_mode_mask(),a);
	 AddWaiting(cj);
	 cj->SetParentFg(this);
	 cj->cmdline=a->Combine();
	 cj->BeQuiet();   // chmod is not supported on all servers; be quiet.

	 m=MOVED;
      }
      return m;

   pre_DONE:
      if(target_is_local)     // FIXME
      {
	 to_transfer->LocalUtime(target_dir,/*only_dirs=*/true);
	 if(flags&ALLOW_CHOWN)
	    to_transfer->LocalChown(target_dir);
	 if(!(flags&NO_PERMS))
	    same->LocalChmod(target_dir,get_mode_mask());
	 if(flags&ALLOW_CHOWN)
	    same->LocalChown(target_dir);
      }
      state=DONE;
      m=MOVED;
   case(DONE):
      return m;
   }
   /*NOTREACHED*/
   abort();
   return m;
}

MirrorJob::MirrorJob(MirrorJob *parent,
   FileAccess *source,FileAccess *target,
   const char *new_source_dir,const char *new_target_dir)
 :
   root_transfer_count(0),
   transfer_count(parent?parent->transfer_count:root_transfer_count)
{
   verbose_report=0;
   parent_mirror=parent;

   source_session=source;
   target_session=target;
   // TODO: get rid of this.
   source_is_local=!strcmp(source_session->GetProto(),"file");
   target_is_local=!strcmp(target_session->GetProto(),"file");

   source_dir=xstrdup(new_source_dir);
   target_dir=xstrdup(new_target_dir);
   source_relative_dir=0;
   target_relative_dir=0;

   to_transfer=to_rm=same=0;
   source_set=target_set=0;
   new_files_set=old_files_set=0;
   create_target_dir=true;
   source_list_info=0;
   target_list_info=0;

   flags=0;

   exclude=0;

   state=INITIAL_STATE;

   dir_made=false;
   newer_than=(time_t)-1;

   script=0;
   script_only=false;
   script_needs_closing=false;

   use_cache=false;
   remove_source_files=false;

   parallel=1;

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
   xfree(source_dir);
   xfree(target_dir);
   xfree(source_relative_dir);
   xfree(target_relative_dir);
   delete source_set;
   delete target_set;
   delete to_transfer;
   delete to_rm;
   delete same;
   delete new_files_set;
   delete old_files_set;
   Delete(source_list_info);
   Delete(target_list_info);
   // session disposal should be done after ListInfo deletion.
   SessionPool::Reuse(source_session);
   SessionPool::Reuse(target_session);
   delete exclude;
   if(script && script_needs_closing)
      fclose(script);
   if(parent_mirror)
      parent_mirror->stats.Add(stats);
   transfer_count++;	// parent mirror will decrement it.
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
      if(p!=-1 && p!=getpgrp())
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

void MirrorJob::SetNewerThan(const char *f)
{
   struct stat st;
   if(stat(f,&st)==-1)
   {
      perror(f);
      return;
   }
   newer_than=st.st_mtime;
}

mode_t MirrorJob::get_mode_mask()
{
   mode_t mode_mask=0;
   if(!(flags&ALLOW_SUID))
      mode_mask|=S_ISUID|S_ISGID;
   if(!(flags&NO_UMASK))
   {
      mode_t u=umask(022); // get+set
      umask(u);	    // retore
      mode_mask|=u;
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
   tot_files=new_files=mod_files=del_files=
   tot_symlinks=new_symlinks=mod_symlinks=del_symlinks=
   dirs=del_dirs=
   error_count=0;
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
}

CMD(mirror)
{
#define args (parent->args)
#define eprintf (parent->eprintf)
   static struct option mirror_opts[]=
   {
      {"delete",no_argument,0,'e'},
      {"allow-suid",no_argument,0,'s'},
      {"allow-chown",no_argument,0,256+'a'},
      {"include",required_argument,0,'i'},
      {"exclude",required_argument,0,'x'},
      {"only-newer",no_argument,0,'n'},
      {"no-recursion",no_argument,0,'r'},
      {"no-perms",no_argument,0,'p'},
      {"no-umask",no_argument,0,256+'u'},
      {"continue",no_argument,0,'c'},
      {"reverse",no_argument,0,'R'},
      {"verbose",optional_argument,0,'v'},
      {"newer-than",required_argument,0,'N'},
      {"dereference",no_argument,0,'L'},
      {"use-cache",no_argument,0,256+'C'},
      {"Remove-source-files",no_argument,0,256+'R'},
      {"parallel",optional_argument,0,'P'},
      {0}
   };

   int opt;
   int flags=0;

   bool use_cache=false;

   FileAccess *source_session=0;
   FileAccess *target_session=0;

   int	 verbose=0;
   const char *newer_than=0;
   bool  remove_source_files=false;
   int	 parallel=0;
   bool	 reverse=false;

   PatternSet *exclude=new PatternSet;

   args->rewind();
   while((opt=args->getopt_long("esi:x:nrpcRvN:LPa",mirror_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 flags|=MirrorJob::DELETE;
	 break;
      case('s'):
	 flags|=MirrorJob::ALLOW_SUID;
	 break;
      case(256+'a'):
	 flags|=MirrorJob::ALLOW_CHOWN;
	 break;
      case('a'):
	 flags|=MirrorJob::ALLOW_SUID|MirrorJob::ALLOW_CHOWN|MirrorJob::NO_UMASK;
	 break;
      case('r'):
	 flags|=MirrorJob::NO_RECURSION;
	 break;
      case('n'):
	 flags|=MirrorJob::ONLY_NEWER;
	 break;
      case('p'):
	 flags|=MirrorJob::NO_PERMS;
	 break;
      case('c'):
	 flags|=MirrorJob::CONTINUE;
	 break;
      case('x'):
      case('i'):
      {
	 PatternSet::Regex *rx=new PatternSet::Regex(optarg);
	 if(rx->Error())
	 {
	    eprintf("%s: %s: %s\n",args->a0(),optarg,rx->ErrorText());
	    delete rx;
	    goto no_job;
	 }
	 exclude->Add(opt=='x'?PatternSet::EXCLUDE:PatternSet::INCLUDE,rx);
	 break;
      }
      case('R'):
	 reverse=true;
	 break;
      case('L'):
	 flags|=MirrorJob::RETR_SYMLINKS;
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
      case(256+'u'):
	 flags|=MirrorJob::NO_UMASK;
	 break;
      case(256+'C'):
	 use_cache=true;
	 break;
      case(256+'R'):
	 remove_source_files=true;
	 break;
      case('P'):
	 if(optarg)
	    parallel=atoi(optarg);
	 else
	    parallel=3;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
      no_job:
	 delete exclude;
	 if(source_session)
	    SMTask::Delete(source_session);
	 if(target_session)
	    SMTask::Delete(target_session);
	 return 0;
      }
   }

   args->back();

   const char *source_dir=".";
   const char *target_dir=".";

   const char *arg=args->getnext();
   if(arg)
   {
      source_dir=arg;
      ParsedURL source_url(source_dir);
      if(source_url.proto && source_url.path)
      {
	 source_session=FileAccess::New(&source_url);
	 if(!source_session)
	 {
	    eprintf("%s: %s%s\n",args->a0(),source_url.proto,
		     _(" - not supported protocol"));
	    goto no_job;
	 }
	 source_dir=alloca_strdup(source_url.path);
      }
      arg=args->getnext();
      if(arg)
      {
	 target_dir=arg;
	 ParsedURL target_url(target_dir);
	 if(target_url.proto && target_url.path)
	 {
	    target_session=FileAccess::New(&target_url);
	    if(!target_session)
	    {
	       eprintf("%s: %s%s\n",args->a0(),target_url.proto,
			_(" - not supported protocol"));
	       goto no_job;
	    }
	    target_dir=alloca_strdup(target_url.path);
	 }
      }
      else
      {
	 target_dir=basename_ptr(source_dir);
	 if(target_dir[0]=='/')
	    target_dir=".";
      }
   }

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

   MirrorJob *j=new MirrorJob(0,source_session,target_session,source_dir,target_dir);
   j->SetFlags(flags,1);
   j->SetVerbose(verbose);
   j->SetExclude(exclude);

   if(newer_than)
      j->SetNewerThan(newer_than);
   j->UseCache(use_cache);
   if(remove_source_files)
      j->RemoveSourceFiles();
   if(parallel<0)
      parallel=0;
   if(parallel>16)
      parallel=16;   // a sane limit.
   if(parallel)
      j->SetParallel(parallel);

   return j;

#undef args
}

#include "modconfig.h"
#ifdef MODULE_CMD_MIRROR
void module_init()
{
   CmdExec::RegisterCommand("mirror",cmd_mirror);
}
#endif
