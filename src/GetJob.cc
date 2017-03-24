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

FileCopyPeer *GetJob::SrcLocal(const char *src)
{
   const char *f=(cwd && src[0]!='/') ? dir_file(cwd,src) : src;
   return FileCopyPeerFDStream::NewGet(f);
}
FileCopyPeer *GetJob::DstLocal(const char *dst)
{
   bool clobber=(cont || QueryBool("xfer:clobber",0));
   int flags=O_WRONLY|O_CREAT|(truncate_target_first?O_TRUNC:0)|(clobber?0:O_EXCL);
   dst=expand_home_relative(dst);
   const char *f=(cwd && dst[0]!='/') ? dir_file(cwd,dst) : dst;

   FileStream *local=new FileStream(f,flags);
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
   if(IsRemoteNonURL(url,mode)) {
      if(parallel>1) // need to clone the session for parallel transfers
	 return new FileCopyPeerFA(session->Clone(),path,mode);
      return new FileCopyPeerFA(session,path,mode);
   }
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
   if(!args)
      return;

   const char *src=args->getnext();
   const char *dst=args->getnext();
   if(!src || !dst)
      return;

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

   AddCopier(c,src);
}

GetJob::GetJob(FileAccess *s,ArgV *a,bool c)
   : CopyJobEnv(s,a,c)
{
   make_dirs=false;
   delete_files=false;
   remove_target_first=false;
   truncate_target_first=!cont;
   reverse=false;
}
