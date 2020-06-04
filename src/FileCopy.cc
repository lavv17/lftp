/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2020 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <cmath>
#include <stddef.h>
#include "FileCopy.h"
#include "url.h"
#include "log.h"
#include "misc.h"
#include "LsCache.h"
#include "plural.h"
#include "ArgV.h"

#define skip_threshold 0x1000

ResDecl rate_period  ("xfer:rate-period","15", ResMgr::UNumberValidate,ResMgr::NoClosure);
ResDecl eta_period   ("xfer:eta-period", "120",ResMgr::UNumberValidate,ResMgr::NoClosure);
ResDecl max_redir    ("xfer:max-redirections", "5",ResMgr::UNumberValidate,ResMgr::NoClosure);
ResDecl buffer_size  ("xfer:buffer-size","0x10000",ResMgr::UNumberValidate,ResMgr::NoClosure);

// It's bad when lftp receives data in small chunks, try to accumulate
// data in a kernel buffer using a delay and slurp it at once:
enum {
   // Delays in microseconds
   MAX_DELAY=30000,
   DELAY_STEP=30,
   // This size is related to socket buffer size.
   // When it is too large, tcp slowdown happens.
   // SSL has packet size 0x4000, so we have to use a lower threshold.
   MAX_READ_TO_DELAY=0x3F00,
};

// FileCopy
#define super SMTask

#define set_state(s) do { state=(s); \
   Log::global->Format(11,"FileCopy(%p) enters state %s\n", this, #s); } while(0)

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
      set_state(INITIAL);
      m=MOVED;
   case(INITIAL):
      if(remove_target_first && !put->FileRemoved())
	 return m;
      remove_target_first=false;
      if(cont && put->CanSeek())
	 put->WantSize();
      if(put->NeedSizeDateBeforehand() || (cont && put->CanSeek() && put->GetSize()==NO_SIZE_YET))
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
      if(get->GetSize()==NO_SIZE_YET)
	 get->WantSize();
      if(get->GetSize()!=NO_SIZE && get->GetSize()!=NO_SIZE_YET)
	 put->SetEntitySize(get->GetSize());
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
      set_state(PUT_WAIT);
      m=MOVED;
      /* fallthrough */
   case(PUT_WAIT):
      if(put->Error())
	 goto put_error;
      if(put->GetSeekPos()!=FILE_END && get->GetSize()>=0
      && put->GetSeekPos()>=get->GetSize())
      {
	 debug((9,_("copy: destination file is already complete\n")));
	 if(get->GetDate()!=NO_DATE)
	    goto pre_CONFIRM_WAIT;  // have to set the date.
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
      set_state(DO_COPY);
      m=MOVED;
      /* fallthrough */
   case(DO_COPY): {
      if(put->Error())
      {
      put_error:
	 SetError(put->ErrorText());
	 return MOVED;
      }
      if(get->Error() && get->Size()==0)
      {
	 put->DontVerify();
	 if(put->GetPos()>0)
	 {
	    put->PutEOF();
	    put->Roll();
	 }
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

      if(high_watermark_timeout.Stopped())
      {
	 SetError(_("no progress timeout"));
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
      if(put->IsFull())
	 get->Suspend(); // stall the get.
      get->Get(&b,&s);
      if(b==0) // eof
      {
	 debug((10,"copy: get hit eof\n"));
	 goto eof;
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
      {
	 s=get->range_limit-get->GetRealPos();
	 if(s<0)
	    s=0;
      }

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
	 const char *eol=0;
	 if(get->Eof() || get->Error())
	    eol=lb+ls-1;
	 else
	    eol=memrchr(lb,'\n',ls);
	 if(eol)
	 {
	    put->Put(lb,eol-lb+1);
	    line_buffer->Skip(eol-lb+1);
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

      if(high_watermark<put_pos+s)
      {
	 high_watermark=put_pos+s;
	 high_watermark_timeout.Reset();
      }

      if(get->range_limit!=FILE_END && get->range_limit<=get->GetRealPos())
      {
	 debug((10,"copy: get reached range limit\n"));
	 goto eof;
      }
      return m;
   }

   eof:
      if(line_buffer)
      {
	 line_buffer->Get(&b,&s);
	 put->Put(b,s);
	 line_buffer->Skip(s);
      }
      if(!CheckFileSizeAtEOF())
      {
	 SetError(_("file size decreased during transfer"));
	 return MOVED;
      }
   pre_CONFIRM_WAIT:
      if(put->IsAutoRename())
	 put->SetSuggestedFileName(get->GetSuggestedFileName());
      put->SetDate(get->GetDate());
      if(get->GetSize()!=NO_SIZE && get->GetSize()!=NO_SIZE_YET)
	 put->SetEntitySize(get->GetSize());
      put->PutEOF();
      get->Suspend();
      put->Resume();
      put_eof_pos=put->GetRealPos();
      debug((10,"copy: waiting for put confirmation\n"));
      set_state(CONFIRM_WAIT);
      m=MOVED;
   case(CONFIRM_WAIT):
      if(put->Error())
	 goto put_error;
      /* check if put position is correct */
      if(put_eof_pos!=put->GetRealPos() || put->GetSeekPos()==FILE_END)
      {
	 set_state(DO_COPY);
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
      set_state(GET_DONE_WAIT);
      m=MOVED;
      end_time=now;
      put->Suspend();
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
      set_state(ALL_DONE);
      get->Suspend();
      LogTransfer();
      return MOVED;

   pre_GET_INFO_WAIT:
      set_state(GET_INFO_WAIT);
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

FileCopy::FileCopy(FileCopyPeer *s,FileCopyPeer *d,bool c)
   : get(s), put(d), cont(c),
   rate("xfer:rate-period"),
   rate_for_eta("xfer:eta-period"),
   high_watermark_timeout("xfer:timeout",0)
{
   set_state(INITIAL);
   int max_buf=buffer_size.Query(0);
   if(max_buf<1)
      max_buf=1;
   s->SetMaxBuffered(max_buf);
   d->SetMaxBuffered(max_buf);
   put_buf=0;
   put_eof_pos=0;
   high_watermark=0;
   bytes_count=0;
   fail_if_cannot_seek=false;
   fail_if_broken=true;
   remove_source_later=false;
   remove_target_first=false;
   line_buffer_max=0;
}
FileCopy::~FileCopy()
{
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
void FileCopy::SuspendInternal()
{
   super::SuspendInternal();
   if(get) get->SuspendSlave();
   if(put) put->SuspendSlave();
}
void FileCopy::ResumeInternal()
{
   if(get) get->ResumeSlave();
   if(put) put->ResumeSlave();
   super::ResumeInternal();
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

void FileCopy::SetError(const char *str)
{
   error_text.set(str);
   get=0;
}

void FileCopy::LineBuffered(int s)
{
   if(!line_buffer)
      line_buffer=new Buffer();
   line_buffer_max=s;
}

off_t FileCopy::GetPos() const
{
   if(put) {
      off_t pos = put->GetRealPos() - put->Buffered();
      // sometimes Buffered overestimates the amount of buffered data
      if(pos<0)
	 pos=0;
      return pos;
   }
   if(get)
      return get->GetRealPos();
   return 0;
}

off_t FileCopy::GetSize() const
{
   if(get)
      return get->GetSize();
   return NO_SIZE;
}

int FileCopy::GetPercentDone() const
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
const char *FileCopy::GetPercentDoneStr() const
{
   int pct=GetPercentDone();
   if(pct==-1)
      return "";
   static char buf[8];
   snprintf(buf,8,"(%d%%) ",pct);
   return buf;
}
void FileCopy::RateAdd(int a)
{
   rate.Add(a);
   rate_for_eta.Add(a);
}
void FileCopy::RateReset()
{
   start_time=now;
   rate.Reset();
   rate_for_eta.Reset();
}
float FileCopy::GetRate()
{
   if(!rate.Valid() || !put)
      return 0;
   return rate.Get();
}
const char *FileCopy::GetRateStr()
{
   if(!rate.Valid() || !put)
      return "";
   return rate.GetStrS();
}
off_t FileCopy::GetBytesRemaining()
{
   if(!get)
      return 0;
   if(get->range_limit==FILE_END)
   {
      off_t size=get->GetSize();
      if(size<=0 || size<get->GetRealPos() || !rate_for_eta.Valid())
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
   return rate_for_eta.GetETAStrSFromSize(b);
}
long FileCopy::GetETA(off_t b)
{
   if(b<0 || !rate_for_eta.Valid())
      return -1;
   return (long)(double(b) / rate_for_eta.Get() + 0.5);
}
const char *FileCopy::GetStatus()
{
   static xstring buf;
   const char *get_st=get?get->GetStatus():0;
   const char *put_st=put?put->GetStatus():0;
   if(get_st && get_st[0] && put_st && put_st[0])
      buf.vset("[",get_st,"->",put_st,"]",NULL);
   else if(get_st && get_st[0])
      buf.vset("[",get_st,"]",NULL);
   else if(put_st && put_st[0])
      buf.vset("[",put_st,"]",NULL);
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

Ref<Log> FileCopy::transfer_log;

void FileCopy::LogTransfer()
{
   const char *log_ctx="xfer";
   if(!ResMgr::QueryBool("log:enabled",log_ctx))
      return;
   const char *src=get->GetURL();
   if(!src)
      return;
   src=alloca_strdup(src);
   const char *dst=put->GetURL();
   if(!dst)
      return;
   dst=alloca_strdup(dst);
   if(!transfer_log)
      transfer_log=new Log(log_ctx);
   long long range_limit=GetRangeLimit();
   if(range_limit==FILE_END)
      range_limit=get->GetPos();
   transfer_log->Format(0,"%s -> %s %lld-%lld %s\n",
      url::remove_password(src),url::remove_password(dst),
      (long long)GetRangeStart(),range_limit,
      Speedometer::GetStrProper(GetBytesCount()/GetTimeSpent()).get());
}

void FileCopy::SetRange(off_t s,off_t lim)
{
   get->SetRange(s,lim);
   put->SetRange(s,lim);
}

bool FileCopy::CheckFileSizeAtEOF() const
{
   long long range_limit=GetRangeLimit();
   if(range_limit==FILE_END)
   {
      const long long size=GetSize();
      if(size==NO_SIZE || size==NO_SIZE_YET)
	 return true;   // nothing to compare with.
      range_limit=size;
   }

   const long long get_pos=get->GetRealPos();
   const long long put_pos=put->GetRealPos();
   const long long pos=(get_pos>put_pos ? get_pos : put_pos);
   if(pos<=0 || pos>=range_limit)
      return true;

   debug((0,"expected pos=%lld, actual pos=%lld\n",range_limit,pos));
   return false;
}

// FileCopyPeer implementation
#undef super
#define super Buffer
off_t FileCopyPeer::GetSize()
{
   if(size>=0 && pos>size)
      WantSize();
   return size;
}
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
void FileCopyPeer::SetDate(time_t d,int p)
{
   date.set(d,p);
   if(d==NO_DATE || d==NO_DATE_YET)
      date_set=true;
   else
      date_set=false;
}

void FileCopyPeer::SetRange(const off_t s,const off_t lim)
{
   range_start=s;
   range_limit=lim;
   if(mode==PUT || range_start>GetPos()+0x4000)
      Seek(range_start);
}

bool FileCopyPeer::Done()
{
   if(Error())
      return true;
   if(eof && Size()==0)
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

void FileCopyPeer::Seek(off_t offs)
{
   seek_pos=offs;
   if(mode==PUT)
      pos-=Size();
   Empty();
   eof=false;
   broken=false;
}

const char *FileCopy::TempFileName(const char *file)
{
   if(!ResMgr::QueryBool("xfer:use-temp-file",0))
      return file;

   xstring &temp=xstring::get_tmp(ResMgr::Query("xfer:temp-file-name",0));
   if(temp.length()==0 || temp.eq("*"))
      return file;

   const char *name=basename_ptr(file);
   int subst_pos=temp.instr('*');
   if(subst_pos>=0)
      temp.set_substr(subst_pos,1,name);
   else {
      if(temp.last_char()=='.')
	 temp.append(name);
      else if(temp[0]=='.')
	 temp.set_substr(0,0,name);
      else
	 temp.append('.').append(name);
   }
   return dir_file(dirname(file),temp);
}

const char *FileCopyPeer::UseTempFile(const char *file)
{
   const char *temp=FileCopy::TempFileName(file);
   if(temp==file)
      return file;

   auto_rename=true;
   temp_file=true;
   SetSuggestedFileName(basename_ptr(file));

   return temp;
}

bool FileCopyPeer::ShouldRename() const
{
   return (auto_rename || temp_file) && suggested_filename;
}

FileCopyPeer::FileCopyPeer(dir_t m) : IOBuffer(m)
{
   want_size=false;
   want_date=false;
   start_transfer=true;
   size=NO_SIZE_YET;
   date=NO_DATE_YET;
   e_size=NO_SIZE;
   seek_pos=0;
   can_seek=false;
   can_seek0=false;
   date_set=false;
   do_set_date=true;
   do_verify=true;
   ascii=false;
   range_start=0;
   range_limit=FILE_END;
   removing=false;
   file_removed=false;
   temp_file=false;
   do_mkdir=false;
   use_cache=true;
   write_allowed=true;
   done=false;
   auto_rename=false;
   Suspend();  // don't do anything too early
}

// FileCopyPeerFA implementation
#undef super
#define super FileCopyPeer
int FileCopyPeerFA::Do()
{
   int m=STALL;
   int res;

   if(session->OpenMode()==FA::MAKE_DIR)
   {
      // doing mkdir
      int res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      if(res<0)
	 debug((3,"mkdir failed: %s\n",session->StrError(res)));
      session->Close();
      m=MOVED;
   }
   else if(session->OpenMode()==FA::RENAME)
   {
      int res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      if(res<0) {
	 if(temp_file)
	    SetError(session->StrError(res));
	 else
	    debug((3,"rename failed: %s\n",session->StrError(res)));
      }
      session->Close();
      done=true;
      return MOVED;
   }

   if(do_mkdir) {
      // do mkdir just once
      do_mkdir=false;
      assert(!session->IsOpen());
      const xstring& dir=dirname(file);
      if(dir.length()>0 && dir.ne("/") && dir.ne(".") && dir.ne("..")) {
	 // FIXME: .././.. should be also excluded
	 session->Mkdir(dirname(file),true);
	 return MOVED;
      }
   }

   if(removing)
   {
      res=session->Done();
      if(res<=0)
      {
	 removing=false;
	 file_removed=true;
	 session->Close();
	 Suspend();
	 m=MOVED;
      }
      return m;
   }

   if(Done() || Error())
      return m;

   if(verify)
   {
      if(verify->Error())
      {
	 SetError(verify->ErrorText());
	 m=MOVED;
      }
      else if(verify->Done())
      {
	 if(ShouldRename())
	 {
	    const char *new_name=dir_file(dirname(file),suggested_filename);
	    bool clobber=temp_file;
	    session->Rename(file,new_name,clobber);
	    return MOVED;
	 }
	 done=true;
	 m=MOVED;
      }
      return m;
   }

   // if we need some info and cannot start the transfer (yet),
   // then use ARRAY_INFO to fetch the file information.
   if(((want_size && size==NO_SIZE_YET) || (want_date && date==NO_DATE_YET))
   && (mode==PUT || !start_transfer) && session->IsClosed())
   {
      FileInfo *fi=new FileInfo(file);
      if(want_size)
	 fi->Need(fi->SIZE);
      if(want_date)
	 fi->Need(fi->DATE);
      info.Empty();
      info.Add(fi);
      session->GetInfoArray(&info);
      m=MOVED;
   }
   if(session->OpenMode()==FA::ARRAY_INFO)
   {
      res=session->Done();
      if(res==FA::IN_PROGRESS)
	 return m;
      if(res<0)
      {
	 session->Close();
	 SetSize(NO_SIZE);
	 SetDate(NO_DATE);
	 return MOVED;
      }
      FileInfo *fi=info[0];
      if(want_size)
	 SetSize(fi->size);
      if(want_date)
	 SetDate(fi->date);
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
      res=Put_LL(buffer+buffer_ptr,Size());
      if(res>0)
      {
	 buffer_ptr+=res;
	 m=MOVED;
      }
      else if(res<0)
	 return MOVED;
      if(Size()==0)
      {
	 if(eof)
	 {
	    if(date!=NO_DATE && date!=NO_DATE_YET)
	       session->SetDate(date);
	    if(e_size!=NO_SIZE && e_size!=NO_SIZE_YET)
	       session->SetSize(e_size);
	    res=session->StoreStatus();
	    if(res==FA::OK)
	    {
	       session->Close();
	    fxp_eof:
	       // FIXME: set date for real.
	       date_set=true;
	       if(!verify && do_verify)
		  verify=new FileVerificator(session,file);
	       else
		  done=true;
	       return MOVED;
	    }
	    else if(res==FA::IN_PROGRESS)
	       return m;
	    else
	    {
	       if(res==FA::DO_AGAIN)
		  return m;
	       if(res==FA::STORE_FAILED)
	       {
		  upload_state.Save(session);
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
      res=TuneGetSize(Get_LL(get_size));
      if(res>0)
      {
	 EmbraceNewData(res);
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

void FileCopyPeerFA::SuspendInternal()
{
   if(fxp && mode==PUT)
      return;
   if(session->IsOpen())
      session->SuspendSlave();
   super::SuspendInternal();
}
void FileCopyPeerFA::ResumeInternal()
{
   super::ResumeInternal();
   session->ResumeSlave();
}

const char *FileCopyPeerFA::GetStatus()
{
   if(verify)
      return verify->Status();
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
      if(size!=NO_SIZE && size!=NO_SIZE_YET && !ascii
      && (seek_pos>size || (seek_pos==size && size>0)))
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
      int err;
      if(use_cache && FileAccess::cache->Find(session,file,FAmode,&err,&b,&s))
      {
	 if(err)
	 {
	    SetError(b);
	    return;
	 }
	 size=s;
	 if(seek_pos>=s)
	    goto past_eof;
	 b+=seek_pos;
	 s-=seek_pos;
	 Save(0);
	 Put(b,s);
	 pos=seek_pos;
	 eof=true;
	 return;
      }
   }
   else // mode==PUT
   {
      if(e_size>=0 && size>=0 && seek_pos>=e_size)
      {
	 debug((10,"copy dst: seek past eof (seek_pos=%lld, size=%lld)\n",
		  (long long)seek_pos,(long long)e_size));
	 eof=true;
	 if(date==NO_DATE || date==NO_DATE_YET)
	    return;
      }
   }
   session->Open(file,FAmode,seek_pos);
   session->SetFileURL(orig_url);
   session->SetLimit(range_limit);
   if(mode==PUT) {
      upload_state.Restore(session);
      if(e_size!=NO_SIZE && e_size!=NO_SIZE_YET)
	 session->SetSize(e_size);
      if(date!=NO_DATE && date!=NO_DATE_YET)
	 session->SetDate(date);
   } else {
      if(size!=NO_SIZE && size!=NO_SIZE_YET)
	 session->SetSize(size);
   }
   session->RereadManual();
   if(base)
      session->SetFragile(); // fallback to base on error
   if(ascii)
      session->AsciiTransfer();
   if(want_size && size==NO_SIZE_YET)
      session->WantSize(&size);
   if(want_date && (date==NO_DATE_YET || date.ts_prec>0))
      session->WantDate(&date);
   if(mode==GET)
      SaveRollback(seek_pos);
   else
      pos=seek_pos+Size();
}

void FileCopyPeerFA::WantSize()
{
   struct stat st;
   if(!strcmp(session->GetProto(),"file")
   && stat(dir_file(session->GetCwd(),file),&st)!=-1)
      SetSize(S_ISREG(st.st_mode)?st.st_size:NO_SIZE);
   else
      super::WantSize();
}

void FileCopyPeerFA::RemoveFile()
{
   session->Open(file,FA::REMOVE);
   removing=true;
}

int FileCopyPeerFA::Get_LL(int len)
{
   if(get_delay>0)
   {
      if(!get_ll_timer.Stopped())
	 return 0;
      session->ResumeSlave();
   }

   int res=0;

   if(session->IsClosed())
      OpenSession();

   if(eof)  // OpenSession can set eof=true.
      return 0;

   off_t io_at=pos;
   if(GetRealPos()!=io_at) // GetRealPos can alter pos.
      return 0;

   res=session->Read(this,len);
   if(res<0)
   {
      if(res==FA::DO_AGAIN)
	 return 0;
      if(res==FA::FRAGILE_FAILED && base) {
	 base.revert(this);
	 return 0;
      }
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

	    if(!session->IsNewLocationPermanent() && !base)
	       base.save(this);

	    orig_url.set(loc_c);
	    file.set(session->GetNewLocationFile());
	    FAmode=session->GetNewLocationMode();
	    FileAccess *new_session=session->GetNewLocationFA();
	    session->Close();
	    if(new_session) {
	       my_session=new_session;
	       session=my_session;
	    }

	    if(want_size || size!=NO_SIZE)
	       WantSize();
	    if(want_date || date!=NO_DATE)
	       WantDate();

	    upload_state.Clear();
	    current->Timeout(0); // retry with new location.
	    return 0;
	 }
      }
      SetError(session->StrError(res));
      return -1;
   }
   else if(res==0)
   {
      debug((10,"copy-peer: EOF on %s\n",session->GetFileURL(session->GetFile()).get()));
      eof=true;
      FileAccess::cache->Add(session,file,FAmode,FA::OK,this);
      SetSuggestedFileName(session->GetSuggestedFileName());
      session->Close();
   }
   else if(res<=MAX_READ_TO_DELAY)
   {
      if(get_delay<=MAX_DELAY-DELAY_STEP)
	 get_delay+=DELAY_STEP;
      get_ll_timer.SetMicroSeconds(get_delay);
      session->SuspendSlave();
   }
   else if(res>MAX_READ_TO_DELAY && get_delay>=DELAY_STEP)
      get_delay-=DELAY_STEP;
   return res;
}

int FileCopyPeerFA::Put_LL(const char *buf,int len)
{
   if(do_mkdir)
      return 0;	  // can't write yet

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
	 upload_state.Save(session);
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

int FileCopyPeerFA::PutEOF_LL()
{
   if(mode==GET && session)
      session->Close();
   return 0;
}

off_t FileCopyPeerFA::GetRealPos()
{
   if(session->OpenMode()!=FAmode || fxp)
      return pos;
   if(mode==PUT)
   {
      if(pos-Size()!=session->GetPos())
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
      if(pos+Size()!=session->GetPos())
	 SaveRollback(session->GetPos());
   }
   return pos;
}

void FileCopyPeerFA::Init()
{
   get_delay=0;
   fxp=false;
   redirections=0;
   can_seek=true;
   can_seek0=true;
   if(FAmode==FA::LIST || FAmode==FA::LONG_LIST)
      Save(FileAccess::cache->SizeLimit());
   if(mode==PUT)
      file.set(UseTempFile(file));
}

FileCopyPeerFA::FileCopyPeerFA(FileAccess *s,const char *f,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET), file(f),
     my_session(s), session(my_session), FAmode(m)
{
   Init();
}
FileCopyPeerFA::FileCopyPeerFA(const FileAccessRef& s,const char *f,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET), file(f),
     session(s), FAmode(m)
{
   Init();
}
FileCopyPeerFA::FileCopyPeerFA(const ParsedURL *u,int m)
   : FileCopyPeer(m==FA::STORE ? PUT : GET), file(u->path), orig_url(u->orig_url),
     my_session(FileAccess::New(u)), session(my_session), FAmode(m)
{
   Init();
   if(!file)
      SetError(_("file name missed in URL"));
}

void FileCopyPeerFA::PrepareToDie()
{
   if(session)
      session->Close();
}
FileCopyPeerFA::~FileCopyPeerFA() {}

FileCopyPeerFA *FileCopyPeerFA::New(FileAccess *s,const char *url,int m)
{
   ParsedURL u(url,true);
   if(u.proto)
   {
      SessionPool::Reuse(s);
      return new FileCopyPeerFA(&u,m);
   }
   return new FileCopyPeerFA(s,url,m);
}
FileCopyPeerFA *FileCopyPeerFA::New(const FileAccessRef& s,const char *url,int m)
{
   ParsedURL u(url,true);
   if(u.proto)
      return new FileCopyPeerFA(&u,m);
   return new FileCopyPeerFA(s,url,m);
}
FileCopyPeer *FileCopyPeerFA::Clone()
{
   FileCopyPeerFA *c=new FileCopyPeerFA(session->Clone(),file,FAmode);
   c->orig_url.set(orig_url);
   return c;
}

const char *FileCopyPeerFA::UseTempFile(const char *file)
{
   const char *temp=FileCopyPeer::UseTempFile(file);
   if(temp!=file && orig_url)
      dirname_modify(orig_url).append('/').append_url_encoded(basename_ptr(temp),URL_PATH_UNSAFE);
   return temp;
}

// FileCopyPeerFDStream
#undef super
#define super FileCopyPeer

FileCopyPeerFDStream::FileCopyPeerFDStream(FDStream *o,dir_t m)
   : FileCopyPeer(m), my_stream(o?o:new FDStream(1,"<stdout>")), stream(my_stream), close_when_done(o!=0)
{
   Init();
}
FileCopyPeerFDStream::FileCopyPeerFDStream(const Ref<FDStream>& o,dir_t m)
   : FileCopyPeer(m), stream(o), close_when_done(false)
{
   Init();
}

void FileCopyPeerFDStream::Init()
{
   seek_base=0;
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
   if(mode==PUT)
      put_ll_timer=new Timer(0,200);
   if(mode==PUT && stream->fd==-1 && stream->can_setmtime())
      stream->full_name.set(UseTempFile(stream->full_name));
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
	 pos+=Size();
   }
   else
   {
      seek_pos=pos;
   }
}

int FileCopyPeerFDStream::getfd()
{
   if(do_mkdir || !stream)
      return -1;
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
      pos+=Size();
   Seek_LL();
   return fd;
}
int FileCopyPeerFDStream::Do()
{
   int m=STALL;
   if(Done() || Error())
      return m;

   if(do_mkdir) {
      do_mkdir=false;
      create_directories(dirname(stream->full_name).get_non_const());
   }

   if(verify)
   {
      if(verify->Error())
      {
	 SetError(verify->ErrorText());
	 m=MOVED;
      }
      else if(verify->Done())
      {
	 if(ShouldRename() && stream && stream->full_name)
	 {
	    const char *new_name=dir_file(dirname(stream->full_name),suggested_filename);
	    struct stat st;
	    if(temp_file || (lstat(new_name,&st)==-1 && errno==ENOENT) || ResMgr::QueryBool("xfer:clobber",0)) {
	       debug((5,"copy: renaming `%s' to `%s'\n",stream->full_name.get(),suggested_filename.get()));
	       int res=rename(stream->full_name,new_name);
	       if(res==-1 && errno==EIO) {
		  // FUSE with HadoopFS workaround
		  unlink(new_name);
		  res=rename(stream->full_name,new_name);
	       }
	       if(res==-1) {
		  const char *err=xstring::format("rename(%s, %s): %s\n",stream->full_name.get(),new_name,strerror(errno));
		  if(temp_file)
		     SetError(err);
		  else
		     debug((3,"%s\n",err));
	       }
	    }
	 }
	 done=true;
	 m=MOVED;
      }
      return m;
   }

   bool check_min_size=true;
#ifndef NATIVE_CRLF
   if(ascii)
      check_min_size=false;
#endif
   int res;
   switch(mode)
   {
   case PUT:
      if(Size()==0)
      {
	 if(eof)
	 {
	    // make sure the stream is open - it may create an empty file.
	    if(stream && !stream->is_closed() && getfd()==-1)
	       return m;
	    if(!date_set && date!=NO_DATE && do_set_date)
	    {
	       if(date==NO_DATE_YET)
		  return m;
	       stream->setmtime(date);
	       date_set=true;
	       m=MOVED;
	    }
	    if(stream && close_when_done && !stream->Done())
	       return m;
	    if(!verify && do_verify)
	       verify=new FileVerificator(stream);
	    else
	       done=true;
	    return MOVED;
	 }
	 if(seek_pos==0)
	    return m;
      }
      if(!write_allowed)
	 return m;
      if(getfd()==-1)
	 return m;

      if(check_min_size && !eof && Size()<PUT_LL_MIN
      && put_ll_timer && !put_ll_timer->Stopped())
	 break;
      res=Put_LL(buffer+buffer_ptr,Size());
      if(res>0)
	 buffer_ptr+=res;
      if(res!=0)
	 m=MOVED;
      break;

   case GET:
      if(eof)
	 return m;

      res=TuneGetSize(Get_LL(get_size));
      if(res>0)
      {
	 EmbraceNewData(res);
	 SaveMaxCheck(0);
      }
      if(res!=0 || eof)
	 m=MOVED;
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
	 pos=seek_pos;
	 if(mode==PUT)
	    pos+=Size();
	 return;
      }
      else
      {
	 off_t s=stream->get_size();
	 if(s!=-1)
	 {
	    SetSize(s);
	    pos=seek_pos+((mode==PUT)?Size():0);
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
	 SetSize(S_ISREG(st.st_mode)?st.st_size:NO_SIZE);
#ifndef NATIVE_CRLF
	 if(ascii)
	    SetSize(NO_SIZE);
#endif
      }
   }

   if(need_seek)  // this does not combine with ascii.
      lseek(fd,seek_base+pos,SEEK_SET);

   char *p=GetSpace(ascii?len*2:len);
   res=read(fd,p,len);
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

   if(res==0) {
      debug((10,"copy-peer: EOF on FD %d\n",fd));
      eof=true;
   }
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
      lseek(fd,seek_base+pos-Size(),SEEK_SET);

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
	 buffer.truncate(buffer_ptr);
	 eof=true;
	 return -1;
      }
      if(stream->NonFatalError(errno))
      {
	 // in case of full disk, check file correctness.
	 if(errno==ENOSPC && can_seek)
	 {
	    struct stat st;
	    if(fstat(fd,&st)!=-1)
	    {
	       if(st.st_size<seek_base+pos-Size())
	       {
		  // workaround solaris nfs bug. It can lose data if disk is full.
		  if(buffer_ptr>=seek_base+pos-Size()-buffer_ptr-st.st_size)
		     UnSkip(seek_base+pos-Size()-st.st_size);
		  else
		  {
		     Empty();
		     pos=st.st_size;
		  }
	       }
	    }
	 }
	 return 0;
      }
      stream->MakeErrorText();
      SetError(stream->error_text);
      return -1;
   }
   stream->clear_status();
   if(res==len && skip_cr)
   {
      res+=skip_cr;
      // performance gets worse because of writing a single char,
      // but leaving uncomplete line on screen allows mixing it with debug text.
      if(write(fd,"\n",1)==1)
	 res+=1;
   }
   if(put_ll_timer)
      put_ll_timer->Reset();
   return res;
}
FgData *FileCopyPeerFDStream::GetFgData(bool fg)
{
   if(!my_stream || !create_fg_data)
      return 0;	  // if we don't own the stream, don't create FgData.
   if(stream->GetProcGroup())
      return new FgData(stream->GetProcGroup(),fg);
   return 0;
}

void FileCopyPeerFDStream::WantSize()
{
   struct stat st;
   int res=-1;

   if(stream->fd!=-1)
      res=fstat(stream->fd,&st);
   else if(stream->full_name)
      res=stat(stream->full_name,&st);

   if(res!=-1)
      SetSize(S_ISREG(st.st_mode)?st.st_size:NO_SIZE);
   else
      super::WantSize();
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
   if(verify)
      return verify->Status();
   return stream->status;
}
void FileCopyPeerFDStream::Kill(int sig)
{
   stream->Kill(sig);
}


FileCopyPeerFDStream *FileCopyPeerFDStream::NewPut(const char *file,bool cont)
{
   int flags=O_WRONLY|O_CREAT;
   if(!cont) {
      flags|=O_TRUNC;
      if(!ResMgr::QueryBool("xfer:clobber",0))
	 flags|=O_EXCL;
   }
   return new FileCopyPeerFDStream(new FileStream(file,flags),
				    FileCopyPeer::PUT);
}
FileCopyPeerFDStream *FileCopyPeerFDStream::NewGet(const char *file)
{
   return new FileCopyPeerFDStream(new FileStream(file,O_RDONLY),
				    FileCopyPeer::GET);
}
FileCopyPeer *FileCopyPeerFDStream::Clone()
{
   NeedSeek();
   FileCopyPeerFDStream *peer=new FileCopyPeerFDStream(stream,mode);
   peer->NeedSeek();
   peer->SetBase(0);
   return peer;
}

// FileCopyPeerDirList
FileCopyPeerDirList::FileCopyPeerDirList(FA *s,ArgV *v)
   : FileCopyPeer(GET), session(s)
{
   dl=session->MakeDirList(v);
   if(dl==0)
      eof=true;
   can_seek=false;
   can_seek0=false;
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
   memcpy(GetSpace(s),b,s);
   SpaceAdd(s);
   dl->Skip(s);
   return MOVED;
}

// FileCopyPeerMemory
int FileCopyPeerMemory::Do()
{
   int m=STALL;
   if(mode==PUT) {
      max_buf=max_size+1;
      if(Size()>max_size) {
	 SetError("buffer limit exceeded");
	 broken=true;
	 return MOVED;
      }
   }
   return m;
}

// FileVerificator
void FileVerificator::Init0()
{
   done=false;
   if(!ResMgr::QueryBool("xfer:verify",0)
   || ResMgr::Query("xfer:verify-command",0).is_empty())
      done=true;
}
void FileVerificator::InitVerify(const char *f)
{
   if(done)
      return;
   ArgV *args=new ArgV(ResMgr::Query("xfer:verify-command",0));
   args->Append(f);
   Log::global->Format(9,"running %s %s\n",args->a0(),f);
   verify_process=new InputFilter(args);
   verify_process->StderrToStdout();
   verify_buffer=new IOBufferFDStream(verify_process.Cast<FDStream>(),IOBuffer::GET);
}
FileVerificator::FileVerificator(const char *f)
{
   Init0();
   InitVerify(f);
}
FileVerificator::FileVerificator(const FDStream *stream)
{
   Init0();
   if(done)
      return;
   const char *f=stream->full_name;
   if(!f)
   {
      done=true;
      return;
   }
   const char *cwd=stream->GetCwd();
   int cwd_len=xstrlen(cwd);
   if(cwd && cwd_len>0 && !strncmp(f,cwd,cwd_len))
   {
      f+=cwd_len;
      while(*f=='/')
	 f++;
      if(*f==0)
	 f=".";
   }
   InitVerify(f);
   if(verify_process)
   {
      verify_process->SetProcGroup(stream->GetProcGroup());
      verify_process->SetCwd(cwd);
   }
}
FileVerificator::FileVerificator(const FileAccess *session,const char *f)
{
   Init0();
   if(done)
      return;
   if(strcmp(session->GetProto(),"file"))
   {
      done=true;
      return;
   }
   InitVerify(f);
   verify_process->SetCwd(session->GetCwd());
}

FileVerificator::~FileVerificator() {}

int FileVerificator::Do()
{
   int m=STALL;
   if(done)
      return m;
   verify_process->Kill(SIGCONT);
   if(!verify_buffer->Eof())
      return m;
   if(verify_process->GetProcState()!=ProcWait::TERMINATED)
      return m;
   done=true;
   m=MOVED;
   if(verify_process->GetProcExitCode()!=0)
   {
      error_text.set(verify_buffer->Get());
      error_text.rtrim('\n');
      if(error_text.length()==0)
	 error_text.set(_("Verify command failed without a message"));
      const char *nl=strrchr(error_text,'\n');
      if(nl)
	 error_text.set(nl+1);
   }
   return m;
}


// special pointer to creator of ftp/ftp copier. It is init'ed in Ftp class.
FileCopy *(*FileCopy::fxp_create)(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
