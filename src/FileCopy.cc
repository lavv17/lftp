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
#include "plural.h"
#include "OutputJob.h"

#define skip_threshold 0x1000
#define debug(a) Log::global->Format a

ResDecl rate_period  ("xfer:rate-period","15", ResMgr::UNumberValidate,ResMgr::NoClosure);
ResDecl eta_period   ("xfer:eta-period", "120",ResMgr::UNumberValidate,ResMgr::NoClosure);
ResDecl max_redir    ("xfer:max-redirections", "0",ResMgr::UNumberValidate,ResMgr::NoClosure);

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
      if(remove_target_first && !put->FileRemoved())
	 return m;
      remove_target_first=false;
      if(put->NeedSizeDateBeforehand() || (cont && put->CanSeek()))
      {
	 if(get->GetSize()==NO_SIZE_YET || get->GetDate()==NO_DATE_YET)
	 {
	    put->Suspend();
	    get->DontStartTransferYet();
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
      if(put->GetSeekPos()!=FILE_END && get->GetSize()>=0
      && put->GetSeekPos()>=get->GetSize())
      {
	 debug((9,_("copy: destination file is already complete\n")));
	 goto pre_GET_DONE_WAIT;
      }
      if(!put->IOReady())
	 return m;
      /* now we know if put's seek failed. Seek get accordingly. */
      if(get->CanSeek())
	 get->Seek(put->GetRealPos());
   pre_DO_COPY:
      get->Resume();
      get->StartTransfer();
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
	 get->Suspend();
	 if(!put->Done())
	    return m;
	 debug((9,_("copy: put is broken\n")));
	 if(fail_if_broken)
	 {
	    SetError(strerror(EPIPE));
	    return MOVED;
	 }
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
	 SetError(_("seek failed"));
	 return MOVED;
      }
      if(get->GetSize()>0 && get->GetRealPos()>get->GetSize())
      {
	 get->SetSize(NO_SIZE_YET);
	 get->SetDate(NO_DATE_YET);
      }
      long lbsize=0;
      if(line_buffer)
	 lbsize=line_buffer->Size();
      /* check if positions are correct */
      off_t get_pos=get->GetRealPos()-get->range_start;
      off_t put_pos=put->GetRealPos()-put->range_start;
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
	       SetError(_("cannot seek on data source"));
	       return MOVED;
	    }
	    debug((9,_("copy: put rolled back to %lld, seeking get accordingly\n"),
		     (long long)put->GetRealPos()));
	    debug((10,"copy: get position was %lld\n",
		     (long long)get->GetRealPos()));
	    get->Seek(put->GetRealPos());
	    return MOVED;
	 }
	 else // put_pos > get_pos
	 {
	    off_t size=get->GetSize();
	    if(size>=0 && put->GetRealPos()>=size)
	    {
	       // simulate eof, as we have already have the whole file.
	       debug((9,_("copy: all data received, but get rolled back\n")));
	       goto eof;
	    }
	    off_t skip=put->GetRealPos()-get->GetRealPos();
	    if(!put->CanSeek(get->GetRealPos()) || skip<skip_threshold)
	    {
	       // we have to skip some data
	       get->Get(&b,&s);
	       if(skip>s)
		  skip=s;
	       if(skip==0)
		  return m;
	       get->Skip(skip);
	       bytes_count+=skip;
	       return MOVED;
	    }
	    debug((9,_("copy: get rolled back to %lld, seeking put accordingly\n"),
		     (long long)get->GetRealPos()));
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
	 put->Resume();
	 if(line_buffer)
	 {
	    line_buffer->Get(&b,&s);
	    put->Put(b,s);
	    line_buffer->Skip(s);
	 }
	 put->SetDate(get->GetDate());
	 put->PutEOF();
	 get->Suspend();
	 put_eof_pos=put->GetRealPos();
	 state=CONFIRM_WAIT;
	 return MOVED;
      }

      rate_add=put_buf;

      if(s==0)
      {
	 put_buf=put->Buffered();
	 rate_add-=put_buf;
	 RateAdd(rate_add);

	 if(put->Size()==0)
	    put->Suspend();
	 return m;
      }
      m=MOVED;

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
      /* check if put position is correct */
      if(put_eof_pos!=put->GetRealPos() || put->GetSeekPos()==FILE_END)
      {
	 state=DO_COPY;
	 return MOVED;
      }

      rate_add=put_buf;
      put_buf=put->Buffered();
      rate_add-=put_buf;
      RateAdd(rate_add);

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
      Delete(put); put=0;
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
      Delete(get); get=0;
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
   rate        =new Speedometer("xfer:rate-period");
   rate_for_eta=new Speedometer("xfer:eta-period");
   put_buf=0;
   put_eof_pos=0;
   bytes_count=0;
   fail_if_cannot_seek=false;
   fail_if_broken=true;
   remove_source_later=false;
   remove_target_first=false;
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
   Delete(get);
   Delete(put);
   Delete(line_buffer);
   xfree(error_text);
   Delete(rate);
   Delete(rate_for_eta);
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
void FileCopy::Fg()
{
   if(get) get->Fg();
   if(put) put->Fg();
}
void FileCopy::Bg()
{
   if(get) get->Bg();
   if(put) put->Bg();
}

void FileCopy::Reconfig(const char *s)
{
}

void FileCopy::SetError(const char *str)
{
   xfree(error_text);
   error_text=xstrdup(str);
   Delete(get); get=0;
   Delete(put); put=0;
}

void FileCopy::LineBuffered(int s)
{
   if(!line_buffer)
      line_buffer=new Buffer();
   line_buffer_max=s;
}

off_t FileCopy::GetPos()
{
   if(put)
      return put->GetRealPos() - put->Buffered();
   if(get)
      return get->GetRealPos();
   return 0;
}

off_t FileCopy::GetSize()
{
   if(get)
      return get->GetSize();
   return NO_SIZE;
}

int FileCopy::GetPercentDone()
{
   if(!get || !put)
      return 100;
   off_t size=get->GetSize();
   if(size==NO_SIZE || size==NO_SIZE_YET)
      return -1;
   if(size==0)
      return 0;
   off_t ppos=put->GetRealPos() - put->Buffered() - put->range_start;
   if(ppos<0)
      return 0;
   off_t psize=size-put->range_start;
   if(put->range_limit!=FILE_END)
      psize=put->range_limit-put->range_start;
   if(psize<0)
      return 100;
   if(ppos>psize)
      return -1;
   return percent(ppos,psize);
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
   if(!rate->Valid() || !put)
      return 0;
   return rate->Get();
}
const char *FileCopy::GetRateStr()
{
   if(!rate->Valid() || !put)
      return "";
   return rate->GetStrS();
}
off_t FileCopy::GetBytesRemaining()
{
   if(!get)
      return 0;
   if(get->range_limit==FILE_END)
   {
      off_t size=get->GetSize();
      if(size<=0 || size<get->GetRealPos() || !rate_for_eta->Valid())
	 return -1;
      return(size-GetPos());
   }
   return get->range_limit-GetPos();
}
const char *FileCopy::GetETAStr()
{
   off_t b=GetBytesRemaining();
   if(b<0 || !put)
      return "";
   return rate_for_eta->GetETAStrSFromSize(b);
}
long FileCopy::GetETA(off_t b)
{
   if(b<0 || !rate_for_eta->Valid())
      return -1;
   return (long)(double(b) / rate_for_eta->Get() + 0.5);
}
const char *FileCopy::GetStatus()
{
   static char *buf;
   xfree(buf); buf=0;
   const char *get_st=0;
   if(get)
      get_st=get->GetStatus();
   const char *put_st=0;
   if(put)
      put_st=put->GetStatus();
   if(get_st && get_st[0] && put_st && put_st[0])
      buf=xasprintf("[%s->%s]",get_st,put_st);
   else if(get_st && get_st[0])
      buf=xasprintf("[%s]",get_st);
   else if(put_st && put_st[0])
      buf=xasprintf("[%s]",put_st);
   else
      return "";
   return buf;
}

double FileCopy::GetTimeSpent()
{
   if(end_time<start_time)
      return 0;
   return TimeDiff(end_time,start_time);
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
void FileCopyPeer::SetSize(off_t s)
{
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
   date=d;
   if(date==NO_DATE || date==NO_DATE_YET)
      date_set=true;
   else
      date_set=false;
}

bool FileCopyPeer::Done()
{
   if(Error())
      return true;
   if(eof && in_buffer==0)
   {
      if(removing)
	 return false;
      if(mode==PUT)
	 return done;
      return true;
   }
   if(broken)
      return true;
   return false;
}

FileCopyPeer::FileCopyPeer(direction m)
{
   mode=m;
   want_size=false;
   want_date=false;
   start_transfer=true;
   size=NO_SIZE_YET;
   e_size=NO_SIZE;
   date=NO_DATE_YET;
   seek_pos=0;
   can_seek=false;
   can_seek0=false;
   date_set=false;
   do_set_date=true;
   ascii=false;
   range_start=0;
   range_limit=FILE_END;
   removing=false;
   file_removed=false;
   use_cache=true;
   write_allowed=true;
   done=false;
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
	 file_removed=true;
	 session->Close();
	 Suspend();
	 return MOVED;
      }
      else
	 return m;
   }

   if(Done() || Error())
      return m;
   if(want_size && size==NO_SIZE_YET && (mode==PUT || !start_transfer))
   {
      if(session->IsClosed())
      {
	 info.file=file;
	 info.get_size=true;
	 info.get_time=want_date;
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
      SetDate(info.time);
      session->Close();
      return MOVED;
   }
   switch(mode)
   {
   case PUT:
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
	       done=true;
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
		  try_time=session->GetTryTime();
		  retries=session->GetRetries();
		  Log::global->Format(10,"try_time=%ld, retries=%d\n",try_time,retries);
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

void FileCopyPeerFA::Seek(off_t new_pos)
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
   current->Timeout(0);	// mark it MOVED.
   if(mode==GET)
   {
      if(size!=NO_SIZE && size!=NO_SIZE_YET && seek_pos>=size && !ascii)
      {
      past_eof:
	 debug((10,"copy src: seek past eof (seek_pos=%lld, size=%lld)\n",
		  (long long)seek_pos,(long long)size));
	 pos=seek_pos;
	 eof=true;
	 return;
      }
      const char *b;
      int s;
      if(use_cache && LsCache::Find(session,file,FAmode,&b,&s))
      {
	 size=s;
	 if(seek_pos>=s)
	    goto past_eof;
	 b+=seek_pos;
	 s-=seek_pos;
	 Save(0);
	 Allocate(s);
	 memmove(buffer+buffer_ptr,b,s);
	 in_buffer=s;
	 pos=seek_pos;
	 eof=true;
	 return;
      }
   }
   else // mode==PUT
   {
      if(e_size!=NO_SIZE && seek_pos>=e_size)
      {
	 debug((10,"copy dst: seek past eof (seek_pos=%lld, size=%lld)\n",
		  (long long)seek_pos,(long long)e_size));
	 eof=true;
	 return;
      }
   }
   session->Open(file,FAmode,seek_pos);
   session->SetFileURL(orig_url);
   if(mode==PUT)
   {
      if(try_time!=0)
	 session->SetTryTime(try_time);
      if(retries!=0)
	 session->SetRetries(retries);
      if(e_size!=NO_SIZE && e_size!=NO_SIZE_YET)
	 session->SetSize(e_size);
      if(date!=NO_DATE && date!=NO_DATE_YET)
	 session->SetDate(date);
   }
   session->RereadManual();
   if(ascii)
      session->AsciiTransfer();
   if(want_size && size==NO_SIZE_YET)
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

   off_t io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   Allocate(len);

   res=session->Read(buffer+buffer_ptr+in_buffer,len);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      if(res==FA::FILE_MOVED)
      {
	 // handle redirection.
	 assert(!fxp);
	 const char *loc_c=session->GetNewLocation();
	 int max_redirections=max_redir.Query(0);
	 if(loc_c && loc_c[0] && max_redirections>0)
	 {
	    Log::global->Format(3,_("copy: received redirection to `%s'\n"),loc_c);
	    if(++redirections>max_redirections)
	    {
	       SetError(_("Too many redirections"));
	       return -1;
	    }
	    if(FAmode==FA::QUOTE_CMD)
	       FAmode=FA::RETRIEVE;

	    char *loc=alloca_strdup(loc_c);
	    session->Close(); // loc_c is no longer valid.

	    ParsedURL u(loc,true);

	    if(u.proto)
	    {
	       if(reuse_later)
		  SessionPool::Reuse(session);

	       session=FileAccess::New(&u);
	       reuse_later=true;

	       xfree(file);
	       file=xstrdup(u.path?u.path:"");
	       xfree(orig_url);
	       orig_url=xstrdup(loc);
	    }
	    else // !proto
	    {
	       if(orig_url)
	       {
		  int p_ind=url::path_index(orig_url);
		  char *s=strrchr(orig_url,'/');
		  int s_ind=s?s-orig_url:-1;
		  if(p_ind==-1 || s_ind==-1 || s_ind<p_ind)
		     s_ind=p_ind=strlen(orig_url);
		  if(loc[0]=='/')
		  {
		     orig_url=(char*)xrealloc(orig_url,p_ind+strlen(loc)+1);
		     strcpy(orig_url+p_ind,loc);
		  }
		  else
		  {
		     orig_url=(char*)xrealloc(orig_url,s_ind+1+strlen(loc)+1);
		     strcpy(orig_url+s_ind,"/");
		     strcpy(orig_url+s_ind+1,loc);
		  }
	       }

	       url::decode_string(loc);
	       char *slash=strrchr(file,'/');
	       char *new_file;
	       if(loc[0]!='/' && slash)
	       {
		  *slash=0;
		  new_file=xstrdup(dir_file(file,loc));
	       }
	       else
	       {
		  new_file=xstrdup(loc);
	       }
	       xfree(file);
	       file=new_file;
	    }

	    size=NO_SIZE_YET;
	    date=NO_DATE_YET;

	    try_time=0;
	    retries=0;
	    current->Timeout(0); // retry with new location.
	    return 0;
	 }
      }
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

   off_t io_at=pos; // GetRealPos can alter pos, save it.
   if(GetRealPos()!=io_at)
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
	 try_time=session->GetTryTime();
	 retries=session->GetRetries();
	 Log::global->Format(10,"try_time=%ld, retries=%d\n",try_time,retries);
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

off_t FileCopyPeerFA::GetRealPos()
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

void FileCopyPeerFA::Init()
{
   FAmode=FA::RETRIEVE;
   file=0;
   session=0;
   reuse_later=false;
   orig_url=0;
   fxp=false;
   try_time=0;
   retries=0;
   redirections=0;
   can_seek=true;
   can_seek0=true;
}

FileCopyPeerFA::FileCopyPeerFA(FileAccess *s,const char *f,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET)
{
   Init();
   FAmode=m;
   file=xstrdup(f);
   session=s;
   reuse_later=true;
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
   xfree(orig_url);
}

FileCopyPeerFA::FileCopyPeerFA(ParsedURL *u,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET)
{
   Init();
   FAmode=m;
   file=xstrdup(u->path);
   session=FileAccess::New(u);
   reuse_later=true;
   orig_url=u->orig_url;
   u->orig_url=0;
   if(!file)
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
   if(stream->usesfd(1))
      write_allowed=false;
   put_ll_timer=0;
   if(m==PUT)
      put_ll_timer=new Timer(TimeDiff(0,100));
}
FileCopyPeerFDStream::~FileCopyPeerFDStream()
{
   if(delete_stream)
      delete stream;
   delete put_ll_timer;
}

void FileCopyPeerFDStream::Seek_LL()
{
   int fd=stream->fd;
   assert(fd!=-1);
   if(CanSeek(seek_pos))
   {
      if(seek_pos==FILE_END)
      {
	 seek_pos=lseek(fd,0,SEEK_END);
	 if(seek_pos==-1)
	 {
	    can_seek=false;
	    can_seek0=false;
	    seek_pos=0;
	 }
	 else
	 {
	    SetSize(seek_pos);
	    if(seek_pos>seek_base)
	       seek_pos-=seek_base;
	    else
	       seek_pos=0;
	 }
	 pos=seek_pos;
      }
      else
      {
	 if(lseek(fd,seek_pos+seek_base,SEEK_SET)==-1)
	 {
	    can_seek=false;
	    can_seek0=false;
	    seek_pos=0;
	 }
	 pos=seek_pos;
      }
      if(mode==PUT)
	 pos+=in_buffer;
   }
   else
   {
      seek_pos=pos;
   }
}

int FileCopyPeerFDStream::getfd()
{
   if(stream->fd!=-1)
      return stream->fd;
   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
      {
	 SetError(stream->error_text);
	 current->Timeout(0);
      }
      else
      {
	 current->TimeoutS(1);
      }
      return -1;
   }
   stream->clear_status();
   pos=0;
   if(mode==PUT)
      pos+=in_buffer;
   Seek_LL();
   return fd;
}
int FileCopyPeerFDStream::Do()
{
   int m=STALL;
   if(Done() || Error())
      return m;
   switch(mode)
   {
   case PUT:
      if(in_buffer==0)
      {
	 if(eof)
	 {
	    if(!date_set && date!=NO_DATE && do_set_date)
	    {
	       if(date==NO_DATE_YET)
		  return m;
	       if(getfd()==-1)
		  return m;
	       stream->setmtime(date);
	       date_set=true;
	       m=MOVED;
	    }
	    if(stream && delete_stream && !stream->Done())
	       return m;
	    done=true;
	    return MOVED;
	 }
	 if(seek_pos==0)
	    return m;
      }
      if(!write_allowed)
	 return m;
      while(in_buffer>0)
      {
	 if(!eof && in_buffer<PUT_LL_MIN
	 && put_ll_timer && !put_ll_timer->Stopped())
	    break;
	 int res=Put_LL(buffer+buffer_ptr,in_buffer);
	 if(res>0)
	 {
	    in_buffer-=res;
	    buffer_ptr+=res;
	    m=MOVED;
	 }
	 if(res<0)
	    return MOVED;
	 if(res==0)
	    break;
      }
      break;

   case GET:
      if(eof)
	 return m;
      while(in_buffer<GET_BUFSIZE)
      {
	 int res=Get_LL(GET_BUFSIZE);
	 if(res>0)
	 {
	    in_buffer+=res;
	    SaveMaxCheck(0);
	    m=MOVED;
	 }
	 if(res<0)
	    return MOVED;
	 if(eof)
	    return MOVED;
	 if(res==0)
	    break;
      }
      break;
   }
   return m;
}

bool FileCopyPeerFDStream::IOReady()
{
   return seek_pos==pos || stream->fd!=-1;
}

void FileCopyPeerFDStream::Seek(off_t new_pos)
{
   if(pos==new_pos)
      return;
#ifndef NATIVE_CRLF
   if(ascii && new_pos!=0)
   {
      // it is possible to read file to determine right position,
      // but it is costly.
      can_seek=false;
      // can_seek0 is still true.
      return;
   }
#endif
   super::Seek(new_pos);
   int fd=stream->fd;
   if(fd==-1)
   {
      if(seek_pos!=FILE_END)
      {
	 pos=seek_pos+(mode==PUT)*in_buffer;
	 return;
      }
      else
      {
	 off_t s=stream->get_size();
	 if(s!=-1)
	 {
	    SetSize(s);
	    pos=seek_pos+(mode==PUT)*in_buffer;
	    return;
	 }
	 else
	 {
	    // ok, have to try getfd.
	    fd=getfd();
	 }
      }
      if(fd==-1)
	 return;
   }
   Seek_LL();
}

int FileCopyPeerFDStream::Get_LL(int len)
{
   int res=0;

   int fd=getfd();
   if(fd==-1)
      return 0;

   if((want_date && date==NO_DATE_YET)
   || (want_size && size==NO_SIZE_YET))
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
      if(E_RETRY(errno))
      {
	 Block(fd,POLLIN);
	 return 0;
      }
      if(stream->NonFatalError(errno))
	 return 0;
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   stream->clear_status();

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
      if(E_RETRY(errno))
      {
	 Block(fd,POLLOUT);
	 return 0;
      }
      if(errno==EPIPE)
      {
	 broken=true;
	 in_buffer=0;
	 eof=true;
	 return -1;
      }
      if(stream->NonFatalError(errno))
	 return 0;
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   stream->clear_status();
   if(res==len)
      res+=skip_cr;
   if(put_ll_timer)
      put_ll_timer->Reset();
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
   file_removed=true;
   Suspend();
   current->Timeout(0);
}

const char *FileCopyPeerFDStream::GetStatus()
{
   return stream->status;
}
void FileCopyPeerFDStream::Kill(int sig)
{
   stream->Kill(sig);
}


FileCopyPeerFDStream *FileCopyPeerFDStream::NewPut(const char *file,bool cont)
{
   return new FileCopyPeerFDStream(new FileStream(file,O_WRONLY|O_CREAT
				    |(cont?0:O_TRUNC)),FileCopyPeer::PUT);
}
FileCopyPeerFDStream *FileCopyPeerFDStream::NewGet(const char *file)
{
   return new FileCopyPeerFDStream(new FileStream(file,O_RDONLY),
				    FileCopyPeer::GET);
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
   Delete(dl);
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

FileCopyPeerOutputJob::FileCopyPeerOutputJob(OutputJob *new_o)
   : FileCopyPeer(PUT)
{
   o=new_o;
   DontCopyDate();
}

int FileCopyPeerOutputJob::Put_LL(const char *buf,int len)
{
   off_t io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   if(len==0 && eof)
      return 0;

   if(o->Full())
      return 0;

   o->Put(buf,len);

   seek_pos+=len; // mainly to indicate that there was some output.
   return len;
}

int FileCopyPeerOutputJob::Do()
{
   if(o->Error())
   {
      broken=true;
      return MOVED;
   }

   if(eof && !in_buffer)
   {
      done=true;
      return MOVED;
   }

   int m=STALL;

   if(!write_allowed)
      return m;

   while(in_buffer>0)
   {
      int res=Put_LL(buffer+buffer_ptr,in_buffer);
      if(res>0)
      {
	 in_buffer-=res;
	 buffer_ptr+=res;
	 m=MOVED;
      }
      if(res<0)
	 return MOVED;
      if(res==0)
	 break;
   }
   return m;
}

void FileCopyPeerOutputJob::Fg()
{
   o->Fg();
   FileCopyPeer::Fg();
}
void FileCopyPeerOutputJob::Bg()
{
   o->Bg();
   FileCopyPeer::Bg();
}

// special pointer to creator of ftp/ftp copier. It is init'ed in Ftp class.
FileCopy *(*FileCopy::fxp_create)(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
