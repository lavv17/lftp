/*
 * lftp and utils
 *
 * Copyright (c) 1998-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "buffer_ssl.h"
#include "xmalloc.h"

#ifdef USE_SSL
# include <openssl/err.h>
# include "lftp_ssl.h"

// IOBufferSSL implementation
#undef super
#define super IOBuffer

bool IOBufferSSL::IsFatal(int res)
{
   if(SSL_get_error(ssl,res)==SSL_ERROR_SYSCALL
   && (ERR_get_error()==0 || TemporaryNetworkError(errno)))
      return false;
   return true;
}

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
      errno=0;
      int res=lftp_ssl_connect(ssl,hostname);
      if(res<=0)
      {
	 if(BIO_sock_should_retry(res))
	    goto blocks;
	 else if (SSL_want_x509_lookup(ssl))
	    return STALL;
	 else // error
	 {
	    SetError(lftp_ssl_strerror("SSL connect"),IsFatal(res));
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
	 EmbraceNewData(res);
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
   errno=0;
   int res=SSL_read(ssl,buffer+buffer_ptr+in_buffer,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return 0;
      else if (SSL_want_x509_lookup(ssl))
	 return 0;
      else // error
      {
	 SetError(lftp_ssl_strerror("SSL read"),IsFatal(res));
	 return -1;
      }
   }
   if(res==0)
      eof=true;
   return res;
}

int IOBufferSSL::Put_LL(const char *buf,int size)
{
   if(!ssl_connected)
      return 0;

   int res=0;

   errno=0;
   res=SSL_write(ssl,buf,size);
   if(res<0)
   {
      if(BIO_sock_should_retry(res))
	 return 0;
      else if (SSL_want_x509_lookup(ssl))
	 return 0;
      else // error
      {
	 SetError(lftp_ssl_strerror("SSL write"),IsFatal(res));
	 return -1;
      }
   }
   return res;
}

IOBufferSSL::IOBufferSSL(SSL *s,dir_t m,const char *h)
 : IOBuffer(m)
{
   ssl=s;
   ssl_connected=false;
   do_connect=false;
   close_later=false;
   hostname=xstrdup(h);
}
IOBufferSSL::~IOBufferSSL()
{
   xfree(hostname);
   if(close_later)
      SSL_free(ssl);
}

#endif // USE_SSL
