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
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "FileCopy.h"

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
      if(!put->CanSeek())
	 goto pre_DO_COPY;
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
      if(get->CanSeek())
	 get->Seek(put->GetRealPos());
      get->Resume();
   pre_DO_COPY:
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
      if(put->Size()>max_buf)
	 get->Suspend(); // stall the get.
      else
	 get->Resume();
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
      delete put; put=0;
      /* fallthrough */
   case(GET_DONE_WAIT):
      if(get->Error())
	 goto get_error;
      if(!get->Done())
	 return m;
      state=ALL_DONE;
      delete get; get=0;
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

long FileCopy::GetPos()
{
   return get->GetRealPos();
}


// FileCopyPeer implementation
#undef super
#define super Buffer
void FileCopyPeer::SetSize(long s)
{
   want_size=false;
   size=s;
   if(seek_pos==FILE_END)
   {
      if(size!=NO_SIZE && size!=NO_SIZE_YET)
	 seek_pos=size;
      else
	 seek_pos=0;
   }
}
void FileCopyPeer::SetDate(time_t d)
{
   want_date=false;
   date=d;
   if(date==NO_DATE || date==NO_DATE_YET)
      date_set=true;
   else
      date_set=false;
}

bool FileCopyPeer::Done()
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

FileCopyPeer::FileCopyPeer(direction m)
{
   mode=m;
   want_size=false;
   want_date=false;
   size=NO_SIZE;
   date=NO_DATE;
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
	       session->Close();
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
	    return m;
	 }
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
      if(eof)
	 return m;
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
      {
	 session->Close();
	 return MOVED;
      }
      break;
   }
   return m;
}

bool FileCopyPeerFA::IOReady()
{
   if(seek_pos==0)
      return true;
   if(seek_pos==FILE_END && size==NO_SIZE_YET)
      return false;
   return session->IOReady();
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

void FileCopyPeerFA::Seek(long new_pos)
{
   if(pos==new_pos)
      return;
   session->Close();
   if(new_pos!=FILE_END)
   {
      session->Open(file,FAmode,new_pos);
      session->RereadManual();
      if(want_size)
	 session->WantSize(&size);
      if(want_date)
	 session->WantDate(&date);
   }
   else
   {
      WantSize();
   }
   super::Seek(new_pos);
}

int FileCopyPeerFA::Get_LL(int len)
{
   int res=0;

   if(session->IsClosed())
   {
      session->Open(file,FAmode,seek_pos);
      session->RereadManual();
      if(want_size)
	 session->WantSize(&size);
      if(want_date)
	 session->WantDate(&date);
   }
   long io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   Allocate(len);

   res=session->Read(buffer+buffer_ptr+in_buffer,len);
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

int FileCopyPeerFA::Put_LL(const char *buf,int len)
{
   if(session->IsClosed())
   {
      session->Open(file,FAmode,seek_pos);
      pos=seek_pos;
   }
   long io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   if(len==0)
      return 0;

   int res=session->Write(buf,len);
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

long FileCopyPeerFA::GetRealPos()
{
   if(mode==PUT)
   {
      if(pos-in_buffer!=session->GetPos())
      {
	 Empty();
	 can_seek=false;
	 pos=session->GetPos();
      }
   }
   else
   {
      if(session->GetRealPos()==0 && session->GetPos()>0)
      {
	 can_seek=false;
	 session->SeekReal();
      }
      if(pos+in_buffer!=session->GetPos())
      {
	 Empty();
	 pos=session->GetPos();
      }
   }
   return pos;
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
   session->Close();
   xfree(file);
}


// FileCopyPeerFDStream
#undef super
#define super FileCopyPeer
FileCopyPeerFDStream::FileCopyPeerFDStream(FDStream *o,direction m)
   : FileCopyPeer(m)
{
   stream=o;
   seek_base=0;
   can_seek=stream->can_seek();
   if(can_seek && stream->fd!=-1)
   {
      seek_base=lseek(stream->fd,0,SEEK_CUR);
      if(seek_base==-1)
      {
	 can_seek=false;
	 seek_base=0;
      }
   }
}
FileCopyPeerFDStream::~FileCopyPeerFDStream()
{
   if(stream) delete stream;
}
int FileCopyPeerFDStream::getfd()
{
   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
      {
	 SetError(stream->error_text);
	 block+=NoWait();
      }
      else
      {
	 Timeout(1000);
      }
   }
   return fd;
}
int FileCopyPeerFDStream::Do()
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
	    if(date!=NO_DATE && date!=NO_DATE_YET)
	    {
	       if(getfd()==-1)
		  return m;
	       stream->setmtime(date);
	    }
	    date_set=true;
	    m=MOVED;
	    return m;
	 }
	 if(seek_pos==0)
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
      if(eof)
	 return m;
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

bool FileCopyPeerFDStream::IOReady()
{
   return seek_pos==0 || stream->fd!=-1;
}

void FileCopyPeerFDStream::Seek(long new_pos)
{
   if(pos==new_pos)
      return;
   super::Seek(new_pos);
   if(getfd()==-1)
      return;
   if(new_pos==FILE_END)
   {
      new_pos=lseek(getfd(),0,SEEK_END);
      if(new_pos==-1)
      {
	 can_seek=false;
	 return;
      }
      SetSize(new_pos);
      if(new_pos>seek_base)
	 new_pos-=seek_base;
      else
	 new_pos=0;
      pos=new_pos;
   }
   else
   {
      if(lseek(getfd(),new_pos+seek_base,SEEK_SET)==-1)
      {
	 can_seek=false;
	 return;
      }
      pos=new_pos;
   }
}

int FileCopyPeerFDStream::Get_LL(int len)
{
   int res=0;

   int fd=getfd();
   if(fd==-1)
      return 0;

   if(want_date || want_size)
   {
      struct stat st;
      if(fstat(fd,&st)==-1)
      {
	 SetDate(NO_DATE);
	 SetSize(NO_SIZE);
      }
      else
      {
	 SetDate(st.st_mtime);
	 SetSize(st.st_size);
      }
   }

   Allocate(len);

   res=read(fd,buffer+buffer_ptr+in_buffer,len);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
      {
	 Block(fd,POLLIN);
	 return 0;
      }
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   if(res==0)
      eof=true;
   return res;
}

int FileCopyPeerFDStream::Put_LL(const char *buf,int len)
{
   if(len==0)
      return 0;

   int fd=getfd();
   if(fd==-1)
      return 0;

   int res=write(fd,buf,len);
   if(res<0)
   {
      if(errno==EAGAIN || errno==EINTR)
      {
	 Block(fd,POLLOUT);
	 return 0;
      }
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   return res;
}
FgData *FileCopyPeerFDStream::GetFgData(bool fg)
{
   if(getfd()!=-1)
      return new FgData(stream->GetProcGroup(),fg);
   return 0;
}

// FileCopyPeerString
#undef super
#define super FileCopyPeer
FileCopyPeerString::FileCopyPeerString(const char *s, int len)
   : super(GET)
{
   if(len==-1)
      len=strlen(s);
   Put(s,len);
}
FileCopyPeerString::~FileCopyPeerString()
{
}
void FileCopyPeerString::Seek(long new_pos)
{
   assert(new_pos!=FILE_END);
   UnSkip(pos-new_pos);
}
