/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "buffer_ssl.h"

#if USE_SSL
# include "lftp_ssl.h"

// IOBufferSSL implementation
#undef super
#define super IOBuffer

int IOBufferSSL::Do()
{
   int m=STALL;

   if(Done() || Error())
      return m;

   if(mode==PUT && Size()==0)
   {
      // nothing to write, but may need to do handshake
      if(!ssl->handshake_done)
      {
	 if(Put_LL("",0)<0)
	    return MOVED;
	 if(ssl->handshake_done && eof)
	    ssl->shutdown();
      }
      if(ssl->handshake_done && !eof)
	 return m;
   }
   else
   {
      // cannot use want_mask before trying to read/write, since ssl can be shared
      if(!ssl->handshake_done || eof || Ready(ssl->fd,dir_mask()))
	 m|=super::Do();
   }
   Block(ssl->fd,block_mask());
   return m;
}

int IOBufferSSL::Get_LL(int size)
{
   int total=0;
   int max_read=0;
   char *space=GetSpace(size);
   while(total<size-max_read) {
      int res=ssl->read(space+total,size-total);
      if(res<0)
      {
	 if(res==ssl->RETRY) {
	    SetNotReady(ssl->fd,want_mask());
	    break;
	 } else { // error
	    SetError(ssl->error,ssl->fatal);
	    break;
	 }
      }
      if(res==0) {
	 eof=true;
	 break;
      }
      total+=res;
      if(max_read<res)
	 max_read=res;
   }
   return total;
}

int IOBufferSSL::Put_LL(const char *buf,int size)
{
   int res=ssl->write(buf,size);
   if(res<0)
   {
      if(res==ssl->RETRY) {
	 SetNotReady(ssl->fd,want_mask());
	 return 0;
      } else { // error
	 SetError(ssl->error,ssl->fatal);
	 return -1;
      }
   }
   return res;
}

int IOBufferSSL::PutEOF_LL()
{
   if(Size()==0)
      ssl->shutdown();
   return 0;
}

IOBufferSSL::~IOBufferSSL()
{
}

#endif // USE_SSL
