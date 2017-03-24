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
#include <fcntl.h>
#include <stddef.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "GetJob.h"
#include "misc.h"
#include "ArgV.h"
#include "url.h"

#define super CopyJobEnv

#define NO_MODE ((mode_t)-1)

int   GetJob::Do()
{
   int m=STALL;

   if(cp && cp->Done())
   {
      if(cp->Error()) {
	 // in case of errors, move the backup to original location
	 if(local && backup_file)
	    rename(backup_file,local->full_name);
      } else {
	 // now we can delete the old file, since there is a new one
	 RemoveBackupFile();
	 if(file_mode!=NO_MODE && local)
	    chmod(local->full_name,file_mode);
      }
   }
   if(super::Do()==MOVED)
      m=MOVED;
   return m;
}

FileCopyPeer *GetJob::SrcLocal(const char *src)
{
   const char *f=(cwd && src[0]!='/') ? dir_file(cwd,src) : src;
   return FileCopyPeerFDStream::NewGet(f);
}
FileCopyPeer *GetJob::DstLocal(const char *dst)
{
   bool clobber=(cont || QueryBool("xfer:clobber",0));
   int flags=O_WRONLY|O_CREAT|(truncate_target_first?O_TRUNC:0);
   dst=expand_home_relative(dst);
   const char *f=(cwd && dst[0]!='/') ? dir_file(cwd,dst) : dst;

   struct stat st;
   if(stat(f,&st)!=-1)
   {
      if(st.st_size>0 && S_ISREG(st.st_mode))
      {
	 if(!clobber)
	 {
	    eprintf(_("%s: %s: file already exists and xfer:clobber is unset\n"),op,dst);
	    errors++;
	    count++;
	    return 0;
	 }
	 if(truncate_target_first && QueryBool("xfer:make-backup",0))
	 {
	    /* rename old file if exists and size>0 */
	    xstring_ca suffix(xstrftime(Query("xfer:backup-suffix",0),now));
	    backup_file.set(f).append(suffix);
	    if(rename(f,backup_file)!=0)
	       backup_file.set(0);
	    else
	       file_mode=st.st_mode;
	 }
      }
   }
   local=new FileStream(f,flags); // local is for pget.
   FileCopyPeerFDStream *p=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   p->CloseWhenDone();
   return p;
}

bool GetJob::IsRemoteNonURL(const ParsedURL &url,FA::open_mode mode)
{
   // store & put || !store & get
   return (!url.proto && ((mode==FA::STORE)^!reverse));
}
bool GetJob::IsLocalNonURL(const ParsedURL &url,FA::open_mode mode)
{
   // store & get || !store & put
   return (!url.proto && ((mode==FA::STORE)^reverse));
}
bool GetJob::IsLocal(const ParsedURL &url)
{
   return !url.proto || !strcasecmp(url.proto,"file");
}
// create copy peer from a cloned session
FileCopyPeer *GetJob::CreateCopyPeer(FileAccess *session,const char *path,FA::open_mode mode)
{
   ParsedURL url(path,true);
   if(IsRemoteNonURL(url,mode))
      return new FileCopyPeerFA(session,path,mode);
   Delete(session);	// delete cloned session.
   return CreateCopyPeer(url,path,mode);
}
// create copy peer using a session reference
FileCopyPeer *GetJob::CreateCopyPeer(const FileAccessRef& session,const char *path,FA::open_mode mode)
{
   ParsedURL url(path,true);
   if(IsRemoteNonURL(url,mode))
      return new FileCopyPeerFA(session,path,mode);
   return CreateCopyPeer(url,path,mode);
}
FileCopyPeer *GetJob::CreateCopyPeer(const ParsedURL &url,const char *path,FA::open_mode mode)
{
   if(IsLocalNonURL(url,mode))
      return CreateCopyPeer(path,mode);
   if(IsLocal(url))
      return CreateCopyPeer(url.path,mode);
   return new FileCopyPeerFA(&url,mode);
}
FileCopyPeer *GetJob::CreateCopyPeer(const char *path,FA::open_mode mode)
{
   return mode==FA::STORE ? DstLocal(path) : SrcLocal(path);
}

void GetJob::NextFile()
{
try_next:
   file_mode=NO_MODE;
   backup_file.set(0);
   local=0;

   if(!args)
      return;

   const char *src=args->getnext();
   const char *dst=args->getnext();
   if(!src || !dst)
   {
      SetCopier(0,0);
      return;
   }

   FileCopyPeer *dst_peer=CreateCopyPeer(session,dst,FA::STORE);
   if(!dst_peer)
      goto try_next;
   if(make_dirs)
      dst_peer->MakeTargetDir();
   FileCopyPeer *src_peer=CreateCopyPeer(session,src,FA::RETRIEVE);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,cont);

   if(delete_files)
      c->RemoveSourceLater();
   if(remove_target_first)
      c->RemoveTargetFirst();

   SetCopier(c,src);
}

void GetJob::RemoveBackupFile()
{
   if(backup_file && !QueryBool("xfer:keep-backup",0))
   {
      remove(backup_file);
      backup_file.set(0);
   }
}

GetJob::GetJob(FileAccess *s,ArgV *a,bool c)
   : CopyJobEnv(s,a,c)
{
   make_dirs=false;
   delete_files=false;
   remove_target_first=false;
   truncate_target_first=!cont;
   file_mode=NO_MODE;
   reverse=false;
}
