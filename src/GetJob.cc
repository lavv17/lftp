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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "GetJob.h"
#include "misc.h"

int   GetJob::Do()
{
   RateDrain();

   int m=STALL;
   int fd,res;

   if(!curr && args)
      NextFile();

   if(Done())
      return m;

   if(in_buffer==0 && got_eof)
   {
      session->Close();
      if(made_backup)
      {
	 // now we can delete old file, since there is new one
	 FileStream *f=(FileStream*)local; // we are sure it is FileStream
	 char *b=(char*)alloca(strlen(f->full_name)+2);
	 strcpy(b,f->full_name);
	 strcat(b,"~");
	 remove(b);
      }
      if(delete_files)
      {
	 if(!deleting)
	 {
	    deleting=true;
	    session->Open(curr,Ftp::REMOVE);
	    m=MOVED;
	 }
	 res=session->Done();
	 if(res<=0)
	 {
	    deleting=false;
	    m=MOVED;
	    if(res<0)
	       eprintf(_("remote rm(%s) - %s\n"),curr,session->StrError(res));
	 }
	 if(deleting)
	    return m;
      }
      if(file_time>=0)
	 local->setmtime(file_time);
      NextFile();
      m=MOVED;
      return m;
   }
   if(!got_eof)
   {
      if(session->IsClosed())
      {
	 if((fd=local->getfd())==-1)
	 {
	    if(!local->error())
	    {
	       block+=TimeOut(1000);
	       return m;
	    }
	    fprintf(stderr,"%s: %s\n",op,local->error_text);
	    failed++;
	    NextFile();
	    return MOVED;
	 }
	 if(cont)
	 {
	    offset=local->getsize_and_seek_end();
	    if(offset<0)
	       offset=0;
	 }
	 m=MOVED;
	 session->Open(curr,session->RETRIEVE,offset);
      	 if(file_time==(time_t)-2)
	 {
	    session->WantDate(&file_time);
	 }
	 session->WantSize(&size);
      }
      res=TryRead(session);
      if(res<0 && res!=Ftp::DO_AGAIN)
      {
	 local->remove_if_empty();
	 NextFile();
	 return MOVED;
      }
      else if(res>=0)
	 m=MOVED;
   }

   res=TryWrite(local);
   if(res<0)
   {
      NextFile();
      return MOVED;
   }
   if(res>0)
      m=MOVED;

   return m;
}

void GetJob::NextFile()
{
   if(!args)
      return;
   if(local)
   {
      delete local;
      local=0;
   }
   char *r=args->getnext();
   char *l=args->getnext();
   if(!r || !l)
   {
      XferJob::NextFile(0);
      return;
   }
   int flags=O_WRONLY|O_CREAT|(cont?0:O_TRUNC);
   const char *f=(saved_cwd && l[0]!='/') ? dir_file(saved_cwd,l) : l;
   made_backup=false;
   if(!cont)
   {
      /* rename old file if exists */
      struct stat st;
      if(stat(f,&st)!=-1)
      {
	 if(st.st_size>0)
	 {
	    char *b=(char*)alloca(strlen(f)+2);
	    strcpy(b,f);
	    strcat(b,"~");
	    if(rename(f,b)==0)
	       made_backup=true;
	 }
      }
   }
   local=new FileStream(f,flags);
   XferJob::NextFile(r);
   file_time=(time_t)-2;
   if(set_file_time!=(time_t)-1)
   {
      file_time=set_file_time;
      set_file_time=(time_t)-1;
   }
}
