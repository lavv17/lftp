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
#include <math.h>
#include "FileCopy.h"
#include "url.h"
#include "log.h"
#include "misc.h"

#define skip_threshold 0x1000
#define debug(a) Log::global->Format a

ResDecl rate_period  ("xfer:rate-period","15", ResMgr::UNumberValidate,0);
ResDecl eta_period   ("xfer:eta-period", "120",ResMgr::UNumberValidate,0);
ResDecl res_eta_terse("xfer:eta-terse",  "yes",ResMgr::BoolValidate,0);

int FileCopy::Do()
{
   int m=STALL;
   const char *b;
   int s;
   int rate_add;

   if(Error() || Done())
      return m;
   switch(state)
   {
   pre_INITIAL:
      state=INITIAL;
      m=MOVED;
   case(INITIAL):
      if(put->NeedSizeDateBeforehand())
      {
	 if(get->GetSize()==NO_SIZE_YET || get->GetDate()==NO_DATE_YET)
	 {
	    put->Suspend();
	    get->Resume();
	    get->WantDate();
	    get->WantSize();
	    goto pre_GET_INFO_WAIT;
	 }
      }
      if(get->GetSize()!=NO_SIZE && get->GetSize()!=NO_SIZE_YET)
	 put->SetEntitySize(get->GetSize());
      else if(get->GetSize()==NO_SIZE_YET)
	 get->WantSize();
      if(get->GetDate()!=NO_DATE && get->GetDate()!=NO_DATE_YET)
	 put->SetDate(get->GetDate());
      else if(get->GetDate()==NO_DATE_YET)
	 get->WantDate();

      if(cont && put->CanSeek())
	 put->Seek(FILE_END);
      else
	 goto pre_DO_COPY;

      get->Suspend();
      put->Resume();
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
      start_time=now;
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
      put->Resume();
      if(put->GetSeekPos()==FILE_END)   // put position is not known yet.
      {
	 get->Suspend();
	 return m;
      }
      get->Resume();
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
	    debug((9,"copy: put rolled back to %ld, seeking get accordingly\n",
		     put->GetRealPos()));
	    debug((10,"copy: get position was %ld\n",get->GetRealPos()));
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
	       if(skip==0)
		  return m;
	       debug((9,"copy: skipping %d bytes on get to adjust to put\n",skip));
	       get->Skip(skip);
	       bytes_count+=skip;
	       return MOVED;
	    }
	    debug((9,"copy: get rolled back to %ld, seeking put accordingly\n",
		     get->GetRealPos()));
	    put->Seek(get->GetRealPos());
	    return MOVED;
	 }
      }
      if(put->Size()>max_buf)
	 get->Suspend(); // stall the get.
      get->Get(&b,&s);
      if(b==0) // eof
      {
	 debug((9,"copy: get hit eof\n"));
	 put->SetDate(get->GetDate());
	 put->PutEOF();
	 get->Suspend();
	 state=CONFIRM_WAIT;
	 return MOVED;
      }
      if(s==0)
      {
	 if(put->Size()==0)
	    put->Suspend();
	 return m;
      }
      rate_add=put_buf;

      put->Put(b,s);
      get->Skip(s);
      bytes_count+=s;

      put_buf=put->Buffered();
      rate_add-=put_buf-s;
      rate->Add(rate_add);
      rate_for_eta->Add(rate_add);
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
      debug((9,"copy: put confirmed store\n"));
      state=GET_DONE_WAIT;
      m=MOVED;
      end_time=now;
      delete put; put=0;
      /* fallthrough */
   case(GET_DONE_WAIT):
      if(get->Error())
	 goto get_error;
      if(!get->Done())
	 return m;
      debug((9,"copy: get is finished - all done\n"));
      state=ALL_DONE;
      delete get; get=0;
      return MOVED;

   pre_GET_INFO_WAIT:
      state=GET_INFO_WAIT;
      m=MOVED;
   case(GET_INFO_WAIT):
      if(get->Error())
	 goto get_error;
      if(get->GetSize()==NO_SIZE_YET || get->GetDate()==NO_DATE_YET)
	 return m;
      goto pre_INITIAL;

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
   rate        =new Speedometer((int)rate_period.Query(0));
   rate_for_eta=new Speedometer((int)eta_period.Query(0));
   put_buf=0;
   bytes_count=0;
   start_time=0;
   end_time=0;
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
   if(put)
      return put->GetRealPos() - put->Buffered();
   if(get)
      return get->GetRealPos();
   return 0;
}

int FileCopy::GetPercentDone()
{
   if(!get || !put)
      return 100;
   long size=get->GetSize();
   if(size==NO_SIZE || size==NO_SIZE_YET)
      return -1;
   if(size==0)
      return 0;
   return (put->GetRealPos() - put->Buffered())*100/size;
}
const char *FileCopy::GetPercentDoneStr()
{
   int pct=GetPercentDone();
   if(pct==-1)
      return "";
   static char buf[6];
   sprintf(buf,"(%d%%) ",pct);
   return buf;
}
float FileCopy::GetRate()
{
   if(!rate->Valid())
      return 0;
   return rate->Get();
}
const char *FileCopy::GetRateStr()
{
   return rate->GetStrS();
}
const char *FileCopy::GetETAStr()
{
   if(GetETA()<0)
      return "";
   return rate_for_eta->GetETAStrS(get->GetSize()-GetPos());
}

long FileCopy::GetETA()
{
   long size=get->GetSize();
   if(size<=0 || size<get->GetRealPos() || !rate_for_eta->Valid())
      return -1;
   return (long)((size-GetPos()) / rate_for_eta->Get() + 0.5);
}
const char *FileCopy::GetStatus()
{
   static char buf[256];
   const char *get_st=get->GetStatus();
   const char *put_st=put->GetStatus();
   if(get_st && get_st[0] && put_st && put_st[0])
      sprintf(buf,"[%s->%s]",get_st,put_st);
   else if(get_st && get_st[0])
      sprintf(buf,"[%s]",get_st);
   else if(put_st && put_st[0])
      sprintf(buf,"[%s]",put_st);
   else
      return "";
   return buf;
}

time_t FileCopy::GetTimeSpent()
{
   if(start_time==0 || end_time==0 || end_time<start_time)
      return 0;
   return end_time-start_time;
}

FgData *FileCopy::GetFgData(bool fg)
{
   // NOTE: only one of get/put can have FgData in this implementation.
   FgData *f=get->GetFgData(fg);
   if(f) return f;
   return put->GetFgData(fg);
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
   size=NO_SIZE_YET;
   e_size=NO_SIZE;
   date=NO_DATE_YET;
   seek_pos=0;
   can_seek=true;
   date_set=false;
   ascii=false;
   Suspend();  // don't do anything too early
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

      res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 m=MOVED;
      }
      else if(res<0)
	 return MOVED;
      if(in_buffer==0)
      {
	 if(eof)
	 {
	    if(date!=NO_DATE && date!=NO_DATE_YET)
	       session->SetDate(date);
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
		  if(can_seek && seek_pos>0)
		     Seek(FILE_END);
		  else
		     Seek(0);
		  return MOVED;
	       }
	       SetError(session->StrError(res));
	       return MOVED;
	    }
	    return m;
	 }
      }
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
   super::Seek(new_pos);
   session->Close();
   if(seek_pos!=FILE_END)
      OpenSession();
   else
      WantSize();
}

void FileCopyPeerFA::OpenSession()
{
   session->Open(file,FAmode,seek_pos);
   if(mode==PUT)
   {
      if(e_size!=NO_SIZE && e_size!=NO_SIZE_YET)
	 session->SetSize(e_size);
      if(date!=NO_DATE && date!=NO_DATE_YET)
	 session->SetDate(date);
   }
   session->RereadManual();
   if(ascii)
      session->AsciiTransfer();
   if(want_size)
      session->WantSize(&size);
   if(want_date)
      session->WantDate(&date);
}

int FileCopyPeerFA::Get_LL(int len)
{
   int res=0;

   if(session->IsClosed())
      OpenSession();

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
      if(ascii)
	 session->AsciiTransfer();
      pos=seek_pos;
   }
   long io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   if(len==0 && eof)
      return 0;

   int res=session->Write(buf,len);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      if(res==FA::STORE_FAILED)
      {
	 session->Close();
	 if(can_seek && seek_pos>0)
	    Seek(FILE_END);
	 else
	    Seek(0);
	 return 0;
      }
      SetError(session->StrError(res));
      return -1;
   }
   seek_pos+=res; // mainly to indicate that there was some output.
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
   reuse_later=true;
}
FileCopyPeerFA::~FileCopyPeerFA()
{
   session->Close();
   xfree(file);
   if(reuse_later && session)
      SessionPool::Reuse(session);
}

FileCopyPeerFA::FileCopyPeerFA(ParsedURL *u,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET)
{
   FAmode=m;
   file=xstrdup(u->path);
   session=FileAccess::New(u);
   reuse_later=true;
   if(!session)
   {
      SetError("no session"); // FIXME
   }
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
   eof=true;
   pos=0;
}
FileCopyPeerString::~FileCopyPeerString()
{
}
void FileCopyPeerString::Seek(long new_pos)
{
   assert(new_pos!=FILE_END);
   UnSkip(pos-new_pos);
   super::Seek(new_pos);
}

// Speedometer
#undef super
#define super SMTask
char Speedometer::buf[40];
Speedometer::Speedometer(int c)
{
   period=c;
   rate=0;
   last_second=now;
   start=now;
   last_bytes=0;
}
bool Speedometer::Valid()
{
   return now>start+2 && now<last_bytes+period;
}
int Speedometer::Do()
{
   if(Valid())
      Timeout(1000);
   return STALL;
}
float Speedometer::Get()
{
   Add(0);
   return rate;
}
void Speedometer::Add(int b)
{
   float div=period;
   if(start>now)
      start=now;  // time was adjusted?
   if(now<last_second)
      last_second=now;
   if(now-start<div)
      div=now-start+1;
   rate*=pow(double(div-1)/div,now-last_second);
   rate+=b/div;
   last_second=now;
   if(b>0)
      last_bytes=now;
}
const char *Speedometer::GetStr()
{
   buf[0]=0;
   float r=Get();
   if(r<1)
      return "";
   if(r<1024)
      sprintf(buf,_("%.0fb/s "),r);
   else if(r<1024*1024)
      sprintf(buf,_("%.1fK/s "),r/1024.);
   else
      sprintf(buf,_("%.2fM/s "),r/1024./1024.);
   return buf;
}
const char *Speedometer::GetETAStr(long size)
{
   buf[0]=0;

   if(!Valid() || Get()<1)
      return buf;

   long eta=long(size/Get()+.5);

   long eta2=0;
   long ueta=0;
   long ueta2=0;
   char letter=0;
   char letter2=0;

   // for translator: only first letter matters
   const char day_c=_("day")[0];
   const char hour_c=_("hour")[0];
   const char minute_c=_("minute")[0];
   const char second_c=_("second")[0];

   const char *tr_eta=_("eta:");

   if((bool)res_eta_terse.Query(0))
   {
      if(eta>=DAY)
      {
	 ueta=(eta+DAY/2)/DAY;
	 eta2=eta-ueta*DAY;
	 letter=day_c;
	 if(ueta<10)
	 {
	    letter2=hour_c;
	    ueta2=((eta2<0?eta2+DAY:eta2)+HOUR/2)/HOUR;
	    if(ueta2>0 && eta2<0)
	       ueta--;
	 }
      }
      else if(eta>=HOUR)
      {
	 ueta=(eta+HOUR/2)/HOUR;
	 eta2=eta-ueta*HOUR;
	 letter=hour_c;
	 if(ueta<10)
	 {
	    letter2=minute_c;
	    ueta2=((eta2<0?eta2+HOUR:eta2)+MINUTE/2)/MINUTE;
	    if(ueta2>0 && eta2<0)
	       ueta--;
	 }
      }
      else if(eta>=MINUTE)
      {
	 ueta=(eta+MINUTE/2)/MINUTE;
	 letter=minute_c;
      }
      else
      {
	 ueta=eta;
	 letter=second_c;
      }
      if(letter2 && ueta2>0)
	 sprintf(buf,"%s%ld%c%ld%c",tr_eta,ueta,letter,ueta2,letter2);
      else
	 sprintf(buf,"%s%ld%c",tr_eta,ueta,letter);
   }
   else // verbose eta (by Ben Winslow)
   {
      long unit;
      strcpy(buf, tr_eta);

      if(eta>=DAY)
      {
	 unit=eta/DAY;
	 sprintf(buf+strlen(buf), "%ld%c", unit, day_c);
      }
      if(eta>=HOUR)
      {
	 unit=(eta/HOUR)%24;
	 sprintf(buf+strlen(buf), "%ld%c", unit, hour_c);
      }
      if(eta>=MINUTE)
      {
	 unit=(eta/MINUTE)%60;
	 sprintf(buf+strlen(buf), "%ld%c", unit, minute_c);
      }
      unit=eta%60;
      sprintf(buf+strlen(buf), "%ld%c", unit, second_c);
   }
   return buf;
}
const char *Speedometer::GetStrS()
{
   GetStr();
   if(buf[0])
      strcat(buf," ");
   return buf;
}
const char *Speedometer::GetETAStrS(long s)
{
   GetETAStr(s);
   if(buf[0])
      strcat(buf," ");
   return buf;
}
