/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

void  MirrorJob::PrintStatus(int v)
{
   const char *tab="\t";

   if(v!=-1)
      SessionJob::PrintStatus(v);
   else
      tab="";

   if(!Done())
      return;

   printf(plural(N_("%sTotal: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n"),
		     dirs,tot_files,tot_symlinks),
      tab,dirs,tot_files,tot_symlinks);
   if(new_files || new_symlinks)
      printf(plural(N_("%sNew: %d file$|s$, %d symlink$|s$\n"),
		     new_files,new_symlinks),
	 tab,new_files,new_symlinks);
   if(mod_files || mod_symlinks)
      printf(plural(N_("%sModified: %d file$|s$, %d symlink$|s$\n"),
		     mod_files,mod_symlinks),
	 tab,mod_files,mod_symlinks);
   if(del_dirs || del_files || del_symlinks)
      printf(plural(flags&DELETE ?
	       N_("%sRemoved: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n")
	      :N_("%sTo be removed: %d director$y|ies$, %d file$|s$, %d symlink$|s$\n"),
	      del_dirs,del_files,del_symlinks),
	 tab,del_dirs,del_files,del_symlinks);
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
      if(waiting && !waiting->Done())
	 waiting->ShowRunStatus(s);
      break;

   case(MAKE_REMOTE_DIR):
      s->Show("mkdir `%s' [%s]",remote_dir,session->CurrentStatus());
      break;

   case(CHANGING_REMOTE_DIR):
      s->Show("cd `%s' [%s]",remote_dir,session->CurrentStatus());
      break;

   case(GETTING_LIST_INFO):
      s->Show(list_info->Status());
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
	 bool cont_this=false;
	 if(how!=0)
	    goto skip;
	 if(flags&REVERSE)
	 {
	    if(remote_set->FindByName(file->name)!=0)
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
	       waiting=new rmJob(Clone(),args);
	       waiting->parent=this;
	       waiting->cmdline=args->Combine();
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
	    && (file->defined&(file->DATE|file->DATE_UNPREC))
	    && file->date + prec.Seconds() < st.st_mtime
	    && (file->defined&file->SIZE) && file->size >= st.st_size)
	       cont_this=true;
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
	    new FileCopyPeerFA(session,file->name,FA::RETRIEVE);
	 src_peer->DontReuseSession(); // mirror won't need session
	 FileCopyPeer *dst_peer=
	    FileCopyPeerFDStream::NewPut(local_name,cont_this);

	 CopyJob *cp=
	    new CopyJob(FileCopy::New(src_peer,dst_peer,cont_this),file->name,"mirror");
	 if(file->defined&(file->DATE|file->DATE_UNPREC))
	    cp->SetDate(file->date);
	 if(file->defined&file->SIZE)
	    cp->SetSize(file->size);
	 waiting=cp;
	 waiting->parent=this;
	 waiting->cmdline=(char*)xmalloc(10+strlen(file->name));
	 sprintf(waiting->cmdline,"\\get %s",file->name);
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
		  waiting=new mkdirJob(Clone(),args);
		  waiting->parent=this;
		  waiting->cmdline=args->Combine();
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
	    res=mkdir(local_name,mode|0700);
	    if(res==-1 && errno==EEXIST)
	    {
	       struct stat st;
	       if(stat(local_name,&st)!=-1)
	       {
		  if(S_ISDIR(st.st_mode))
		     chmod(local_name,st.st_mode|0700);
	    	  else
		  {
		     Report(_("Removing old local file `%s'"),
			      dir_file(local_relative_dir,file->name));
		     remove(local_name);
		     res=mkdir(local_name,mode|0700);
		  }
	       }
	    }
	    else if(res==-1) // other error
	    {
	       eprintf("mirror: mkdir(%s): %s\n",local_name,strerror(errno));
	       goto skip;
	    }
	 }
	 // launch sub-mirror
	 MirrorJob *mj=new MirrorJob(Clone(),local_name,remote_name);
	 mj->parent_mirror=this;
	 waiting=mj;
	 waiting->parent=this;
	 waiting->cmdline=(char*)xmalloc(strlen("\\mirror")+1+
					 strlen(file->name)+1);
	 sprintf(waiting->cmdline,"\\mirror %s",file->name);

	 // inherit flags and other things
	 mj->SetFlags(flags,1);
	 mj->SetPrec(prec);

	 if(rx_include)	mj->SetInclude(rx_include);
	 if(rx_exclude)	mj->SetExclude(rx_exclude);

	 mj->local_relative_dir=
	       xstrdup(dir_file(local_relative_dir,file->name));
	 mj->remote_relative_dir=
	       xstrdup(dir_file(remote_relative_dir,file->name));

	 mj->verbose_report=verbose_report;
	 mj->newer_than=newer_than;

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
	       remove(local_name);
	    }
	    else
	    {
	       new_symlinks++;
	    }
	    Report(_("Making symbolic link `%s' to `%s'"),
		     dir_file(local_relative_dir,file->name),file->symlink);
	    res=symlink(file->symlink,local_name);
	    if(res==-1)
	       perror(local_name);
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
   if(prec.IsInfty())
      ignore|=FileInfo::DATE;
   to_transfer->SubtractSame(dest,flags&ONLY_NEWER,prec.Seconds(),ignore);

   same->SubtractAny(to_transfer);

   if(newer_than!=(time_t)-1)
      to_transfer->SubtractOlderThan(newer_than);

   new_files_set=new FileSet(to_transfer);
   new_files_set->SubtractAny(dest);
   old_files_set=new FileSet(dest);
   old_files_set->SubtractNotIn(to_transfer);
}

int   MirrorJob::Do()
{
   int	 res;
   int	 m=STALL;
   FileInfo *fi;

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
	    eprintf("mirror: cannot create `file:' access object, installation error?\n");
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
	 eprintf("mirror: %s\n",session->StrError(res));
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

      if(flags&REVERSE)
	 InitSets(local_set,remote_set);
      else
	 InitSets(remote_set,local_set);

      to_transfer->rewind();
      waiting=0;
      state=WAITING_FOR_SUBGET;
      return MOVED;
   }

   case(WAITING_FOR_RM_BEFORE_PUT):
   {
      if(waiting)
      {
	 if(!waiting->Done())
	    return STALL;
	 Delete(waiting);
	 waiting=0;
      }
      Report(_("Sending local file `%s'"),
	       dir_file(local_relative_dir,file->name));
      const char *local_name=dir_file(local_dir,file->name);

#if 0 // unfinished
      if(script)
      {
	 args=new ArgV("put1");
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
	 new FileCopyPeerFA(session,file->name,FA::STORE);
      dst_peer->DontReuseSession(); // mirror won't need session
      FileCopyPeer *src_peer=
	 FileCopyPeerFDStream::NewGet(local_name);

      CopyJob *cp=
	 new CopyJob(FileCopy::New(src_peer,dst_peer,false),file->name,"mirror");
      waiting=cp;
      waiting->parent=this;
      waiting->cmdline=(char*)xmalloc(10+strlen(file->name));
      sprintf(waiting->cmdline,"\\put %s",file->name);
      state=WAITING_FOR_SUBGET;
      return MOVED;
   }

   case(WAITING_FOR_SUBGET):
      if(waiting && waiting->Done())
      {
	 to_transfer->next();
	 Delete(waiting);
	 waiting=0;
      }
      while(!waiting && state==WAITING_FOR_SUBGET)
      {
	 file=to_transfer->curr();
      	 if(!file)
	 {
	    to_transfer->rewind();
	    state=WAITING_FOR_SUBMIRROR;
	    return MOVED;
	 }
	 HandleFile(0);
	 m=MOVED;
      }
      return m;

   case(WAITING_FOR_MKDIR_BEFORE_SUBMIRROR):
      if(waiting)
      {
	 if(!waiting->Done())
	    return STALL;
	 if(waiting->ExitCode()==0)
	    dir_made=true;
	 Delete(waiting);
	 waiting=0;
	 if(!dir_made)
	    to_transfer->next();
      }
      state=WAITING_FOR_SUBMIRROR;
      return MOVED;

   case(WAITING_FOR_SUBMIRROR):
      if(waiting && waiting->Done())
      {
	 MirrorJob &mj=*(MirrorJob*)waiting; // we are sure it is a MirrorJob
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

	 to_transfer->next();
	 Delete(waiting);
	 waiting=0;
      }
      while(!waiting && state==WAITING_FOR_SUBMIRROR)
      {
	 file=to_transfer->curr();
      	 if(!file)
	 {
	    to_rm->Count(&del_dirs,&del_files,&del_symlinks,&del_files);
	    to_rm->rewind();
	    if(flags&REVERSE)
	    {
	       if(flags&DELETE)
	       {
		  ArgV *args=new ArgV("rm");
		  while((file=to_rm->curr())!=0)
		  {
		     Report(_("Removing old remote file `%s'"),
			      dir_file(remote_relative_dir,file->name));
		     args->Append(file->name);
		     to_rm->next();
		  }
		  waiting=new rmJob(Clone(),args);
		  waiting->parent=this;
		  waiting->cmdline=args->Combine();
	       }
	       else if(flags&REPORT_NOT_DELETED)
	       {
		  for(file=to_rm->curr(); file; file=to_rm->next())
		  {
		     Report(_("Old remote file `%s' is not removed"),
			      dir_file(remote_relative_dir,file->name));
		  }
	       }
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
	    mode_t mode_mask=0;
	    if(!(flags&ALLOW_SUID))
	       mode_mask|=S_ISUID|S_ISGID;
	    if(!(flags&NO_UMASK))
	    {
	       mode_t u=umask(022); // get+set
	       umask(u);	    // retore
	       mode_mask|=u;
	    }
	    if(!(flags&NO_PERMS))
	       to_transfer->LocalChmod(local_dir,mode_mask);
	    to_transfer->LocalUtime(local_dir,/*only_dirs=*/true);
	    if(!(flags&NO_PERMS))
	       same->LocalChmod(local_dir,mode_mask);
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
      if(!waiting)
	 goto pre_REMOTE_CHMOD;
      if(waiting->Done())
      {
	 Delete(waiting);
	 waiting=0;
	 state=DONE;
	 m=MOVED;
	 goto pre_REMOTE_CHMOD;
      }
      return m;

   pre_REMOTE_CHMOD:
      to_transfer->rewind();
      goto remote_chmod_next;
   case(REMOTE_CHMOD):
      if(waiting->Done())
      {
	 Delete(waiting);
	 waiting=0;

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
	 ChmodJob *cj=new ChmodJob(Clone(),fi->mode,a);
	 waiting=cj;
	 waiting->parent=this;
	 waiting->cmdline=a->Combine();
	 cj->BeQuiet();   // chmod is not supported on all servers; be quiet.

	 m=MOVED;
      }
      return m;
   }
   /*NOTREACHED*/
   abort();
}

MirrorJob::MirrorJob(FileAccess *f,const char *new_local_dir,const char *new_remote_dir)
   : SessionJob(f), prec("0")
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
   list_info=0;
   local_session=0;

   tot_files=new_files=mod_files=del_files=
   tot_symlinks=new_symlinks=mod_symlinks=del_symlinks=0;
   dirs=1; del_dirs=0;

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
      free(rx_include);
      regfree(&rxc_include);
   }
   if(rx_exclude)
   {
      free(rx_exclude);
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
      {"include",required_argument,0,'i'},
      {"exclude",required_argument,0,'x'},
      {"time-prec",required_argument,0,'t'},
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
      {0}
   };

   char cwd[2*1024];
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

   TimeInterval prec("12h");
   bool	 create_remote_dir=false;
   int	 verbose=0;
   const char *newer_than=0;

   args->rewind();
   while((opt=args->getopt_long("esi:x:t:nrpcRvN:L",mirror_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 flags|=MirrorJob::DELETE;
	 break;
      case('s'):
	 flags|=MirrorJob::ALLOW_SUID;
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
      case('t'):
	 prec=TimeInterval(optarg);
	 if(prec.Error())
	 {
	    parent->eprintf("%s: %s: %s\n",args->a0(),optarg,prec.ErrorText());
	    parent->eprintf(_("Try `help %s' for more information.\n"),args->a0());
	    return 0;
	 }
	 break;
      case('x'):
	 APPEND_STRING(exclude,exclude_alloc,optarg);
	 break;
      case('i'):
	 APPEND_STRING(include,include_alloc,optarg);
	 break;
      case('R'):
	 flags|=MirrorJob::REVERSE|MirrorJob::ONLY_NEWER;
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
      case('?'):
	 parent->eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   if(getcwd(cwd,sizeof(cwd)/2)==0)
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
	    strcpy(cwd,arg);
	 else
	 {
	    strcat(cwd,"/");
	    strcat(cwd,arg);
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
	 strcat(cwd,"/");
	 char *arg=args->getnext();
	 if(arg)
	 {
	    if(arg[0]=='/')
	       strcpy(cwd,arg);
	    else
	       strcat(cwd,arg);
	    if(create_directories(arg)==-1)
	       return 0;
	 }
	 else
	 {
	    int len=strlen(cwd);
	    const char *base=basename_ptr(rcwd);
	    if(base[0]!='/')
	    {
	       strcat(cwd,base);
	       if(create_directories(cwd+len)==-1)
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

   j->SetPrec(prec);
   if(newer_than)
      j->SetNewerThan(newer_than);
   j->UseCache(use_cache);
   return j;

err_out:
   parent->eprintf("%s: %s: %s\n",args->a0(),err_tag,err);
   SMTask::Delete(j);
   return 0;
#undef args
}


#ifdef MODULE
CDECL void module_init()
{
   CmdExec::RegisterCommand("mirror",cmd_mirror);
}
#endif
