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
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "GetJob.h"
#include "misc.h"
#include "ArgV.h"
#include "url.h"

ResDecl res_clobber	("xfer:clobber",     "yes",ResMgr::BoolValidate,ResMgr::NoClosure);
ResDecl res_make_backup	("xfer:make-backup", "yes",ResMgr::BoolValidate,ResMgr::NoClosure);

#define super CopyJobEnv

#define NO_MODE ((mode_t)-1)

int   GetJob::Do()
{
   int m=STALL;

   if(cp && cp->Done() && !cp->Error())
   {
      // now we can delete old file, since there is new one
      RemoveBackupFile();
      if(file_mode!=NO_MODE && local)
	 chmod(local->full_name,file_mode);
   }
   if(super::Do()==MOVED)
      m=MOVED;
   return m;
}

FileCopyPeer *GetJob::NoProtoSrc(const char *src,bool from_local)
{
   if(from_local)
   {
      const char *f=(cwd && src[0]!='/') ? dir_file(cwd,src) : src;
      return FileCopyPeerFDStream::NewGet(f);
   }

   FileCopyPeerFA *s=new FileCopyPeerFA(session,src,FA::RETRIEVE);
   s->DontReuseSession();
   return s;
}
FileCopyPeer *GetJob::NoProtoDst(const char *dst,bool to_local)
{
   if(!to_local)
   {
      FileCopyPeerFA *s=new FileCopyPeerFA(session,dst,FA::STORE);
      s->DontReuseSession();
      return s;
   }

   int flags=O_WRONLY|O_CREAT|(cont?0:O_TRUNC);
   const char *f=(cwd && dst[0]!='/') ? dir_file(cwd,dst) : dst;
   if(!cont && res_make_backup.QueryBool(0))
   {
      /* rename old file if exists and size>0 */
      struct stat st;
      if(stat(f,&st)!=-1)
      {
	 if(st.st_size>0 && S_ISREG(st.st_mode))
	 {
	    if(!res_clobber.QueryBool(0))
	    {
	       eprintf(_("%s: %s: file already exists and xfer:clobber is unset\n"),op,dst);
	       errors++;
	       count++;
	       return 0;
	    }
	    backup_file=xstrdup(f,+1);
	    strcat(backup_file,"~");
	    if(rename(f,backup_file)!=0)
	    {
	       xfree(backup_file);
	       backup_file=0;
	    }
	    else
	    {
	       file_mode=st.st_mode;
	    }
	 }
      }
   }
   local=new FileStream(f,flags); // local is for pget.
   FileCopyPeerFDStream *p=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   p->DontDeleteStream();
   return p;
}

FileCopyPeer *GetJob::CreateCopyPeer(const char *path,FA::open_mode mode)
{
   ParsedURL url(path);
   if(!url.proto)
      return (mode==FA::STORE)
	 ? NoProtoDst(path,!reverse)
	 : NoProtoSrc(path,reverse);
   else if(!strcasecmp(url.proto,"file"))
      return (mode==FA::STORE)
	 ? NoProtoDst(url.path,true)
	 : NoProtoSrc(url.path,true);
   return new FileCopyPeerFA(&url,mode);
}

void GetJob::NextFile()
{
try_next:
   file_mode=NO_MODE;
   if(backup_file)
   {
      xfree(backup_file);
      backup_file=0;
   }
   if(local)
   {
      delete local;
      local=0;
   }

   if(!args)
      return;

   const char *src=args->getnext();
   const char *dst=args->getnext();
   if(!src || !dst)
   {
      SetCopier(0,0);
      return;
   }

   FileCopyPeer *dst_peer=CreateCopyPeer(dst,FA::STORE);
   if(!dst_peer)
      goto try_next;
   FileCopyPeer *src_peer=CreateCopyPeer(src,FA::RETRIEVE);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,cont);

   if(delete_files)
      c->RemoveSourceLater();
   if(remove_target_first)
      c->RemoveTargetFirst();

   SetCopier(c,src);
}

void GetJob::RemoveBackupFile()
{
   if(backup_file)
   {
      remove(backup_file);
      xfree(backup_file);
      backup_file=0;
   }
}

GetJob::GetJob(FileAccess *s,ArgV *a,bool c)
   : CopyJobEnv(s,a,c)
{
   delete_files=false;
   remove_target_first=false;
   backup_file=0;
   file_mode=NO_MODE;
   local=0;
   reverse=false;
}
GetJob::~GetJob()
{
   xfree(backup_file);
   if(local)
      delete local;
}
