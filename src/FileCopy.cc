/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* FileCopyPeer behaviour:
    1) when suspended, does nothing
    2) tries to read some data at seek_pos, sets pos to position of Get (get).
    2.5) tries to position to seek_pos and gets ready to write (put).
    3) if it cannot seek to seek_pos, changes pos to what it can seek.
    4) if it knows that it cannot seek to pos>0, CanSeek()==false
    5) if it knows that it cannot seek to pos==0, CanSeek0()==false
    6) it tries to get date/size if told to. (get)
    7) it sets date on the file if eof is reached and date is known (put).
    8) if put needs size/date before it writes data, NeedSizeDateBeforehand()==true.
 */

#include <config.h>
#include <assert.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <math.h>
#include "FileCopy.h"
#include "url.h"
#include "log.h"
#include "misc.h"
#include "LsCache.h"

#define skip_threshold 0x1000
#define debug(a) Log::global->Format a

ResDecl rate_period  ("xfer:rate-period","15", ResMgr::UNumberValidate,0);
ResDecl eta_period   ("xfer:eta-period", "120",ResMgr::UNumberValidate,0);
ResDecl res_eta_terse("xfer:eta-terse",  "yes",ResMgr::BoolValidate,0);

// FileCopy
#define super SMTask

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
	    get->WantSize();
	    if(put->NeedDate())
	       get->WantDate();
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
      {
	 if(put->NeedDate())
	    get->WantDate();
      }

      if(cont && put->CanSeek())
	 put->Seek(FILE_END);
      else
      {
	 if(put->range_start>0 && put->CanSeek())
	    put->Seek(put->range_start);
	 if(get->range_start>0 && get->CanSeek())
	    get->Seek(get->range_start);
	 goto pre_DO_COPY;
      }

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
      RateReset();
      state=DO_COPY;
      m=MOVED;
      /* fallthrough */
   case(DO_COPY): {
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
      if(put->Broken())
      {
	 debug((9,"copy: put is broken"));
	 goto pre_GET_DONE_WAIT;
      }
      put->Resume();
      if(put->GetSeekPos()==FILE_END)   // put position is not known yet.
      {
	 get->Suspend();
	 return m;
      }
      get->Resume();
      if(fail_if_cannot_seek && (get->GetRealPos()<get->range_start
			      || put->GetRealPos()<put->range_start
			      || get->GetRealPos()!=put->GetRealPos()))
      {
	 SetError("seek failed");
	 return MOVED;
      }
      long lbsize=0;
      if(line_buffer)
	 lbsize=line_buffer->Size();
      /* check if positions are correct */
      long get_pos=get->GetRealPos()-get->range_start;
      long put_pos=put->GetRealPos()-put->range_start;
      if(get_pos-lbsize!=put_pos)
      {
	 if(line_buffer)
	    line_buffer->Empty();
	 if(get_pos==put_pos)
	 {  // rare case.
	    return MOVED;
	 }
	 if(put_pos<get_pos)
	 {
	    if(!get->CanSeek(put->GetRealPos()))
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
	 else // put_pos > get_pos
	 {
	    int skip=put->GetRealPos()-get->GetRealPos();
	    if(!put->CanSeek(get->GetRealPos()) || skip<skip_threshold)
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
	 debug((10,"copy: get hit eof\n"));
      eof:
	 if(line_buffer)
	 {
	    line_buffer->Get(&b,&s);
	    put->Put(b,s);
	    line_buffer->Skip(s);
	 }
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
      m=MOVED;

      rate_add=put_buf;

      if(get->range_limit!=FILE_END && get->range_limit<get->GetRealPos()+s)
	 s=get->range_limit-get->GetRealPos();

      if(line_buffer)
      {
	 const char *lb;
	 int ls;
	 if(line_buffer->Size()>line_buffer_max)
	 {
	    line_buffer->Get(&lb,&ls);
	    put->Put(lb,ls);
	    line_buffer->Skip(ls);
	 }
	 line_buffer->Put(b,s);
	 get->Skip(s);
	 bytes_count+=s;

	 // now find eol in line_buffer.
	 line_buffer->Get(&lb,&ls);
	 while(ls>0)
	 {
	    const char *eol=(const char *)memchr(lb,'\n',ls);
	    if(!eol)
	       break;
	    put->Put(lb,eol-lb+1);
	    line_buffer->Skip(eol-lb+1);
	    line_buffer->Get(&lb,&ls);
	 }
      }
      else
      {
	 put->Put(b,s);
	 get->Skip(s);
	 bytes_count+=s;
      }

      put_buf=put->Buffered();
      rate_add-=put_buf-s;
      RateAdd(rate_add);

      if(get->range_limit!=FILE_END && get->range_limit<=get->GetRealPos())
      {
	 debug((10,"copy: get reached range limit\n"));
	 goto eof;
      }
      return m;
   }
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
      debug((10,"copy: put confirmed store\n"));
   pre_GET_DONE_WAIT:
      get->Empty();
      get->PutEOF();
      get->Resume();
      state=GET_DONE_WAIT;
      m=MOVED;
      end_time=now;
      end_time_ms=now_ms;
      delete put; put=0;
      /* fallthrough */
   case(GET_DONE_WAIT):
      if(get->Error())
	 goto get_error;
      if(remove_source_later)
      {
	 get->RemoveFile();
	 remove_source_later=false;
      }
      if(!get->Done())
	 return m;
      debug((10,"copy: get is finished - all done\n"));
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
   start_time_ms=0;
   end_time=0;
   end_time_ms=0;
   fail_if_cannot_seek=false;
   remove_source_later=false;
   line_buffer=0;
   line_buffer_max=0;
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
   if(line_buffer) delete line_buffer;
}
FileCopy *FileCopy::New(FileCopyPeer *s,FileCopyPeer *d,bool c)
{
   FileCopy *res=0;
   if(fxp_create)
      res=fxp_create(s,d,c);
   if(res)
      return res;
   return new FileCopy(s,d,c);
}
void FileCopy::Suspend()
{
   if(get) get->Suspend();
   if(put) put->Suspend();
   super::Suspend();
}
void FileCopy::Resume()
{
   super::Resume();
   if(state!=PUT_WAIT)
   {
      if(get && !(put && put->Size()>=max_buf))
	 get->Resume();
   }
   if(state!=GET_INFO_WAIT)
   {
      if(put)
	 put->Resume();
   }
}

void FileCopy::Reconfig(const char *s)
{
   if(rate)
      rate->SetPeriod(ResMgr::Query("xfer:rate-period",0));
   if(rate_for_eta)
      rate_for_eta->SetPeriod(ResMgr::Query("xfer:eta-period",0));
}

void FileCopy::SetError(const char *str)
{
   xfree(error_text);
   error_text=xstrdup(str);
   if(get) { delete get; get=0; }
   if(put) { delete put; put=0; }
}

void FileCopy::LineBuffered(int s)
{
   if(!line_buffer)
      line_buffer=new Buffer();
   line_buffer_max=s;
}

long FileCopy::GetPos()
{
   if(put)
      return put->GetRealPos() - put->Buffered();
   if(get)
      return get->GetRealPos();
   return 0;
}

long FileCopy::GetSize()
{
   if(get)
      return get->GetSize();
   return NO_SIZE;
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
   return percent(put->GetRealPos() - put->Buffered(),size);
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
long FileCopy::GetBytesRemaining()
{
   if(!get)
      return 0;
   if(get->range_limit==FILE_END)
   {
      long size=get->GetSize();
      if(size<=0 || size<get->GetRealPos() || !rate_for_eta->Valid())
	 return -1;
      return(size-GetPos());
   }
   return get->range_limit-GetPos();
}
const char *FileCopy::GetETAStr()
{
   long b=GetBytesRemaining();
   if(b<0)
      return "";
   return rate_for_eta->GetETAStrSFromSize(b);
}
long FileCopy::GetETA(long b)
{
   if(b<0 || !rate_for_eta->Valid())
      return -1;
   return (long)(b / rate_for_eta->Get() + 0.5);
}
const char *FileCopy::GetStatus()
{
   static char buf[256];
   const char *get_st=0;
   if(get)
      get_st=get->GetStatus();
   const char *put_st=0;
   if(put)
      put_st=put->GetStatus();
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
int FileCopy::GetTimeSpentMilli()
{
   if(start_time==0 || end_time==0 || end_time<start_time
   || (end_time==start_time && end_time_ms<start_time_ms))
      return 0;
   return end_time_ms-start_time_ms;
}

FgData *FileCopy::GetFgData(bool fg)
{
   // NOTE: only one of get/put can have FgData in this implementation.
   FgData *f=0;
   if(get) f=get->GetFgData(fg);
   if(f) return f;
   if(put) f=put->GetFgData(fg);
   return f;
}

pid_t FileCopy::GetProcGroup()
{
   pid_t p=0;
   if(get) p=get->GetProcGroup();
   if(p) return p;
   if(put) p=put->GetProcGroup();
   return p;
}

void FileCopy::Kill(int sig)
{
   if(get) get->Kill(sig);
   if(put) put->Kill(sig);
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
      if(removing)
	 return false;
      if(mode==PUT)
      {
	 if(do_set_date && !date_set)
	    return false;
      }
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
   can_seek0=true;
   date_set=false;
   do_set_date=true;
   ascii=false;
   range_start=0;
   range_limit=FILE_END;
   removing=false;
   use_cache=true;
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

   if(removing)
   {
      res=session->Done();
      if(res<=0)
      {
	 removing=false;
	 session->Close();
	 m=MOVED;
      }
      else
	 return m;
   }

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
      if(fxp)
      {
	 if(eof)
	    goto fxp_eof;
	 return m;
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
	    fxp_eof:
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
      if(fxp)
	 return m;
      res=Get_LL(GET_BUFSIZE);
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
   if(fxp && mode==PUT)
      return;
   session->Suspend();
   super::Suspend();
}
void FileCopyPeerFA::Resume()
{
   super::Resume();
   session->Resume();
}

const char *FileCopyPeerFA::GetStatus()
{
   if(!session->IsOpen())
      return 0;
   return session->CurrentStatus();
}

void FileCopyPeerFA::Seek(long new_pos)
{
   if(pos==new_pos)
      return;
   super::Seek(new_pos);
   session->Close();
   if(seek_pos==FILE_END)
      WantSize();
   else
      pos=new_pos;
}

void FileCopyPeerFA::OpenSession()
{
   if(mode==GET)
   {
      if(size!=NO_SIZE && size!=NO_SIZE_YET && seek_pos>=size && !ascii)
      {
	 pos=seek_pos;
	 eof=true;
	 return;
      }
      char *b;
      int s;
      if(use_cache && LsCache::Find(session,file,FAmode,&b,&s))
      {
	 if(seek_pos>=s)
	 {
	    eof=true;
	    return;
	 }
	 size=s;
	 b+=seek_pos;
	 s-=seek_pos;
	 Save(0);
	 Allocate(s);
	 memmove(buffer+buffer_ptr,b,s);
      #ifndef NATIVE_CRLF
	 if(ascii)
	 {
	    char *buf=buffer+buffer_ptr;
	    // find where line ends.
	    char *cr=buf;
	    for(;;)
	    {
	       cr=(char *)memchr(cr,'\r',s-(cr-buf));
	       if(!cr)
		  break;
	       if(cr-buf<s-1 && cr[1]=='\n')
	       {
		  memmove(cr,cr+1,s-(cr-buf)-1);
		  s--;
		  if(s<=1)
		     break;
	       }
	       else if(cr-buf==s-1)
		  break;
	       cr++;
	    }
	 }
      #endif	 // NATIVE_CRLF
	 in_buffer=s;
	 pos=seek_pos;
	 eof=true;
	 return;
      }
   }
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
   if(want_date && date==NO_DATE_YET)
      session->WantDate(&date);
   if(mode==GET)
   {
      SaveRollback(seek_pos);
      pos=seek_pos;
   }
   else
   {
      pos=seek_pos+in_buffer;
   }
}

void FileCopyPeerFA::RemoveFile()
{
   session->Open(file,FA::REMOVE);
   removing=true;
}

int FileCopyPeerFA::Get_LL(int len)
{
   int res=0;

   if(session->IsClosed())
      OpenSession();

   if(eof)  // OpenSession can set eof=true.
      return 0;

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
   {
      eof=true;
      LsCache::Add(session,file,FAmode,this);
   }
   return res;
}

int FileCopyPeerFA::Put_LL(const char *buf,int len)
{
   if(session->IsClosed())
      OpenSession();

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
   if(session->OpenMode()!=FAmode || fxp)
      return pos;
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
      if(eof)
	 return pos;
      if(session->GetRealPos()==0 && session->GetPos()>0)
      {
	 can_seek=false;
	 session->SeekReal();
      }
      if(pos+in_buffer!=session->GetPos())
      {
	 SaveRollback(session->GetPos());
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
   fxp=false;
   if(FAmode==FA::LIST || FAmode==FA::LONG_LIST)
      Save(LsCache::SizeLimit());
}
FileCopyPeerFA::~FileCopyPeerFA()
{
   if(session)
   {
      session->Close();
      if(reuse_later)
	 SessionPool::Reuse(session);
   }
   xfree(file);
}

FileCopyPeerFA::FileCopyPeerFA(ParsedURL *u,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET)
{
   FAmode=m;
   file=xstrdup(u->path);
   session=FileAccess::New(u);
   reuse_later=true;
   fxp=false;
   if(!session)
   {
      const char *e=_(" - not supported protocol");
      char *m=string_alloca(strlen(e)+strlen(u->proto)+1);
      strcpy(m,u->proto);
      strcat(m,e);
      SetError(m);
   }
   else if(!file)
   {
      SetError(_("file name missed in URL"));
   }
}

FileCopyPeerFA *FileCopyPeerFA::New(FileAccess *s,const char *url,int m,bool reuse)
{
   ParsedURL u(url,true);
   if(u.proto)
   {
      if(reuse)
	 SessionPool::Reuse(s);
      return new FileCopyPeerFA(&u,m);
   }
   FileCopyPeerFA *peer=new FileCopyPeerFA(s,url,m);
   if(!reuse)
      peer->DontReuseSession();
   return peer;
}

// FileCopyPeerFDStream
#undef super
#define super FileCopyPeer
FileCopyPeerFDStream::FileCopyPeerFDStream(FDStream *o,direction m)
   : FileCopyPeer(m)
{
   if(o==0 && m==PUT)
      o=new FDStream(1,"<stdout>");
   stream=o;
   seek_base=0;
   delete_stream=true;
   create_fg_data=true;
   need_seek=false;
   can_seek = can_seek0 = stream->can_seek();
   if(can_seek && stream->fd!=-1)
   {
      seek_base=lseek(stream->fd,0,SEEK_CUR);
      if(seek_base==-1)
      {
	 can_seek=false;
	 can_seek0=false;
	 seek_base=0;
      }
   }
}
FileCopyPeerFDStream::~FileCopyPeerFDStream()
{
   if(stream && delete_stream)
      delete stream;
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
	    if(date_set || date==NO_DATE_YET)
	       return m;
	    if(date!=NO_DATE && do_set_date)
	    {
	       if(getfd()==-1)
		  return m;
	       stream->setmtime(date);
	    }
	    date_set=true;
	    return MOVED;
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
      res=Get_LL(GET_BUFSIZE);
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

bool FileCopyPeerFDStream::Done()
{
   if(!super::Done())
      return false;
   if(stream && delete_stream && !stream->Done())
      return false;
   return true;
}

void FileCopyPeerFDStream::Seek(long new_pos)
{
   if(pos==new_pos)
      return;
#ifndef NATIVE_CRLF
   if(ascii)
   {
      // it is possible to read file to determine right position,
      // but it is costly.
      can_seek=false;
      // can_seek0 is still true.
      return;
   }
#endif
   super::Seek(new_pos);
   if(getfd()==-1)
      return;
   if(new_pos==FILE_END)
   {
      new_pos=lseek(getfd(),0,SEEK_END);
      if(new_pos==-1)
      {
	 can_seek=false;
	 can_seek0=false;
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
	 can_seek0=false;
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

#ifndef NATIVE_CRLF
   if(ascii)
      Allocate(len*2);
   else
#endif
      Allocate(len);

   if(need_seek)  // this does not combine with ascii.
      lseek(fd,seek_base+pos,SEEK_SET);

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

#ifndef NATIVE_CRLF
   if(ascii)
   {
      char *p=buffer+buffer_ptr+in_buffer;
      for(int i=res; i>0; i--)
      {
	 if(*p=='\n')
	 {
	    memmove(p+1,p,i);
	    *p++='\r';
	    res++;
	 }
	 p++;
      }
   }
#endif

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

   int skip_cr=0;

#ifndef NATIVE_CRLF
   if(ascii)
   {
      // find where line ends.
      const char *cr=buf;
      for(;;)
      {
	 cr=(const char *)memchr(cr,'\r',len-(cr-buf));
	 if(!cr)
	    break;
	 if(cr-buf<len-1 && cr[1]=='\n')
	 {
	    skip_cr=1;
	    len=cr-buf;
	    break;
	 }
	 if(cr-buf==len-1)
	 {
	    if(eof)
	       break;
	    len--;
	    break;
	 }
	 cr++;
      }
   }
#endif	 // NATIVE_CRLF

   if(len==0)
      return skip_cr;

   if(need_seek)  // this does not combine with ascii.
      lseek(fd,seek_base+pos-in_buffer,SEEK_SET);

   int res=write(fd,buf,len);
   if(res<0)
   {
      if(errno==EAGAIN || errno==EINTR)
      {
	 Block(fd,POLLOUT);
	 return 0;
      }
      if(errno==EPIPE)
      {
	 broken=true;
	 return -1;
      }
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   if(res==len)
      res+=skip_cr;
   return res;
}
FgData *FileCopyPeerFDStream::GetFgData(bool fg)
{
   if(!delete_stream || !create_fg_data)
      return 0;	  // if we don't own the stream, don't create FgData.
   if(stream->GetProcGroup())
      return new FgData(stream->GetProcGroup(),fg);
   return 0;
}

void FileCopyPeerFDStream::RemoveFile()
{
   stream->remove();
   removing=false;   // it is instant.
}

FileCopyPeerFDStream *FileCopyPeerFDStream::NewPut(const char *file,bool cont=false)
{
   return new FileCopyPeerFDStream(new FileStream(file,O_WRONLY|O_CREAT
				    |(cont?0:O_TRUNC)),FileCopyPeer::PUT);
}
FileCopyPeerFDStream *FileCopyPeerFDStream::NewGet(const char *file)
{
   return new FileCopyPeerFDStream(new FileStream(file,O_RDONLY),
				    FileCopyPeer::GET);
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

// FileCopyPeerDirList
FileCopyPeerDirList::FileCopyPeerDirList(FA *s,ArgV *v)
   : FileCopyPeer(GET)
{
   session=s;
   dl=session->MakeDirList(v);
   if(dl==0)
      eof=true;
   can_seek=false;
   can_seek0=false;
}

FileCopyPeerDirList::~FileCopyPeerDirList()
{
   if(dl)
      delete dl;
   if(session)
      SessionPool::Reuse(session);
}

int FileCopyPeerDirList::Do()
{
   if(Done())
      return STALL;
   if(dl->Error())
   {
      SetError(dl->ErrorText());
      return MOVED;
   }

   const char *b;
   int s;
   dl->Get(&b,&s);
   if(b==0) // eof
   {
      eof=true;
      return MOVED;
   }
   if(s==0)
      return STALL;
   Allocate(s);
   memmove(buffer+buffer_ptr+in_buffer,b,s);
   in_buffer+=s;
   dl->Skip(s);
   return MOVED;
}


// Speedometer
#undef super
#define super SMTask
char Speedometer::buf_rate[40];
char Speedometer::buf_eta[40];
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
const char *Speedometer::GetStr(float r)
{
   buf_rate[0]=0;
   if(r<1)
      return "";
   if(r<1024)
      sprintf(buf_rate,_("%.0fb/s"),r);
   else if(r<1024*1024)
      sprintf(buf_rate,_("%.1fK/s"),r/1024.);
   else
      sprintf(buf_rate,_("%.2fM/s"),r/1024./1024.);
   return buf_rate;
}
const char *Speedometer::GetETAStrFromSize(long size)
{
   buf_eta[0]=0;

   if(!Valid() || Get()<1)
      return buf_eta;

   return GetETAStrFromTime(long(size/Get()+.5));
}
const char *Speedometer::GetETAStrFromTime(long eta)
{
   buf_eta[0]=0;

   if(eta<0)
      return buf_eta;

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
	 sprintf(buf_eta,"%s%ld%c%ld%c",tr_eta,ueta,letter,ueta2,letter2);
      else
	 sprintf(buf_eta,"%s%ld%c",tr_eta,ueta,letter);
   }
   else // verbose eta (by Ben Winslow)
   {
      long unit;
      strcpy(buf_eta, tr_eta);

      if(eta>=DAY)
      {
	 unit=eta/DAY;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, day_c);
      }
      if(eta>=HOUR)
      {
	 unit=(eta/HOUR)%24;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, hour_c);
      }
      if(eta>=MINUTE)
      {
	 unit=(eta/MINUTE)%60;
	 sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, minute_c);
      }
      unit=eta%60;
      sprintf(buf_eta+strlen(buf_eta), "%ld%c", unit, second_c);
   }
   return buf_eta;
}
const char *Speedometer::GetStrS(float r)
{
   GetStr(r);
   if(buf_rate[0])
      strcat(buf_rate," ");
   return buf_rate;
}
const char *Speedometer::GetETAStrSFromSize(long s)
{
   GetETAStrFromSize(s);
   if(buf_eta[0])
      strcat(buf_eta," ");
   return buf_eta;
}
const char *Speedometer::GetETAStrSFromTime(long s)
{
   GetETAStrFromTime(s);
   if(buf_eta[0])
      strcat(buf_eta," ");
   return buf_eta;
}
void Speedometer::Reset()
{
   start=now;
   last_second=now;
   rate=0;
   last_bytes=0;
}

FileCopy *(*FileCopy::fxp_create)(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
