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

#include <config.h>
#include "PutJob.h"
#include <assert.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "misc.h"

int   PutJob::Do()
{
   RateDrain();

   int m=STALL;
   int fd;
   int res;

   if(!curr && args)
      NextFile();

   if(Done())
      return m;

   // now deal with next todo
   if(remote_size==-1)
   {
      if(session->IsClosed())
      {
	 m=MOVED;
	 info.file=curr;
	 info.get_size=true;
	 info.get_time=false;
	 session->GetInfoArray(&info,1);
      }
      int res=session->Done();
      if(res==FA::IN_PROGRESS)
      	 return m;
      m=MOVED;
      if(res<0)
	 remote_size=-2;
      else
	 remote_size=(info.size<0?-2:info.size);
      session->Close();
   }

   // now we can get to data...
   if(session->IsClosed())
   {
      if(size!=-1)
      {
	 if(remote_size>=size)	  // assume done
	 {
	    NextFile();
	    return MOVED;
	 }
	 m=MOVED;
	 session->Open(curr,FA::STORE,remote_size<0?0:remote_size);
	 if(size>=0)
	    session->SetSize(size);
      }
   }
   // in store mode position can jump back
   if(session->IsOpen() && session->GetPos()!=offset)
   {
      got_eof=false;
      offset+=in_buffer;
      CountBytes(in_buffer);
      in_buffer=0;
   }

   if(got_eof && in_buffer==0)
   {
      res=session->StoreStatus();
remote_error:
      if(res==FA::DO_AGAIN)
	 return m;
      if(res==FA::IN_PROGRESS)
	 return m;
      m=MOVED;
      if(res==FA::STORE_FAILED)
      {
	 offset+=in_buffer;
	 CountBytes(in_buffer);
	 in_buffer=0;	// flush buffer
	 remote_size=-1;
	 session->Close();
	 return m;
      }
      if(res!=FA::OK)
      {
	 fprintf(stderr,"%s: %s\n",op,session->StrError(res));
	 failed++;
	 NextFile();
	 return MOVED;
      }
      if(delete_files)
	 remove(local->name); // name is absolute path
      NextFile();
      m=MOVED;
      return m;
   }
   if(!got_eof)
   {
      if(buffer==0)
      {
	 // allocate buffer
	 buffer=(char*)xmalloc(buffer_size=0x4000);
      }

      if(in_buffer==buffer_size)
	 goto try_write;

      fd=local->getfd();
      if(fd==-1)
      {
	 if(!local->error())
	 {
	    block+=TimeOut(1000);
	    return m;
	 }
	 fprintf(stderr,"%s: %s\n",op,local->error_text);
	 failed++;
	 NextFile();
	 m=MOVED;
	 return m;
      }
      if(session->IsOpen() && session->GetPos()!=offset)
      {
	 long diff=session->GetPos()-offset;

	 offset+=diff;

	 // don't subtract too much.
	 if(offset<remote_size && offset-diff>=remote_size)
	    diff+=remote_size-offset;

	 CountBytes(diff);

	 res=lseek(fd,offset,SEEK_SET);
	 if(res==-1)
	 {
	    perror(local->name);
	    failed++;
	    NextFile();
	    return MOVED;
	 }
      }

      if(size==-1 || size<offset)
      {
	 struct stat st;
	 res=fstat(fd,&st);
	 if(res==-1)
	    size=-2;
	 else
	 {
	    size=st.st_size;
	    session->SetSize(size);
	    session->SetDate(st.st_mtime);
	 }
      }

      block+=PollVec(fd,POLLIN);
      struct pollfd pfd={fd,POLLIN};
      int res=poll(&pfd,1,0);
      if(res==1 && pfd.revents&(POLLIN|POLLNVAL))
      {
	 res=read(fd,buffer+in_buffer,buffer_size-in_buffer);
	 if(res==-1)
	 {
	    perror(local->name);
	    failed++;
	    NextFile();
	    return MOVED;
	 }
	 if(res==0)
	 {
	    // EOF
	    got_eof=true;
	 }
	 in_buffer+=res;
      }
   }

try_write:
   if(in_buffer==0)
   {
      if(!got_eof)
	 session->Suspend();
      return MOVED;
   }
   res=session->Write(buffer,in_buffer);
   if(res==FA::DO_AGAIN)
      return m;
   m=MOVED;
   if(res<0)
      goto remote_error;

   in_buffer-=res;
   memmove(buffer,buffer+res,in_buffer);

   offset+=res;
   CountBytes(res);

   return m;
}

void PutJob::NextFile()
{
   if(local)
   {
      delete local;
      local=0;
   }
   char *l=args->getnext();
   char *r=args->getnext();
   if(!r || !l)
   {
      XferJob::NextFile(0);
      return;
   }
   if(saved_cwd && l[0]!='/')
      local=new FileStream(dir_file(saved_cwd,l),O_RDONLY);
   else
      local=new FileStream(l,O_RDONLY);
   XferJob::NextFile(r);
   if(cont)
      remote_size=-1;
   else
      remote_size=-2;	// -2 means no file
}
