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
#include <errno.h>
#include "buffer.h"
#include "FileAccess.h"
#include "misc.h"
#include "trio.h"
#include "Speedometer.h"
#include "log.h"

#define BUFFER_INC	   (8*1024) // should be power of 2

const char *Buffer::Get() const
{
   if(Size()==0)
      return eof?0:"";
   return buffer+buffer_ptr;
}

void Buffer::Get(const char **buf,int *size) const
{
   *size=Size();
   *buf=Get();
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
   *size=buffer.length();
}

void Buffer::SaveRollback(off_t p)
{
   pos=p;
   if(buffer_ptr<p)
      save=false;
   if(!save)
      p=0;
   buffer.truncate(buffer_ptr=p);
}

void Buffer::Allocate(int size)
{
   if(buffer_ptr>0 && Size()==0 && !save)
   {
      buffer.truncate(0);
      buffer_ptr=0;
   }

   size_t in_buffer_real=Size();
   /* disable data movement to beginning of the buffer, if:
      1. we save the data explicitly;
      2. we add more data than there is space in the beginning of the buffer
	 (because the probability of realloc is high anyway);
      3. the gap at beginning is smaller than the amount of data in the buffer
	 (because the penalty of data movement is high). */
   if(save || buffer_ptr<size || buffer_ptr<Size())
      in_buffer_real+=buffer_ptr;

   // could be round-robin, but this is easier
   if(buffer.length()>in_buffer_real)
   {
      buffer.nset(buffer+buffer_ptr,Size());
      buffer_ptr=0;
   }

   buffer.get_space2(in_buffer_real+size,BUFFER_INC);
}

void Buffer::SaveMaxCheck(int size)
{
   if(save && buffer_ptr+size>save_max)
      save=false;
}

void Buffer::Append(const char *buf,int size)
{
   if(size==0)
      return;

   SaveMaxCheck(size);
   if(Size()==0 && buffer_ptr>0 && !save)
   {
      buffer.truncate(0);
      buffer_ptr=0;
   }

   memmove(GetSpace(size),buf,size);
   SpaceAdd(size);
}
void Buffer::Put(const char *buf,int size)
{
   Append(buf,size);
   pos+=size;
}
void Buffer::Prepend(const char *buf,int size)
{
   if(size==0)
      return;
   save=false;
   if(Size()==0)
   {
      memmove(GetSpace(size),buf,size);
      SpaceAdd(size);
      return;
   }
   if(buffer_ptr<size)
   {
      Allocate(size-buffer_ptr);
      memmove(buffer.get_non_const()+size,buffer+buffer_ptr,Size());
      SpaceAdd(size-buffer_ptr);
      buffer_ptr=size;
   }
   memmove(buffer.get_non_const()+buffer_ptr-size,buf,size);
   buffer_ptr-=size;
}

void Buffer::Format(const char *f,...)
{
   va_list v;
   va_start(v,f);
   vFormat(f, v);
   va_end(v);
}

void Buffer::vFormat(const char *f, va_list v)
{
   int size=64;
   for(;;)
   {
      va_list tmp;
      VA_COPY(tmp,v);
      int res=vsnprintf(GetSpace(size), size, f, tmp);
      va_end(tmp);
      if(res>=0 && res<size)
      {
	 SpaceAdd(res);
	 return;
      }
      if(res>size)   // some vsnprintf's return desired buffer size.
	 size=res+1;
      else
	 size*=2;
   }
}

void Buffer::Skip(int len)
{
   if(len>Size())
      len=Size();
   buffer_ptr+=len;
   pos+=len;
}
void Buffer::UnSkip(int len)
{
   if(len>buffer_ptr)
      len=buffer_ptr;
   buffer_ptr-=len;
   pos-=len;
}

void Buffer::Empty()
{
   buffer.truncate(0);
   buffer_ptr=0;
   if(save_max>0)
      save=true;
}

// move data from other buffer, prepare for SpaceAdd.
int Buffer::MoveDataHere(Buffer *o,int max_len)
{
   const char *b;
   int size;
   o->Get(&b,&size);
   if(size>max_len)
      size=max_len;
   if(size>0) {
      if(size>=64 && Size()==0 && o->Size()==size && !save && !o->save) {
	 // optimization by swapping buffers
	 buffer.swap(o->buffer);
	 buffer_ptr=replace_value(o->buffer_ptr,buffer_ptr);
	 buffer.set_length_no_z(buffer_ptr);
	 o->pos+=size;
      } else {
	 memcpy(GetSpace(size),b,size);
	 o->Skip(size);
      }
   }
   return size;
}

Buffer::Buffer()
{
   saved_errno=0;
   error_fatal=false;
   buffer_ptr=0;
   eof=false;
   broken=false;
   save=false;
   save_max=0;
   pos=0;
}
Buffer::~Buffer()
{
}

const char *Buffer::GetRateStrS()
{
   if(!rate || !rate->Valid())
      return "";
   return rate->GetStrS();
}
void Buffer::RateAdd(int n)
{
   if(!rate)
      return;
   rate->Add(n);
}

void Buffer::SetError(const char *e,bool fatal)
{
   error_text.set(e);
   error_fatal=fatal;
}
void Buffer::SetErrorCached(const char *e)
{
   SetError(e,false);
   error_text.append(_(" [cached]"));
}
const char *Buffer::Dump() const
{
   if(buffer_ptr==0)
      return buffer.dump();
   return xstring::get_tmp(Get(),Size()).dump();
}

void DataTranslator::AppendTranslated(Buffer *target,const char *put_buf,int size)
{
   off_t old_pos=target->GetPos();
   PutTranslated(target,put_buf,size);
   target->SetPos(old_pos);
}

void DirectedBuffer::SetTranslator(DataTranslator *t)
{
   if(mode==GET && !translator && Size()>0) {
      // translate unread data
      const char *data;
      int len;
      Get(&data,&len);
      t->Put(data,len);
      buffer.truncate(buffer_ptr);
      t->AppendTranslated(this,0,0);
   }
   translator=t;
}

#ifdef HAVE_ICONV
void DataRecoder::PutTranslated(Buffer *target,const char *put_buf,int size)
{
   bool from_untranslated=false;
   if(Size()>0)
   {
      Put(put_buf,size);
      Get(&put_buf,&size);
      from_untranslated=true;
   }
   if(size<=0)
      return;
   if(!backend_translate)
   {
      target->Put(put_buf,size);
      if(from_untranslated)
	 Skip(size);
      return;
   }
   size_t put_size=size;

   int size_coeff=6;
try_again:
   if(put_size==0)
      return;
   size_t store_size=size_coeff*put_size;
   char *store_space=target->GetSpace(store_size);
   char *store_buf=store_space;
   const char *base_buf=put_buf;
   // do the translation
   ICONV_CONST char **put_buf_ptr=const_cast<ICONV_CONST char**>(&put_buf);
   size_t res=iconv(backend_translate,put_buf_ptr,&put_size,&store_buf,&store_size);
   target->SpaceAdd(store_buf-store_space);
   if(from_untranslated)
      Skip(put_buf-base_buf);
   if(res==(size_t)-1)
   {
      switch(errno)
      {
      case EINVAL: // incomplete character
	 if(!from_untranslated)
	    Put(put_buf,put_size);
	 break;
      case EILSEQ: // invalid character
	 target->Put("?");
	 put_buf++;
	 put_size--;
	 goto try_again;
      case E2BIG:  // no room to store result, allocate more.
	 size_coeff*=2;
	 goto try_again;
      default:
	 break;
      }
   }
   return;
}
void DataRecoder::ResetTranslation()
{
   Empty();
   if(!backend_translate)
      return;
   iconv(backend_translate,0,0,0,0);
}
DataRecoder::~DataRecoder()
{
   if(backend_translate)
      iconv_close(backend_translate);
}
DataRecoder::DataRecoder(const char *from_code,const char *to_code,bool translit)
{
   backend_translate=0;

   if(translit) {
      const char *to_code_translit=xstring::cat(to_code,"//TRANSLIT",NULL);
      backend_translate=iconv_open(to_code_translit,from_code);
      if(backend_translate!=(iconv_t)-1) {
	 Log::global->Format(9,"initialized translation from %s to %s\n",from_code,to_code_translit);
	 return;
      }
      backend_translate=0;
   }

   backend_translate=iconv_open(to_code,from_code);
   if(backend_translate!=(iconv_t)-1) {
      Log::global->Format(9,"initialized translation from %s to %s\n",from_code,to_code);
      return;
   }

   Log::global->Format(0,"iconv_open(%s,%s) failed: %s\n",
			      to_code,from_code,strerror(errno));
   backend_translate=0;
}

void DirectedBuffer::SetTranslation(const char *enc,bool translit)
{
   if(!enc || !*enc)
      return;
   const char *local_code=ResMgr::Query("file:charset",0);
   if(!local_code || !*local_code)
      return;
   const char *from_code=(mode==GET?enc:local_code);
   const char *to_code  =(mode==GET?local_code:enc);
   if(!strcasecmp(from_code,to_code))
      return;
   SetTranslator(new DataRecoder(from_code,to_code,translit));
}
#endif //HAVE_ICONV

void DirectedBuffer::ResetTranslation()
{
   if(translator)
      translator->ResetTranslation();
}
void DirectedBuffer::Put(const char *buf,int size)
{
   if(mode==PUT && translator)
      translator->PutTranslated(this,buf,size);
   else
      Buffer::Put(buf,size);
}
void DirectedBuffer::PutTranslated(const char *buf,int size)
{
   if(translator)
      translator->PutTranslated(this,buf,size);
   else
      Buffer::Put(buf,size);
}
int DirectedBuffer::MoveDataHere(Buffer *buf,int size)
{
   if(size>buf->Size())
      size=buf->Size();
   if(mode==PUT && translator)
      translator->PutTranslated(this,buf->Get(),size);
   else
      return Buffer::MoveDataHere(buf,size);
   return size;
}
void DirectedBuffer::PutEOF()
{
   if(mode==PUT && translator)
      translator->PutTranslated(this,0,0);
   Buffer::PutEOF();
}

void DirectedBuffer::EmbraceNewData(int len)
{
   if(len<=0)
      return;
   RateAdd(len);
   if(translator)
   {
      // copy the data to free room for translated data
      translator->Put(buffer+buffer.length(),len);
      translator->AppendTranslated(this,0,0);
   }
   else
      SpaceAdd(len);
   SaveMaxCheck(0);
}


IOBuffer::IOBuffer(dir_t m)
   : DirectedBuffer(m), event_time(now),
     max_buf(0), get_size(GET_BUFSIZE)
{
}
IOBuffer::~IOBuffer()
{
}

void IOBuffer::Put(const char *buf,int size)
{
   if(size>=PUT_LL_MIN && Size()==0 && mode==PUT && !save && !translator)
   {
      int res=Put_LL(buf,size);
      if(res>=0)
      {
	 buf+=res;
	 size-=res;
	 pos+=res;
      }
   }
   if(size<=0)
      return;
   if(Size()==0)
      current->Timeout(0);
   DirectedBuffer::Put(buf,size);
}
void IOBuffer::Put(const char *buf)
{
   Put(buf,strlen(buf));
}

int IOBuffer::TuneGetSize(int res)
{
   if(res>0)
   {
      // buffer size tuning depending on data rate
      const int max_get_size=(max_buf?max_buf:0x100000);
      if(res>get_size/2 && Size()+get_size*2<=max_get_size)
	 get_size*=2;
   }
   return res;
}

int IOBuffer::Do()
{
   if(Done() || Error())
      return STALL;
   int res=0;
   switch(mode)
   {
   case PUT:
      if(Size()==0)
	 return STALL;
      res=Put_LL(buffer+buffer_ptr,Size());
      if(res>0)
      {
	 RateAdd(res);
	 buffer_ptr+=res;
	 event_time=now;
	 if(eof)
	    PutEOF_LL();
	 return MOVED;
      }
      break;

   case GET:
      if(eof)
	 return STALL;
      res=TuneGetSize(Get_LL(get_size));
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
   return STALL;
}

// IOBufferStacked implementation
#undef super
#define super IOBuffer
int IOBufferStacked::Do()
{
   int m=STALL;
   if(Done() || Error())
      return m;
   int res=0;
   switch(mode)
   {
   case PUT:
      if(down->Broken() && !broken)
      {
	 broken=true;
	 return MOVED;
      }
      if(down->Error())
      {
	 SetError(down->ErrorText(),down->ErrorFatal());
	 m=MOVED;
      }
      if(Size()==0)
	 return m;
      res=Put_LL(buffer+buffer_ptr,Size());
      if(res>0)
      {
	 buffer_ptr+=res;
	 m=MOVED;
      }
      break;

   case GET:
      if(eof)
	 return m;
      res=Get_LL(/*unused*/0);
      if(res>0)
      {
	 EmbraceNewData(res);
	 m=MOVED;
      }
      if(eof)
	 m=MOVED;
      if(down->Error())
      {
	 SetError(down->ErrorText(),down->ErrorFatal());
	 m=MOVED;
      }
      break;
   }
   if(res<0)
      return MOVED;
   return m;
}
int IOBufferStacked::Put_LL(const char *buf,int size)
{
   if(down->Broken())
   {
      broken=true;
      return -1;
   }
   down->Put(buf,size);
   return size;
}

int IOBufferStacked::Get_LL(int)
{
   if(max_buf && Size()>=max_buf) {
      down->SuspendSlave();
      return 0;
   }
   down->ResumeSlave();
   int size=MoveDataHere(down,down->Size());
   if(down->Size()==0 && down->Eof())
      PutEOF();
   return size;
}

bool IOBufferStacked::Done()
{
   if(super::Done())
      return down->Done();
   return false;
}

void IOBufferStacked::SuspendInternal()
{
   super::SuspendInternal();
   down->SuspendSlave();
}
void IOBufferStacked::ResumeInternal()
{
   if(!max_buf || Size()<max_buf)
      down->ResumeSlave();
   super::ResumeInternal();
}

// IOBufferFDStream implementation
#include <fcntl.h>
#include <unistd.h>
#undef super
#define super IOBuffer
int IOBufferFDStream::Put_LL(const char *buf,int size)
{
   if(put_ll_timer && !eof && Size()<PUT_LL_MIN
   && !put_ll_timer->Stopped())
      return 0;
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
      TimeoutS(1);
      event_time=now;
      return 0;
   }

   res=write(fd,buf,size);
   if(res==-1)
   {
      saved_errno=errno;
      if(E_RETRY(saved_errno))
      {
	 Block(fd,POLLOUT);
	 return 0;
      }
      if(NonFatalError(saved_errno))
	 return 0;
      if(errno==EPIPE)
      {
	 broken=true;
	 return -1;
      }
      stream->MakeErrorText(saved_errno);
      goto stream_err;
   }
   if(put_ll_timer)
      put_ll_timer->Reset();
   return res;

stream_err:
   SetError(stream->error_text,!TemporaryNetworkError(saved_errno));
   return -1;
}

int IOBufferFDStream::Get_LL(int size)
{
   if(max_buf && Size()>=max_buf)
      return 0;

   int res=0;

   int fd=stream->getfd();
   if(fd==-1)
   {
      if(stream->error())
	 goto stream_err;
      TimeoutS(1);
      return 0;
   }

   if(!Ready(fd,POLLIN))
   {
      Block(fd,POLLIN);
      return 0;
   }

   res=read(fd,GetSpace(size),size);
   if(res==-1)
   {
      saved_errno=errno;
      if(E_RETRY(saved_errno))
      {
	 SetNotReady(fd,POLLIN);
	 Block(fd,POLLIN);
	 return 0;
      }
      if(NonFatalError(saved_errno))
	 return 0;
      stream->MakeErrorText(saved_errno);
      goto stream_err;
   }

   if(res==0) {
      Log::global->Format(10,"buffer: EOF on FD %d\n",fd);
      eof=true;
   }
   return res;

stream_err:
   SetError(stream->error_text,!TemporaryNetworkError(saved_errno));
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
   if(put_ll_timer)
      put_ll_timer->Stop();
   if(super::Done())
      return stream->Done(); // stream->Done indicates if sub-process finished
   return false;
}

IOBufferFDStream::~IOBufferFDStream() {}


// IOBufferFileAccess implementation
#undef super
#define super IOBuffer
int IOBufferFileAccess::Get_LL(int size)
{
   if(max_buf && Size()>=max_buf) {
      session->SuspendSlave();
      return 0;
   }
   session->ResumeSlave();

   int res=0;

   res=session->Read(this,size);
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

void IOBufferFileAccess::SuspendInternal()
{
   super::SuspendInternal();
   session->SuspendSlave();
}
void IOBufferFileAccess::ResumeInternal()
{
   if(!max_buf || Size()<max_buf)
      session->ResumeSlave();
   super::ResumeInternal();
}
const char *IOBufferFileAccess::Status()
{
   return session->CurrentStatus();
}

unsigned long long Buffer::UnpackUINT64BE(int offset) const
{
   if(Size()-offset<8)
      return 0;
   unsigned long long res=UnpackUINT32BE(offset);
   res=(res<<32)|UnpackUINT32BE(offset+4);
   return res;
}
long long Buffer::UnpackINT64BE(int offset) const
{
   unsigned long long n=UnpackUINT64BE(offset);
   if(n&0x8000000000000000ULL)
      return -(long long)(n^0xFFFFFFFFFFFFFFFFULL)-1;
   return (long long)n;
}
unsigned Buffer::UnpackUINT32BE(int offset) const
{
   if(Size()-offset<4)
      return 0;
   unsigned char *b=(unsigned char*)buffer.get()+buffer_ptr+offset;
   return (b[0]<<24)|(b[1]<<16)|(b[2]<<8)|b[3];
}
int Buffer::UnpackINT32BE(int offset) const
{
   unsigned n=UnpackUINT32BE(offset);
   if(n&0x80000000U)
      return -(int)(n^0xFFFFFFFFU)-1;
   return (int)n;
}
unsigned Buffer::UnpackUINT16BE(int offset) const
{
   if(Size()-offset<2)
      return 0;
   unsigned char *b=(unsigned char*)buffer.get()+buffer_ptr+offset;
   return (b[0]<<8)|b[1];
}
unsigned Buffer::UnpackUINT8(int offset) const
{
   if(Size()-offset<1)
      return 0;
   unsigned char *b=(unsigned char*)buffer.get()+buffer_ptr+offset;
   return b[0];
}
void Buffer::PackUINT64BE(unsigned long long data)
{
#ifndef NDEBUG
   Log::global->Format(11,"PackUINT64BE(0x%016llX)\n",data);
#endif
   Allocate(8);
   PackUINT32BE((unsigned)(data>>32));
   PackUINT32BE((unsigned)(data&0xFFFFFFFFU));
}
void Buffer::PackINT64BE(long long data)
{
   unsigned long long n;
   if(data<0)
      n=((unsigned long long)(-data)^0xFFFFFFFFFFFFFFFFULL)+1;
   else
      n=(unsigned long long)data;
   PackUINT64BE(n);
}
void Buffer::PackUINT32BE(unsigned data)
{
#ifndef NDEBUG
   Log::global->Format(11,"PackUINT32BE(0x%08X)\n",data);
#endif
   char *b=GetSpace(4);
   b[0]=(data>>24)&255;
   b[1]=(data>>16)&255;
   b[2]=(data>>8)&255;
   b[3]=(data)&255;
   SpaceAdd(4);
}
void Buffer::PackINT32BE(int data)
{
   unsigned n;
   if(data<0)
      n=((unsigned)(-data)^0xFFFFFFFFU)+1;
   else
      n=(unsigned)data;
   PackUINT32BE(n);
}
void Buffer::PackUINT16BE(unsigned data)
{
   char *b=GetSpace(2);
   b[0]=(data>>8)&255;
   b[1]=(data)&255;
   SpaceAdd(2);
}
void Buffer::PackUINT8(unsigned data)
{
#ifndef NDEBUG
   Log::global->Format(11,"PackUINT8(0x%02X)\n",data);
#endif
   char *b=GetSpace(1);
   b[0]=(data)&255;
   SpaceAdd(1);
}
