/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "buffer.h"
#include "xmalloc.h"
#include "FileAccess.h"

#define BUFFER_INC (8*1024) // should be power of 2

void Buffer::Get(const char **buf,int *size)
{
   if(in_buffer==0)
   {
      *size=0;
      if(eof)
	 *buf=0;
      else
	 *buf="";
      return;
   }
   *buf=buffer+buffer_ptr;
   *size=in_buffer;
}

void Buffer::Allocate(int size)
{
   if(in_buffer==0)
      buffer_ptr=0;

   if(buffer_allocated<in_buffer+size)
   {
      buffer_allocated=(in_buffer+size+(BUFFER_INC-1)) & ~(BUFFER_INC-1);
      buffer=(char*)xrealloc(buffer,buffer_allocated);
   }
   // could be round-robin, but this is easier
   if(buffer_ptr+in_buffer+size>buffer_allocated)
   {
      memmove(buffer,buffer+buffer_ptr,in_buffer);
      buffer_ptr=0;
   }
}

void Buffer::Put(const char *buf,int size)
{
   if(in_buffer==0)
   {
      buffer_ptr=0;

      int res=Put_LL(buf,size);
      if(res>=0)
      {
	 buf+=res;
	 size-=res;
      }
   }

   if(size==0)
      return;

   Allocate(size);

   memcpy(buffer+buffer_ptr+in_buffer,buf,size);
   in_buffer+=size;
}

void Buffer::Skip(int len)
{
   if(len>in_buffer)
      len=in_buffer;
   in_buffer-=len;
   buffer_ptr+=len;
}

int Buffer::Do()
{
   return STALL;
}

Buffer::Buffer()
{
   error_text=0;
   saved_errno=0;
   buffer=0;
   buffer_allocated=0;
   in_buffer=0;
   buffer_ptr=0;
   eof=false;
   broken=false;
}
Buffer::~Buffer()
{
   xfree(error_text);
   xfree(buffer);
}

// FileOutputBuffer implementation
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
FileOutputBuffer::FileOutputBuffer(FDStream *o)
{
   out=o;
   event_time=now;
}
FileOutputBuffer::~FileOutputBuffer()
{
   delete out;
}
int FileOutputBuffer::Do()
{
   if(Done() || Error())
      return STALL;
   if(in_buffer==0)
      return STALL;
   int res=Put_LL(buffer+buffer_ptr,in_buffer);
   if(res>0)
   {
      in_buffer-=res;
      buffer_ptr+=res;
      event_time=now;
      return MOVED;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
   int fd=out->getfd();
   if(fd>=0)
      block+=PollVec(fd,POLLOUT);
   else
      block+=TimeOut(1000);
   return STALL;
}
int FileOutputBuffer::Put_LL(const char *buf,int size)
{
   if(out->broken())
   {
      broken=true;
      return -1;
   }

   int fd=out->getfd();
   if(fd==-1)
   {
      if(out->error())
      {
      err:
	 error_text=xstrdup(out->error_text);
	 return -1;
      }
      event_time=now;
      return 0;
   }

   int res=write(fd,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN)
	 return 0;
      if(errno==EPIPE)
      {
	 broken=true;
	 return -1;
      }
      saved_errno=errno;
      out->MakeErrorText();
      goto err;
   }
   return res;
}

FgData *FileOutputBuffer::GetFgData(bool fg)
{
   if(out->getfd()!=-1)
      return new FgData(out->GetProcGroup(),fg);
   return 0;
}

bool FileOutputBuffer::Done()
{
   if(broken || Error()
   || (eof && Buffer::Done()))
      return out->Done(); // out->Done indicates if sub-process finished
   return false;
}

time_t FileOutputBuffer::EventTime()
{
   return event_time;
}

// FileInputBuffer implementation
#undef super
#define super Buffer
FileInputBuffer::FileInputBuffer(FDStream *i)
{
   in=i;
   in_FA=0;
   event_time=now;
}
FileInputBuffer::FileInputBuffer(FileAccess *i)
{
   in=0;
   in_FA=i;
   event_time=now;
}
FileInputBuffer::~FileInputBuffer()
{
   if(in)
      delete in;
}
int FileInputBuffer::Do()
{
   if(Done() || Error())
      return STALL;

   int res=Get_LL(0x4000);
   if(res>0)
   {
      in_buffer+=res;
      event_time=now;
      return MOVED;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
   if(eof)
   {
      event_time=now;
      return MOVED;
   }
   if(in)
   {
      int fd=in->getfd();
      if(fd>=0)
	 Block(fd,POLLIN);
      else
	 Timeout(1000);
   }
   return STALL;
}
int FileInputBuffer::Get_LL(int size)
{
   int res=0;

   if(in_FA)
   {
      Allocate(size);

      res=in_FA->Read(buffer+buffer_ptr+in_buffer,size);
      if(res<0)
      {
	 if(res==FA::DO_AGAIN)
	    return 0;
	 error_text=xstrdup(in_FA->StrError(res));
	 return -1;
      }
   }
   else if(in)
   {
      int fd=in->getfd();
      if(fd==-1)
      {
	 if(in->error())
	 {
	 err:
	    error_text=xstrdup(in->error_text);
	    return -1;
	 }
	 return 0;
      }

      Allocate(size);

      res=read(fd,buffer+buffer_ptr+in_buffer,size);
      if(res==-1)
      {
	 if(errno==EAGAIN || errno==EINTR)
	    return 0;
	 saved_errno=errno;
	 in->MakeErrorText();
	 goto err;
      }
   }
   if(res==0)
      eof=true;
   return res;
}

FgData *FileInputBuffer::GetFgData(bool fg)
{
   if(!in)
      return 0;
   if(in->getfd()!=-1)
      return new FgData(in->GetProcGroup(),fg);
   return 0;
}

bool FileInputBuffer::Done()
{
   if(broken || Error() || eof)
   {
      if(in_FA)
	 return true;
      return in->Done(); // in->Done indicates if sub-process finished
   }
   return false;
}

time_t FileInputBuffer::EventTime()
{
   if(suspended)
      return now;
   return event_time;
}

void FileInputBuffer::Suspend()
{
   if(in_FA)
      in_FA->Suspend();
   super::Suspend();
}
void FileInputBuffer::Resume()
{
   super::Resume();
   if(in_FA)
      in_FA->Resume();
}
