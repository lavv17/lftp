/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "FileCopy.h"

int FileCopy::Do()
{
   int m=STALL;
   switch(state)
   {
   case(INITIAL):
      if(cont)
	 put->Seek(FILE_END);
      cont=true;  // after a failure we'll have to restart
      get->WantDate();
      get->WantSize();
      state=PUT_WAIT;
      m=MOVED;
      /* fallthrough */
   case(PUT_WAIT):
      if(put->Error())
	 goto put_error;
      if(!put->IOReady())
	 return m;
      /* now we know if put's seek failed. Seek get accordingly. */
      get->Seek(put->GetRealPos());
      state=DO_COPY;
      m=MOVED;
      /* fallthrough */
   case(DO_COPY):
      if(put->Error())
      {
      put_error:
	 ...
      }
      if(get->Error())
      {
      get_error:
	 ...
      }
      /* check if positions are correct */
      if(get->GetRealPos()!=put->GetRealPos())
      {
	 ...
      }
      get->Get(&b,&s);
      if(b==0) // eof
      {
	 put->SetDate(date);
	 put->PutEof();
	 state=CONFIRM_WAIT;
	 return MOVED;
      }
      if(s==0)
	 return m;
      put->Put(b,s);
      return MOVED;

   case(CONFIRM_WAIT):
      if(put->Error())
	 goto put_error;
      /* check if positions are correct */
      if(get->GetRealPos()!=put->GetRealPos())
      {
	 state=DO_COPY;
	 return MOVED;
      }
      if(!put->Done())
	 return m;
      state=GET_DONE_WAIT;
      m=MOVED;
      /* fallthrough */
   case(GET_DONE_WAIT):
      if(get->Error())
	 goto get_error;
      if(!get->Done())
	 return m;
      state=ALL_DONE;
      return MOVED;

   case(ALL_DONE):
      return m;
   }
}


int FileCopyPeerFA::Do()
{
   int m=STALL;
   int res;
   if(Done() || Error())
      return STALL;
   switch(mode)
   {
   case PUT:
      if(in_buffer==0)
      {
	 if(eof)
	 {
	    // FIXME: set date for real.
	    date_set=true;
	    m=MOVED;
	 }
	 return m;
      }
      res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 return MOVED;
      }
      if(res<0)
	 return MOVED;
      break;

   case GET:
      res=Get_LL(0x4000);
      if(res>0)
      {
	 in_buffer+=res;
	 SaveMaxCheck(0);
	 return MOVED;
      }
      if(res<0)
	 return MOVED;
      if(eof)
	 return MOVED;
      break;
   }
   return m;
}

bool FileCopyPeerFA::Done()
{
   if(broken || Error())
      return true;
   if(eof && in_buffer==0)
   {
      if(mode==PUT)
	 return date_set;
      return true;
   }
   return false;
}

void FileCopyPeerFA::Suspend()
{
   session->Suspend();
   super::Suspend();
}
void FileCopyPeerFA::Resume()
{
   super::Resume();
   session->Resume();
}
