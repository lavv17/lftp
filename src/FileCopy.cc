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
#include <assert.h>

#define skip_threshold 0x1000

int FileCopy::Do()
{
   int m=STALL;
   const char *b;
   int s;

   if(Error() || Done())
      return m;
   switch(state)
   {
   case(INITIAL):
      if(cont)
	 put->Seek(FILE_END);
      cont=true;  // after a failure we'll have to restart
      get->WantDate();
      get->WantSize();
      get->Suspend();
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
      get->Resume();
      state=DO_COPY;
      m=MOVED;
      /* fallthrough */
   case(DO_COPY):
      if(put->Error())
      {
      put_error:
	 SetError(put->ErrorText());
	 return MOVED;
      }
      if(get->Error())
      {
      get_error:
	 SetError(get->ErrorText());
	 return MOVED;
      }
      /* check if positions are correct */
      if(get->GetRealPos()!=put->GetRealPos())
      {
	 if(put->GetRealPos()<get->GetRealPos())
	 {
	    if(!get->CanSeek())
	    {
	       // we lose... How about a large buffer?
	       SetError("cannot seek on data source");
	       return MOVED;
	    }
	    get->Seek(put->GetRealPos());
	    return MOVED;
	 }
	 else // put->GetRealPos() > get->GetRealPos()
	 {
	    int skip=put->GetRealPos()-get->GetRealPos();
	    if(!put->CanSeek() || skip<skip_threshold)
	    {
	       // we have to skip some data
	       get->Get(&b,&s);
	       if(skip>s)
		  skip=s;
	       get->Skip(skip);
	       return MOVED;
	    }
	    put->Seek(get->GetRealPos());
	    return MOVED;
	 }
      }
      get->Get(&b,&s);
      if(b==0) // eof
      {
	 put->SetDate(get->GetDate());
	 put->PutEOF();
	 state=CONFIRM_WAIT;
	 return MOVED;
      }
      if(s==0)
	 return m;
      put->Put(b,s);
      get->Skip(s);
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
   return m;
}

void FileCopy::Init()
{
   get=0;
   put=0;
   state=INITIAL;
   max_buf=0x10000;
   cont=false;
   error_text=0;
}

FileCopy::FileCopy(FileCopyPeer *s,FileCopyPeer *d,bool c)
{
   Init();
   get=s;
   put=d;
   cont=c;
}
FileCopy::~FileCopy()
{
   if(get) delete get;
   if(put) delete put;
}
void FileCopy::SetError(const char *str)
{
   xfree(error_text);
   error_text=xstrdup(str);
   if(get) { delete get; get=0; }
   if(put) { delete put; put=0; }
}


// FileCopyPeer implementation
#undef super
#define super Buffer
void FileCopyPeer::Skip(int n)
{
   assert(n<=in_buffer);
   real_pos+=n;
   seek_pos+=n;
   super::Skip(n);
}
FileCopyPeer::FileCopyPeer(direction m)
{
   mode=m;
   want_size=false;
   want_date=false;
   size=NO_SIZE;
   date=NO_DATE;
   real_pos=0;
   seek_pos=0;
   can_seek=true;
   date_set=false;
}
FileCopyPeer::~FileCopyPeer()
{
}

// FileCopyPeerFA implementation
#undef super
#define super FileCopyPeer
int FileCopyPeerFA::Do()
{
   int m=STALL;
   int res;
   if(Done() || Error())
      return STALL;
   switch(mode)
   {
   case PUT:
      if(want_size)
      {
	 if(session->IsClosed())
	 {
	    info.file=file;
	    info.get_size=true;
	    info.get_time=false;
	    session->GetInfoArray(&info,1);
	    m=MOVED;
	 }
	 res=session->Done();
	 if(res==FA::IN_PROGRESS)
	    return m;
	 if(res<0)
	 {
	    session->Close();
	    SetSize(NO_SIZE);
	    return MOVED;
	 }
	 SetSize(info.size);
	 session->Close();
	 m=MOVED;
      }

      if(in_buffer==0)
      {
	 if(eof)
	 {
	    res=session->StoreStatus();
	    if(res==FA::OK)
	    {
	       // FIXME: set date for real.
	       date_set=true;
	       m=MOVED;
	    }
	    else if(res==FA::IN_PROGRESS)
	       return m;
	    else
	    {
	       if(res==FA::DO_AGAIN)
		  return m;
	       if(res==FA::STORE_FAILED)
	       {
		  session->Close();
		  Seek(FILE_END);
		  return MOVED;
	       }
	       SetError(session->StrError(res));
	       return MOVED;
	    }
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

void FileCopyPeerFA::Seek(long pos)
{
   if(pos==real_pos)
      return;
   session->Close();
   if(pos!=FILE_END)
   {
      session->Open(file,FAmode,pos);
      session->RereadManual();
   }
   else
   {
      WantSize();
   }
   super::Seek(pos);
}

int FileCopyPeerFA::Get_LL(int size)
{
   int res=0;

   if(session->IsClosed())
   {
      session->Open(file,FAmode,seek_pos);
      session->RereadManual();
   }
   if(session->GetRealPos()==0 && session->GetPos()>0)
   {
      can_seek=false;
      session->SeekReal();
   }
   if(real_pos+in_buffer!=session->GetPos())
   {
      Empty();
      real_pos=session->GetPos();
   }

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

int FileCopyPeerFA::Put_LL(const char *buf,int size)
{
   if(session->IsClosed())
   {
      if(seek_pos==FILE_END)
      {
	 if(size!=NO_SIZE && size!=NO_SIZE_YET)
	    seek_pos=size;
	 else
	    seek_pos=0;
      }
      session->Open(file,FAmode,seek_pos);
      real_pos=seek_pos;
   }
   if(real_pos+in_buffer!=session->GetPos())
   {
      Empty();
      can_seek=false;
      real_pos=session->GetPos();
      return 0;
   }

   int res=session->Write(buf,size);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      if(res==FA::STORE_FAILED)
      {
	 session->Close();
	 Seek(FILE_END);
	 return 0;
      }
      SetError(session->StrError(res));
      return -1;
   }
   return res;
}

FileCopyPeerFA::FileCopyPeerFA(FileAccess *s,const char *f,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET)
{
   FAmode=m;
   file=xstrdup(f);
   session=s;
}
FileCopyPeerFA::~FileCopyPeerFA()
{
   xfree(file);
}
