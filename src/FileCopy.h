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

/*
  classes defined here:
   FileCopy
   FileCopyPeer
   +FileCopyPeerFA
   +FileCopyPeerFDStream
   +FileCopyPeerString
   \FileCopyPeerList
*/

#ifndef FILECOPY_H
#define FILECOPY_H

#include "SMTask.h"
#include "buffer.h"
#include "FileAccess.h"
#include "Speedometer.h"

#define FILE_END     (-1L)

class FileCopyPeer : public Buffer
{
protected:
   bool want_size;
   bool want_date;
   long size;
   long e_size;
   time_t date;

   long seek_pos;
   bool can_seek;
   bool can_seek0;
   bool date_set;
   bool do_set_date;
   bool removing;

   bool ascii;
   bool use_cache;

public:
   enum direction { GET, PUT };

   long range_start; // NOTE: ranges are implemented only partially. (FIXME)
   long range_limit;

protected:
   enum direction mode;

   ~FileCopyPeer();

public:
   bool CanSeek() { return can_seek; }
   bool CanSeek0() { return can_seek0; }
   bool CanSeek(long p) { return p==0 ? CanSeek0() : CanSeek(); }
   long GetSeekPos() { return seek_pos; }
   virtual void Seek(long offs) { seek_pos=offs; Empty(); eof=false; broken=false; }
   virtual long GetRealPos() { return pos; }
   virtual long Buffered() { return in_buffer; }
   virtual bool IOReady() { return true; }

   virtual void WantDate() { want_date=true; date=NO_DATE_YET; }
   virtual void WantSize() { want_size=true; size=NO_SIZE_YET; }
   time_t GetDate() { return date; }
   long   GetSize() { return size; }

   void SetDate(time_t d);
   void SetSize(long s);
   void SetEntitySize(long s) { e_size=s; }

   void DontCopyDate() { do_set_date=false; }
   bool NeedDate() { return do_set_date; }

   void SetRange(long s,long lim) { range_start=s; range_limit=lim; }

   FileCopyPeer(direction m);

   bool Done();

   void Ascii() { ascii=true; }
   virtual void NoCache() { use_cache=false; }

   virtual const char *GetStatus() { return 0; }
   virtual bool NeedSizeDateBeforehand() { return false; }

   virtual pid_t GetProcGroup() { return 0; }
   virtual void Kill(int sig) {}

   virtual void RemoveFile() {}
   virtual void NeedSeek() {} // fd is shared, seek before access.

   void CannotSeek(int p)
      {
	 can_seek=false;
	 if(p==0)
	    can_seek0=false;
      }
   virtual FA *GetSession() { return 0; } // for fxp.
   virtual void Fg() {}
   virtual void Bg() {}
};

class FileCopy : public SMTask
{
public:
   FileCopyPeer *get;
   FileCopyPeer *put;

protected:
   enum state_t
      {
	 INITIAL,
	 GET_INFO_WAIT,
	 PUT_WAIT,
	 DO_COPY,
	 CONFIRM_WAIT,
	 GET_DONE_WAIT,
	 ALL_DONE
      } state;

private:

   int max_buf;
   bool cont;

   char *error_text;

   Speedometer *rate;
   Speedometer *rate_for_eta;
   int put_buf;

   time_t start_time;
   int start_time_ms;
   time_t end_time;
   int end_time_ms;

   bool fail_if_cannot_seek;
   bool remove_source_later;

   Buffer *line_buffer;
   int  line_buffer_max;

protected:
   void RateAdd(long a)
      {
	 rate->Add(a);
	 rate_for_eta->Add(a);
      }
   void RateReset()
      {
	 start_time=now;
	 start_time_ms=now_ms;
	 rate->Reset();
	 rate_for_eta->Reset();
      }
   long bytes_count;

   ~FileCopy();

public:
   long GetPos();
   long GetSize();
   int  GetPercentDone();
   const char *GetPercentDoneStr();
   float GetRate();
   const char *GetRateStr();
   long GetBytesRemaining();
   long GetETA() { return GetETA(GetBytesRemaining()); }
   long GetETA(long b);
   const char *GetETAStr();
   const char *GetETAStrSFromTime(time_t t) { return rate_for_eta->GetETAStrSFromTime(t); }
   const char *GetStatus();
   FgData *GetFgData(bool fg);
   pid_t GetProcGroup();
   void Kill(int sig);
   long GetBytesCount() { return bytes_count; }
   time_t GetTimeSpent();
   int GetTimeSpentMilli();

   void SetDate(time_t t) { get->SetDate(t); }
   void SetSize(long   s) { get->SetSize(s); }

   bool Done() { return state==ALL_DONE; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }
   void SetError(const char *str);

   void DontCopyDate() { put->DontCopyDate(); }
   void Ascii() { get->Ascii(); put->Ascii(); }
   void FailIfCannotSeek() { fail_if_cannot_seek=true; }
   void SetRange(long s,long lim) { get->SetRange(s,lim); put->SetRange(s,lim); }
   void RemoveSourceLater() { remove_source_later=true; }
   void LineBuffered(int size=0x1000);

   FileCopy(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
   void Init();

   int Do();
   void Suspend();
   void Resume();
   void Fg();
   void Bg();

   void Reconfig(const char *);

   static FileCopy *New(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
   static FileCopy *(*fxp_create)(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
};

class FileCopyPeerFA : public FileCopyPeer
{
   char *file;
   int FAmode;
   FileAccess *session;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

   FileAccess::fileinfo info;

   bool reuse_later;
   bool fxp;   // FXP (ftp<=>ftp copy) active

protected:
   ~FileCopyPeerFA();

public:
   FileCopyPeerFA(FileAccess *s,const char *f,int m);
   FileCopyPeerFA(class ParsedURL *u,int m);
   int Do();
   bool IOReady();
   long GetRealPos();
   void Seek(long new_pos);

   long Buffered() { return in_buffer+session->Buffered(); }

   void Suspend();
   void Resume();

   void DontReuseSession() { reuse_later=false; }

   const char *GetStatus();

   bool NeedSizeDateBeforehand() { return session->NeedSizeDateBeforehand(); }
   void RemoveFile();

   static FileCopyPeerFA *New(FA *s,const char *url,int m,bool reuse=false);

   void OpenSession();
   FA *GetSession() { return session; }
   void Fg() { session->SetPriority(1); }
   void Bg() { session->SetPriority(0); }
   void SetFXP(bool on) { fxp=on; }
};

class FileCopyPeerFDStream : public FileCopyPeer
{
   FDStream *stream;
   long seek_base;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

   int getfd();

   bool delete_stream;
   bool create_fg_data;
   bool need_seek;

protected:
   ~FileCopyPeerFDStream();

public:
   FileCopyPeerFDStream(FDStream *o,direction m);
   int Do();
   bool Done();
   bool IOReady();
   void Seek(long new_pos);
   FgData *GetFgData(bool fg);
   pid_t GetProcGroup() { return stream->GetProcGroup(); }
   void Kill(int sig) { stream->Kill(sig); }

   void DontDeleteStream() { delete_stream=false; }
   void DontCreateFgData() { create_fg_data=false; }
   void NeedSeek() { need_seek=true; }
   void RemoveFile();
   void SetBase(long b) { seek_base=b; }

   static FileCopyPeerFDStream *NewPut(const char *file,bool cont=false);
   static FileCopyPeerFDStream *NewGet(const char *file);
};

class FileCopyPeerString : public FileCopyPeer
{
protected:
   ~FileCopyPeerString();

public:
   FileCopyPeerString(const char *s,int len=-1);
   void Seek(long new_pos);
};

class FileCopyPeerDirList : public FileCopyPeer
{
private:
   FileAccess *session;
   DirList *dl;

protected:
   ~FileCopyPeerDirList();

public:
   FileCopyPeerDirList(FA *s,ArgV *v); // consumes s and v.

   int Do();
   void NoCache() { if(dl) dl->UseCache(false); }
   void Fg() { session->SetPriority(1); }
   void Bg() { session->SetPriority(0); }
};

#endif
