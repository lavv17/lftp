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

void  MirrorJob::PrintStatus(int v)
{
   const char *tab="\t";

   if(v!=-1)
      SessionJob::PrintStatus(v);
   else
      tab="";

   if(Done())
      goto final;

   switch(state)
   {
   case(INITIAL_STATE):
   case(DONE):
   case(WAITING_FOR_SUBGET):
   case(WAITING_FOR_SUBMIRROR):
   case(WAITING_FOR_RM_BEFORE_PUT):
   case(WAITING_FOR_MKDIR_BEFORE_SUBMIRROR):
   case(REMOTE_REMOVE_OLD):
   case(REMOTE_CHMOD):
      break;

   case(MAKE_REMOTE_DIR):
      printf("\tmkdir `%s' [%s]\n",remote_dir,session->CurrentStatus());
      break;

   case(CHANGING_REMOTE_DIR):
      printf("\tcd `%s' [%s]\n",remote_dir,session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(remote_relative_dir)
	 printf("\t%s: %s\n",remote_relative_dir,list_info->Status());
      else
	 printf("\t%s\n",list_info->Status());
      break;
   }
   return;

final:
   if(dirs>0)
      printf(plural("%sTotal: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
		     dirs,tot_files,tot_symlinks),
	 tab,dirs,tot_files,tot_symlinks);
   if(new_files || new_symlinks)
      printf(plural("%sNew: %d file$|s$, %d symlink$|s$\n",
		     new_files,new_symlinks),
	 tab,new_files,new_symlinks);
   if(mod_files || mod_symlinks)
      printf(plural("%sModified: %d file$|s$, %d symlink$|s$\n",
		     mod_files,mod_symlinks),
	 tab,mod_files,mod_symlinks);
   if(del_dirs || del_files || del_symlinks)
      printf(plural(flags&DELETE ?
	       "%sRemoved: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n"
	      :"%sTo be removed: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n",
	      del_dirs,del_files,del_symlinks),
	 tab,del_dirs,del_files,del_symlinks);
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
   case(WAITING_FOR_SUBGET):
   case(WAITING_FOR_SUBMIRROR):
   case(WAITING_FOR_RM_BEFORE_PUT):
   case(WAITING_FOR_MKDIR_BEFORE_SUBMIRROR):
   case(REMOTE_REMOVE_OLD):
   case(REMOTE_CHMOD):
      Job::ShowRunStatus(s);
      break;

   case(MAKE_REMOTE_DIR):
      s->Show("mkdir `%s' [%s]",remote_dir,session->CurrentStatus());
      break;

   case(CHANGING_REMOTE_DIR):
      s->Show("cd `%s' [%s]",remote_dir,session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      if(remote_relative_dir)
	 s->Show("%s: %s",squeeze_file_name(remote_relative_dir,20),
	    list_info->Status());
      else
	 s->Show("%s",list_info->Status());
      break;
   }
}

void  MirrorJob::HandleFile(int how)
{
   ArgV *args;
   int	 res;
   mode_t mode;
   struct stat st;

   // dir_name returns pointer to static data - need to dup it.
   const char *local_name=dir_file(local_dir,file->name);
   local_name=alloca_strdup(local_name);
   const char *remote_name=dir_file(remote_dir,file->name);
   remote_name=alloca_strdup(remote_name);

   if(file->defined&file->TYPE)
   {
      switch(file->filetype)
      {
      case(FileInfo::NORMAL):
      {
      try_get:
	 cont_this=false;
	 if(how!=0)
	    goto skip;
	 if(flags&REVERSE)
	 {
	    FileInfo *old=remote_set->FindByName(file->name);
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
		  Report(_("Removing old remote file `%s'"),
			   dir_file(remote_relative_dir,file->name));
		  args=new ArgV("rm");
		  args->Append(file->name);
		  if(script)
		  {
		     char *cmd=args->CombineQuoted();
		     fprintf(script,"%s",cmd);
		     xfree(cmd);
		     if(script_only)
		     {
			delete args;
			goto skip;
		     }
		  }
		  Job *j=new rmJob(Clone(),args);
		  j->SetParentFg(this);
		  j->cmdline=args->Combine();
		  AddWaiting(j);
	       }
	       mod_files++;
	    }
	    else
	       new_files++;
	    state=WAITING_FOR_RM_BEFORE_PUT;
	    break;
	 }
	 if(lstat(local_name,&st)!=-1)
	 {
	    // few safety checks.
	    FileInfo *old=new_files_set->FindByName(file->name);
	    if(old)
	       goto skip;  // file has appeared after mirror start
	    old=old_files_set->FindByName(file->name);
	    if(old && ((old->defined&old->SIZE && old->size!=st.st_size)
		     ||(old->defined&old->DATE && old->date!=st.st_mtime)))
	       goto skip;  // the file has changed after mirror start

	    if((flags&CONTINUE) && S_ISREG(st.st_mode)
	    && (file->defined&file->DATE)
	    && file->date + file->date_prec < st.st_mtime
	    && (file->defined&file->SIZE) && file->size >= st.st_size)
	    {
	       cont_this=true;
	       // make it writable
	       if((st.st_mode&0200)==0)
		  chmod(local_name,st.st_mode|0200);
	    }
	    else
	    {
	       Report(_("Removing old local file `%s'"),
			dir_file(local_relative_dir,file->name));
	       if(script)
	       {
		  // FIXME: shell-quote file name.
		  fprintf(script,"! rm %s",file->name);
		  if(script_only)
		     goto skip;
	       }
	       remove(local_name);
	    }
	    mod_files++;
	 }
	 else
	 {
	    new_files++;
	 }
	 // launch get job
	 Report(_("Retrieving remote file `%s'"),
		  dir_file(remote_relative_dir,file->name));
	 if(script)
	 {
	    args=new ArgV("get1");
	    if(cont_this)
	       args->Append("-c");
	    args->Append(file->name);
	    args->Append("-o");
	    args->Append(local_name);
	    char *cmd=args->CombineQuoted();
	    fprintf(script,"%s",cmd);
	    xfree(cmd);
	    delete args; args=0;
	    if(script_only)
	       goto skip;
	 }

	 FileCopyPeerFA *src_peer=
	    new FileCopyPeerFA(session->Clone(),file->name,FA::RETRIEVE);
	 FileCopyPeer *dst_peer=
	    FileCopyPeerFDStream::NewPut(local_name,cont_this);

	 FileCopy *c=FileCopy::New(src_peer,dst_peer,cont_this);
	 if(remove_source_files)
	    c->RemoveSourceLater();
	 CopyJob *cp=
	    new CopyJob(c,file->name,"mirror");
	 if(file->defined&file->DATE)
	    cp->SetDate(file->date);
	 if(file->defined&file->SIZE)
	    cp->SetSize(file->size);
	 AddWaiting(cp);
	 cp->SetParentFg(this);
	 cp->cmdline=(char*)xmalloc(10+strlen(file->name));
	 sprintf(cp->cmdline,"\\get %s",file->name);
	 break;
      }
      case(FileInfo::DIRECTORY):
      {
      try_recurse:
	 if(how!=1 || (flags&NO_RECURSION))
	    goto skip;
	 if(!strcmp(file->name,".") || !strcmp(file->name,".."))
	    goto skip;
	 if(flags&REVERSE)
	 {
	    if(!dir_made)
	    {
	       FileInfo *f=remote_set->FindByName(file->name);
	       if(f==0)
	       {
		  Report(_("Making remote directory `%s'"),
			   dir_file(remote_relative_dir,file->name));
		  args=new ArgV("mkdir");
		  args->Append("--");
		  args->Append(file->name);
		  Job *j=new mkdirJob(Clone(),args);
		  j->SetParentFg(this);
		  j->cmdline=args->Combine();
		  AddWaiting(j);
	       }
	       else
		  dir_made=true;
	       state=WAITING_FOR_MKDIR_BEFORE_SUBMIRROR;
	       break;
	    }
	 }
	 else // !REVERSE
	 {
	    mode=((file->defined&file->MODE)&&!(flags&NO_PERMS))?file->mode:0755;
	    struct stat st;
	    if(lstat(local_name,&st)!=-1)
	    {
	       if(S_ISDIR(st.st_mode))
		  chmod(local_name,st.st_mode|0700);
	       else
	       {
		  Report(_("Removing old local file `%s'"),
			   dir_file(local_relative_dir,file->name));
		  if(remove(local_name)==-1)
		  {
		     eprintf("mirror: remove(%s): %s\n",local_name,strerror(errno));
		     goto skip;
		  }
		  goto do_mkdir;
	       }
	    }
	    else // no such directory
	    {
	    do_mkdir:
	       res=mkdir(local_name,mode|0700);
	       if(res==-1)
	       {
		  eprintf("mirror: mkdir(%s): %s\n",local_name,strerror(errno));
		  goto skip;
	       }
	    }
	 }
	 // launch sub-mirror
	 MirrorJob *mj=new MirrorJob(Clone(),local_name,remote_name);
	 mj->parent_mirror=this;
	 AddWaiting(mj);
	 mj->SetParentFg(this);
	 mj->cmdline=(char*)xmalloc(strlen("\\mirror")+1+
					 strlen(file->name)+1);
	 sprintf(mj->cmdline,"\\mirror %s",file->name);

	 // inherit flags and other things
	 mj->SetFlags(flags,1);
	 mj->UseCache(use_cache);

	 if(rx_include)	mj->SetInclude(rx_include);
	 if(rx_exclude)	mj->SetExclude(rx_exclude);

	 mj->local_relative_dir=
	       xstrdup(dir_file(local_relative_dir,file->name));
	 mj->remote_relative_dir=
	       xstrdup(dir_file(remote_relative_dir,file->name));

	 mj->verbose_report=verbose_report;
	 mj->newer_than=newer_than;
	 mj->parallel=parallel;
	 mj->remove_source_files=remove_source_files;

	 if(verbose_report>=3)
	    Report(_("Mirroring directory `%s'"),
	       (flags&REVERSE)?mj->local_relative_dir:mj->remote_relative_dir);

	 dir_made=false;   // for next directory

	 break;
      }
      case(FileInfo::SYMLINK):
	 if(how!=0)
	    goto skip;
	 if(flags&REVERSE)
	 {
	    // can't create symlink remotely
	    goto skip;
	 }

#ifdef HAVE_LSTAT
	 if(file->defined&file->SYMLINK)
	 {
	    struct stat st;
	    if(lstat(local_name,&st)!=-1)
	    {
	       Report(_("Removing old local file `%s'"),
			dir_file(local_relative_dir,file->name));
	       mod_symlinks++;
	       if(remove(local_name)==-1)
	       {
		  eprintf("mirror: remove(%s): %s\n",local_name,strerror(errno));
		  goto skip;
	       }
	    }
	    else
	    {
	       new_symlinks++;
	    }
	    Report(_("Making symbolic link `%s' to `%s'"),
		     dir_file(local_relative_dir,file->name),file->symlink);
	    res=symlink(file->symlink,local_name);
	    if(res==-1)
	       eprintf("mirror: symlink(%s): %s\n",local_name,strerror(errno));
	 }
#endif /* LSTAT */
	 goto skip;
      }
   }
   else
   {
      FileInfo *local=local_set->FindByName(file->name);
      if(local && (local->defined&local->TYPE)
      && local->filetype==local->DIRECTORY)
      {
	 // assume it's a directory
	 goto try_recurse;
      }
      // no info on type -- try to get
      goto try_get;
   }
   return;

skip:
   if(how==1)  // NOTE: check invocation places before changing this
      to_transfer->next();
   return;
}

void  MirrorJob::InitSets(FileSet *source,FileSet *dest)
{
   source->Count(NULL,&tot_files,&tot_symlinks,&tot_files);

   to_rm=new FileSet(dest);
   to_rm->SubtractAny(source);

   same=new FileSet(source);
   to_transfer=new FileSet(source);

   int ignore=0;
   if(flags&ONLY_NEWER)
      ignore|=FileInfo::IGNORE_SIZE_IF_OLDER|FileInfo::IGNORE_DATE_IF_OLDER;
   if(flags&REVERSE)
      ignore|=FileInfo::IGNORE_DATE_IF_OLDER;
   to_transfer->SubtractSame(dest,ignore);

   same->SubtractAny(to_transfer);

   if(newer_than!=(time_t)-1)
      to_transfer->SubtractOlderThan(newer_than);

   new_files_set=new FileSet(to_transfer);
   new_files_set->SubtractAny(dest);
   old_files_set=new FileSet(dest);
   old_files_set->SubtractNotIn(to_transfer);

   to_transfer->SortByPatternList(ResMgr::Query(
      flags&REVERSE?"mirror:order-upload":"mirror:order-download",0));
}

int   MirrorJob::Do()
{
   int	 res;
   int	 m=STALL;
   FileInfo *fi;
   Job	 *j;

   switch(state)
   {
   case(DONE):
      return STALL;

   case(INITIAL_STATE):
      if(!local_set)
      {
	 local_session=FileAccess::New("file");
	 if(!local_session)
	 {
	    eprintf(_("mirror: cannot create `file:' access object, installation error?\n"));
	    state=DONE;
	    return MOVED;
	 }
	 local_session->Chdir(local_dir,false);
	 list_info=local_session->MakeListInfo();
	 list_info->UseCache(use_cache);
	 if(flags&RETR_SYMLINKS)
	    list_info->FollowSymlinks();
	 list_info->SetExclude(local_relative_dir,
			rx_exclude?&rxc_exclude:0,rx_include?&rxc_include:0);

	 while(!list_info->Done())
	    Roll(list_info);  // this should be fast

	 if(list_info->Error())
	    goto list_info_error;

	 local_set=list_info->GetResult();
	 Delete(list_info);
	 list_info=0;
	 Delete(local_session);
	 local_session=0;

	 local_set->ExcludeDots();  // don't need .. and .
      }
      if(create_remote_dir)
      {
	 create_remote_dir=false;
	 session->Mkdir(remote_dir,true);
	 state=MAKE_REMOTE_DIR;
	 return MOVED;
      }
      session->Chdir(remote_dir);
      redirections=0;
      Roll(session);
      state=CHANGING_REMOTE_DIR;
      return MOVED;

   case(MAKE_REMOTE_DIR):
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      session->Close();
      state=INITIAL_STATE;
      return MOVED;

   case(CHANGING_REMOTE_DIR):
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return STALL;
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
		  return MOVED;
	       }
	       SessionPool::Reuse(session);
	       session=FA::New(&u);
	       session->Chdir(u.path);
	       return MOVED;
	    }
	 }
      cd_err_normal:
	 eprintf("mirror: %s\n",session->StrError(res));
	 error_count++;
	 state=DONE;
	 session->Close();
	 return MOVED;
      }
      session->Close();

      xfree(remote_dir);
      remote_dir=xstrdup(session->GetCwd());

      list_info=session->MakeListInfo();
      if(list_info==0)
      {
	 eprintf(_("mirror: protocol `%s' is not suitable for mirror\n"),
		  session->GetProto());
      	 state=DONE;
      	 return MOVED;
      }
      list_info->UseCache(use_cache);
      list_info->Need(FileInfo::ALL_INFO);
      if(flags&RETR_SYMLINKS)
	 list_info->FollowSymlinks();

      list_info->SetExclude(remote_relative_dir,
		     rx_exclude?&rxc_exclude:0,rx_include?&rxc_include:0);
      Roll(list_info);
      state=GETTING_LIST_INFO;
      return MOVED;

   case(GETTING_LIST_INFO):
   {
      if(!list_info->Done())
	 return STALL;
      if(list_info->Error())
      {
      list_info_error:
	 eprintf("mirror: %s\n",list_info->ErrorText());
	 error_count++;
	 state=DONE;
      	 Delete(list_info);
	 list_info=0;
	 return MOVED;
      }
      remote_set=list_info->GetResult();
      Delete(list_info);
      list_info=0;

      remote_set->ExcludeDots(); // don't need .. and .

      // now we have both local and remote file sets.
      dirs++;

      if(flags&REVERSE)
	 InitSets(local_set,remote_set);
      else
	 InitSets(remote_set,local_set);

      to_transfer->rewind();
//       waiting=0; ???
      state=WAITING_FOR_SUBGET;
      return MOVED;
   }

   case(WAITING_FOR_RM_BEFORE_PUT):
   {
      if(waiting_num>0)
      {
	 while((j=FindDoneAwaitedJob())!=0)
	 {
	    m=MOVED;
	    RemoveWaiting(j);
	    Delete(j);
	 }
	 if(waiting_num>0)
	    return m;
      }
      Report(_("Sending local file `%s'"),
	       dir_file(local_relative_dir,file->name));
      const char *local_name=dir_file(local_dir,file->name);

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

      FileCopyPeerFA *dst_peer=
	 new FileCopyPeerFA(session->Clone(),file->name,FA::STORE);
      FileCopyPeer *src_peer=
	 FileCopyPeerFDStream::NewGet(local_name);

      FileCopy *c=FileCopy::New(src_peer,dst_peer,cont_this);
      if(remove_source_files)
	 c->RemoveSourceLater();
      CopyJob *cp=
	 new CopyJob(c,file->name,"mirror");
      AddWaiting(cp);
      cp->SetParentFg(this);
      cp->cmdline=(char*)xmalloc(10+strlen(file->name));
      sprintf(cp->cmdline,"\\put %s",file->name);
      state=WAITING_FOR_SUBGET;
      return MOVED;
   }

   case(WAITING_FOR_SUBGET):
      j=FindDoneAwaitedJob();
      if(j)
      {
	 if(j->ExitCode()!=0)
	    error_count++;
	 RemoveWaiting(j);
	 Delete(j);
      }
      while(waiting_num<parallel && state==WAITING_FOR_SUBGET)
      {
	 file=to_transfer->curr();
      	 if(!file)
	 {
	    if(waiting_num>0)
	       return m;
	    to_transfer->rewind();
	    state=WAITING_FOR_SUBMIRROR;
	    return MOVED;
	 }
	 HandleFile(0);
	 to_transfer->next();
	 m=MOVED;
      }
      return m;

   case(WAITING_FOR_MKDIR_BEFORE_SUBMIRROR):
      j=FindDoneAwaitedJob();
      if(j==0 && waiting_num>0)
	 return STALL;
      if(j)
      {
	 if(j->ExitCode()==0)
	    dir_made=true;
	 RemoveWaiting(j);
	 Delete(j);
	 if(!dir_made)
	    to_transfer->next();
      }
      state=WAITING_FOR_SUBMIRROR;
      return MOVED;

   case(WAITING_FOR_SUBMIRROR):
      j=FindDoneAwaitedJob();
      if(j==0 && waiting_num>0)
	 return STALL;
      if(j)
      {
	 MirrorJob &mj=*(MirrorJob*)j; // we are sure it is a MirrorJob
	 tot_files+=mj.tot_files;
	 new_files+=mj.new_files;
	 mod_files+=mj.mod_files;
	 del_files+=mj.del_files;
	 tot_symlinks+=mj.tot_symlinks;
	 new_symlinks+=mj.new_symlinks;
	 mod_symlinks+=mj.mod_symlinks;
	 del_symlinks+=mj.del_symlinks;
	 dirs+=mj.dirs;
	 del_dirs+=mj.del_dirs;
	 error_count+=mj.error_count;

	 to_transfer->next();
	 RemoveWaiting(j);
	 Delete(j);
      }
      while(waiting_num<1 && state==WAITING_FOR_SUBMIRROR)
      {
	 file=to_transfer->curr();
      	 if(!file)
	 {
	    to_rm->Count(&del_dirs,&del_files,&del_symlinks,&del_files);
	    to_rm->rewind();
	    if(flags&REVERSE)
	    {
	       state=REMOTE_REMOVE_OLD;
	       return MOVED;
	    }
	    // else not REVERSE
	    if(flags&DELETE)
	    {
	       while((file=to_rm->curr())!=0)
	       {
		  Report(_("Removing old local file `%s'"),
			   dir_file(local_relative_dir,file->name));
		  const char *local_name=dir_file(local_dir,file->name);
		  if(remove(local_name)==-1)
		  {
		     if(!(file->defined & file->TYPE)
		     || file->filetype==file->DIRECTORY)
			// try this
			truncate_file_tree(local_name);
		     else
			perror(local_name);
		  }
		  to_rm->next();
	       }
	    }
	    else if(flags&REPORT_NOT_DELETED)
	    {
	       for(file=to_rm->curr(); file; file=to_rm->next())
	       {
		  Report(_("Old local file `%s' is not removed"),
			   dir_file(local_relative_dir,file->name));
	       }
	    }
	    mode_t mode_mask=get_mode_mask();
	    if(!(flags&NO_PERMS))
	       to_transfer->LocalChmod(local_dir,mode_mask);
	    to_transfer->LocalUtime(local_dir,/*only_dirs=*/true);
	    if(flags&ALLOW_CHOWN)
	       to_transfer->LocalChown(local_dir);
	    if(!(flags&NO_PERMS))
	       same->LocalChmod(local_dir,mode_mask);
	    if(flags&ALLOW_CHOWN)
	       same->LocalChown(local_dir);
#if 0 // this can cause problems if files really differ
	    same->LocalUtime(local_dir); // the old mtime can differ up to prec
#endif
	    state=DONE;
	    return MOVED;
	 }
	 HandleFile(1);
	 m=MOVED;
      }
      return m;

   case(REMOTE_REMOVE_OLD):
      if(waiting_num==0)
      {
	 if(flags&DELETE)
	 {
	    ArgV *args=new ArgV("rm");
	    file=to_rm->curr();
	    if(file)
	    {
	       args->Append(file->name);
	       to_rm->next();
	       if(file->defined&file->TYPE && file->filetype==file->DIRECTORY)
	       {
		  Report(_("Removing old remote directory `%s'"),
			   dir_file(remote_relative_dir,file->name));
		  args->getnext(); // prepare args position.
	       	  j=new FinderJob_Cmd(Clone(),args,FinderJob_Cmd::RM);
	       }
	       else
	       {
		  Report(_("Removing old remote file `%s'"),
			   dir_file(remote_relative_dir,file->name));
		  j=new rmJob(Clone(),args);
	       }
	       j->SetParentFg(this);
	       j->cmdline=args->Combine();
	       AddWaiting(j);
	    }
	 }
	 else if(flags&REPORT_NOT_DELETED)
	 {
	    for(file=to_rm->curr(); file; file=to_rm->next())
	    {
	       Report(_("Old remote file `%s' is not removed"),
			dir_file(remote_relative_dir,file->name));
	    }
	 }
	 if(waiting_num==0)
	    goto pre_REMOTE_CHMOD;
      }
      j=FindDoneAwaitedJob();
      if(j)
      {
	 RemoveWaiting(j);
	 Delete(j);
	 return MOVED;
      }
      return m;

   pre_REMOTE_CHMOD:
      if(flags&NO_PERMS)
      {
	 state=DONE;
	 return MOVED;
      }
      to_transfer->rewind();
      state=REMOTE_CHMOD;
      m=MOVED;
      goto remote_chmod_next;
   case(REMOTE_CHMOD):
      j=FindDoneAwaitedJob();
      if(j)
      {
	 RemoveWaiting(j);
	 Delete(j);

      remote_chmod_next:
	 fi=to_transfer->curr();
	 if(!fi)
	 {
	    state=DONE;
	    return MOVED;
	 }
	 to_transfer->next();
	 if(!(fi->defined&fi->MODE))
	    goto remote_chmod_next;
	 ArgV *a=new ArgV("chmod");
	 a->Append(fi->name);
	 ChmodJob *cj=new ChmodJob(Clone(),fi->mode&~get_mode_mask(),a);
	 AddWaiting(cj);
	 cj->SetParentFg(this);
	 cj->cmdline=a->Combine();
	 cj->BeQuiet();   // chmod is not supported on all servers; be quiet.

	 m=MOVED;
      }
      return m;
   }
   /*NOTREACHED*/
   abort();
}

MirrorJob::MirrorJob(FileAccess *f,const char *new_local_dir,const char *new_remote_dir)
   : SessionJob(f)
{
   verbose_report=0;
   parent_mirror=0;

   local_dir=xstrdup(new_local_dir);
   remote_dir=xstrdup(new_remote_dir);
   local_relative_dir=0;
   remote_relative_dir=0;

   to_transfer=to_rm=same=0;
   remote_set=local_set=0;
   new_files_set=old_files_set=0;
   file=0;
   cont_this=false;
   list_info=0;
   local_session=0;

   tot_files=new_files=mod_files=del_files=
   tot_symlinks=new_symlinks=mod_symlinks=del_symlinks=0;
   dirs=0; del_dirs=0;
   error_count=0;

   flags=0;

   rx_include=rx_exclude=0;
   memset(&rxc_include,0,sizeof(regex_t));   // for safety
   memset(&rxc_exclude,0,sizeof(regex_t));   // for safety

   state=INITIAL_STATE;

   dir_made=false;
   create_remote_dir=false;
   newer_than=(time_t)-1;

   script=0;
   script_only=false;
   script_needs_closing=false;

   use_cache=false;
   remove_source_files=false;

   parallel=1;

   redirections=0;
}

MirrorJob::~MirrorJob()
{
   xfree(local_dir);
   xfree(remote_dir);
   xfree(local_relative_dir);
   xfree(remote_relative_dir);
   if(local_set)
      delete local_set;
   if(remote_set)
      delete remote_set;
   if(to_transfer)
      delete to_transfer;
   if(to_rm)
      delete to_rm;
   if(same)
      delete same;
   if(new_files_set)
      delete new_files_set;
   if(old_files_set)
      delete old_files_set;
   // don't delete this->file -- it is a reference
   Delete(list_info);
   Delete(local_session);
   if(rx_include)
   {
      xfree(rx_include);
      regfree(&rxc_include);
   }
   if(rx_exclude)
   {
      xfree(rx_exclude);
      regfree(&rxc_exclude);
   }
   if(script && script_needs_closing)
      fclose(script);
}

const char *MirrorJob::SetRX(const char *s,char **rx,regex_t *rxc)
{
   if(*rx)
   {
      *rx=(char*)xrealloc(*rx,strlen(*rx)+1+strlen(s)+1);
      strcat(*rx,"|");
      strcat(*rx,s);
      regfree(rxc);
      memset(rxc,0,sizeof(*rxc));   // for safety
   }
   else
   {
      *rx=xstrdup(s);
   }
   int res=regcomp(rxc,*rx,REG_NOSUB|REG_EXTENDED);
   if(res!=0)
   {
      xfree(*rx);
      *rx=0;

      static char err[256];
      regerror(res,rxc,err,sizeof(err));

      return err;
   }
   return 0;
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


CMD(mirror)
{
#define args (parent->args)
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

   char *cwd;
   const char *rcwd;
   int opt;
   int	 flags=0;

   static char *include=0;
   static int include_alloc=0;
   static char *exclude=0;
   static int exclude_alloc=0;
   bool use_cache=false;
#define APPEND_STRING(s,a,s1) \
   {			                  \
      int len,len1=strlen(s1);            \
      if(!s)		                  \
      {			                  \
	 s=(char*)xmalloc(a = len1+1);    \
      	 strcpy(s,s1);	                  \
      }			                  \
      else				  \
      {					  \
	 len=strlen(s);		       	  \
	 if(a < len+1+len1+1)		  \
	    s=(char*)xrealloc(s, a = len+1+len1+1); \
	 if(s[0]) strcat(s,"|");	  \
	 strcat(s,s1);			  \
      }					  \
   } /* END OF APPEND_STRING */

   if(include)
      include[0]=0;
   if(exclude)
      exclude[0]=0;

   bool	 create_remote_dir=false;
   int	 verbose=0;
   const char *newer_than=0;
   bool  remove_source_files=false;
   int	 parallel=0;

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
	 APPEND_STRING(exclude,exclude_alloc,optarg);
	 break;
      case('i'):
	 APPEND_STRING(include,include_alloc,optarg);
	 break;
      case('R'):
	 flags|=MirrorJob::REVERSE;
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
	 parent->eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   cwd=string_alloca(1024);
   if(getcwd(cwd,1024)==0)
   {
      perror("getcwd()");
      return 0;
   }

   args->back();
   if(flags&MirrorJob::REVERSE)
   {
      char *arg=args->getnext();
      if(!arg)
	 rcwd=".";
      else
      {
	 if(arg[0]=='/')
	    cwd=arg;
	 else
	 {
	    char *cwd1=alloca_strdup2(cwd,strlen(arg)+1);
	    strcat(cwd1,"/");
	    strcat(cwd1,arg);
	    cwd=cwd1;
	 }
	 rcwd=args->getnext();
	 if(!rcwd)
	 {
	    rcwd=basename_ptr(cwd);
	    if(rcwd[0]=='/')
	       rcwd=".";
	    else
	       create_remote_dir=true;
	 }
	 else
	    create_remote_dir=true;
      }
   }
   else	/* !REVERSE (normal) */
   {
      rcwd=args->getnext();
      if(!rcwd)
	 rcwd=".";
      else
      {
	 char *arg=args->getnext();
	 if(arg)
	 {
	    if(arg[0]=='/')
	       cwd=arg;
	    else
	    {
	       char *cwd1=alloca_strdup2(cwd,strlen(arg)+1);
	       strcat(cwd1,"/");
	       strcat(cwd1,arg);
	       cwd=cwd1;
	    }
	    if(create_directories(arg)==-1)
	       return 0;
	 }
	 else
	 {
	    const char *base=basename_ptr(rcwd);
	    if(base[0]!='/')
	    {
	       int len=strlen(cwd);
	       char *cwd1=alloca_strdup2(cwd,strlen(base)+1);
	       strcat(cwd1,"/");
	       strcat(cwd1,base);
	       cwd=cwd1;
	       if(create_directories(cwd+len+1)==-1)
		  return 0;
	    }
	 }
      }
   }
   MirrorJob *j=new MirrorJob(parent->session->Clone(),cwd,rcwd);
   j->SetFlags(flags,1);
   j->SetVerbose(verbose);
   if(create_remote_dir)
      j->CreateRemoteDir();

   const char *err;
   const char *err_tag;

   err_tag="include";
   if(include && include[0] && (err=j->SetInclude(include)))
      goto err_out;
   err_tag="exclude";
   if(exclude && exclude[0] && (err=j->SetExclude(exclude)))
      goto err_out;

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

err_out:
   parent->eprintf("%s: %s: %s\n",args->a0(),err_tag,err);
   SMTask::Delete(j);
   return 0;
#undef args
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

#include "modconfig.h"
#ifdef MODULE_CMD_MIRROR
void module_init()
{
   CmdExec::RegisterCommand("mirror",cmd_mirror);
}
#endif
