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
#include "lftp_ssl.h"

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

void Buffer::GetSaved(const char **buf,int *size) const
{
   if(!save)
   {
      *size=0;
      *buf=0;
      return;
   }
   *buf=buffer;
   *size=buffer_ptr+in_buffer;
}

void Buffer::SaveRollback(int p)
{
   if(buffer_ptr<p)
      save=false;
   if(!save)
   {
      buffer_ptr=0;
      in_buffer=0;
   }
   else
   {
      buffer_ptr=p;
      in_buffer=0;
   }
}

void Buffer::Allocate(int size)
{
   if(in_buffer==0 && !save)
      buffer_ptr=0;

   int in_buffer_real=in_buffer;
   if(save)
      in_buffer_real+=buffer_ptr;

   if(buffer_allocated<in_buffer_real+size)
   {
      buffer_allocated=(in_buffer_real+size+(BUFFER_INC-1)) & ~(BUFFER_INC-1);
      buffer=(char*)xrealloc(buffer,buffer_allocated);
   }
   // could be round-robin, but this is easier
   if(!save && buffer_ptr+in_buffer+size>buffer_allocated)
   {
      memmove(buffer,buffer+buffer_ptr,in_buffer);
      buffer_ptr=0;
   }
}

void Buffer::SaveMaxCheck(int size)
{
   if(save && buffer_ptr+size>save_max)
      save=false;
}

void Buffer::Put(const char *buf,int size)
{
   SaveMaxCheck(size);

   if(in_buffer==0 && !save)
   {
      buffer_ptr=0;

      if(size>=1024)
      {
	 int res=Put_LL(buf,size);
	 if(res>=0)
	 {
	    buf+=res;
	    size-=res;
	    pos+=res;
	 }
      }
   }

   if(size==0)
      return;

   Allocate(size);

   memcpy(buffer+buffer_ptr+in_buffer,buf,size);
   in_buffer+=size;
   pos+=size;
}

void Buffer::Skip(int len)
{
   if(len>in_buffer)
      len=in_buffer;
   in_buffer-=len;
   buffer_ptr+=len;
   pos+=len;
}
void Buffer::UnSkip(int len)
{
   if(len>buffer_ptr)
      len=buffer_ptr;
   in_buffer+=len;
   buffer_ptr-=len;
   pos-=len;
}

void Buffer::Empty()
{
   in_buffer=0;
   buffer_ptr=0;
   if(save_max>0)
      save=true;
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
   save=false;
   save_max=0;
   pos=0;
}
Buffer::~Buffer()
{
   xfree(error_text);
   xfree(buffer);
}

void Buffer::SetError(const char *e)
{
   xfree(error_text);
   error_text=xstrdup(e);
}
#if 0
void Buffer::SetError2(const char *e1,const char *e2)
{
   xfree(error_text);
   int len1=strlen(e1);
   int len2=strlen(e2);
   error_text=(char*)xmalloc(len1+len2+1);
   strcpy(error_text,e1);
   strcat(error_text,e2);
}
#endif

// IOBufferFDStream implementation
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#undef super
#define super IOBuffer
int IOBufferFDStream::Do()
{
   if(Done() || Error())
      return STALL;
   int res=0;
   switch(mode)
   {
   case PUT:
      if(in_buffer==0)
	 return STALL;
      res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 event_time=now;
	 return MOVED;
      }
      break;

   case GET:
      res=Get_LL(GET_BUFSIZE);
      if(res>0)
      {
	 in_buffer+=res;
	 SaveMaxCheck(0);
	 event_time=now;
	 return MOVED;
      }
      if(eof)
      {
	 event_time=now;
	 return MOVED;
      }
      break;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
   int fd=stream->getfd();
   if(fd>=0)
      Block(fd,mode==PUT?POLLOUT:POLLIN);
   else
      TimeoutS(1);
   return STALL;
}
int IOBufferFDStream::Put_LL(const char *buf,int size)
{
   if(stream->broken())
   {
      broken=true;
      return -1;
   }

   int res=0;

   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
	 goto stream_err;
      event_time=now;
      return 0;
   }

   res=write(fd,buf,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return 0;
      if(errno==EPIPE)
      {
	 broken=true;
	 return -1;
      }
      saved_errno=errno;
      stream->MakeErrorText();
      goto stream_err;
   }
   return res;

stream_err:
   SetError(stream->error_text);
   return -1;
}

int IOBufferFDStream::Get_LL(int size)
{
   int res=0;

   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
	 goto stream_err;
      return 0;
   }

   Allocate(size);

   res=read(fd,buffer+buffer_ptr+in_buffer,size);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 return 0;
      saved_errno=errno;
      stream->MakeErrorText();
      goto stream_err;
   }

   if(res==0)
      eof=true;
   return res;

stream_err:
   SetError(stream->error_text);
   return -1;
}

FgData *IOBufferFDStream::GetFgData(bool fg)
{
   if(stream->getfd()!=-1)
      return new FgData(stream->GetProcGroup(),fg);
   return 0;
}

bool IOBufferFDStream::Done()
{
   if(super::Done())
      return stream->Done(); // stream->Done indicates if sub-process finished
   return false;
}

// FileInputBuffer implementation
#undef super
#define super IOBuffer
IOBufferFileAccess::~IOBufferFileAccess()
{
   session->Resume();
   session->Close();
}
int IOBufferFileAccess::Do()
{
   if(Done() || Error())
      return STALL;

   int res=Get_LL(GET_BUFSIZE);
   if(res>0)
   {
      in_buffer+=res;
      SaveMaxCheck(0);
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
   return STALL;
}
int IOBufferFileAccess::Get_LL(int size)
{
   int res=0;

   Allocate(size);

   res=session->Read(buffer+buffer_ptr+in_buffer,size);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      SetError(session->StrError(res));
      return -1;
   }
   if(res==0)
      eof=true;
   return res;
}

void IOBufferFileAccess::Suspend()
{
   session->Suspend();
   super::Suspend();
}
void IOBufferFileAccess::Resume()
{
   super::Resume();
   session->Resume();
}

#ifdef USE_SSL
# include <openssl/err.h>

// IOBufferSSL implementation
#undef super
#define super IOBuffer
int IOBufferSSL::Do()
{
   if(Done() || Error())
      return STALL;

   int res=0;

   if(!ssl_connected && SSL_is_init_finished(ssl))
      ssl_connected=true;
   if(!ssl_connected)
   {
      if(!do_connect)
	 return STALL;
      int res=SSL_connect(ssl);
      if(res<=0)
      {
	 if(BIO_sock_should_retry(res))
	    goto blocks;
	 else if (SSL_want_x509_lookup(ssl))
	    return STALL;
	 else // error
	 {
	    SetError(lftp_ssl_strerror("SSL connect"));
	    return MOVED;
	 }
      }
      ssl_connected=true;
      event_time=now;
   }
   switch(mode)
   {
   case PUT:
      if(in_buffer==0)
	 return STALL;
      res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 event_time=now;
	 return MOVED;
      }
      break;

   case GET:
      res=Get_LL(GET_BUFSIZE);
      if(res>0)
      {
	 in_buffer+=res;
	 SaveMaxCheck(0);
	 event_time=now;
	 return MOVED;
      }
      if(eof)
      {
	 event_time=now;
	 return MOVED;
      }
      break;
   }
   if(res<0)
   {
      event_time=now;
      return MOVED;
   }
blocks:
   if(SSL_want_read(ssl))
      Block(SSL_get_fd(ssl),POLLIN);
   if(SSL_want_write(ssl))
      Block(SSL_get_fd(ssl),POLLOUT);
   return STALL;
}

int IOBufferSSL::Get_LL(int size)
{
   if(!ssl_connected)
      return 0;
   Allocate(size);
   int res=SSL_read(ssl,buffer+buffer_ptr+in_buffer,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return 0;
      else if (SSL_want_x509_lookup(ssl))
	 return 0;
      else // error
      {
	 SetError(lftp_ssl_strerror("SSL read"));
	 return -1;
      }
   }
   return res;
}

int IOBufferSSL::Put_LL(const char *buf,int size)
{
   if(!ssl_connected)
      return 0;

   int res=0;

   res=SSL_write(ssl,buf,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return 0;
      else if (SSL_want_x509_lookup(ssl))
	 return 0;
      else // error
      {
	 SetError(lftp_ssl_strerror("SSL write"));
	 return -1;
      }
   }
   return res;
}

IOBufferSSL::~IOBufferSSL()
{
   if(close_later)
   {
      SSL_free(ssl);
   }
}

#endif // USE_SSL
