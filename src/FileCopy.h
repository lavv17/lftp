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

/*
  classes defined here:
   FileCopy
   FileCopyPeer
   +FileCopyPeerFA
   +FileCopyPeerFDStream
   \FileCopyPeerList
*/

#ifndef FILECOPY_H
#define FILECOPY_H

#include "SMTask.h"
#include "buffer.h"
#include "FileAccess.h"
#include "Speedometer.h"
#include "Timer.h"
#include "log.h"

class FileCopyPeer : public IOBuffer
{
protected:
   bool want_size;
   bool want_date;
   bool start_transfer;
   off_t size;
   off_t e_size;
   FileTimestamp date;

   off_t seek_pos;
   bool can_seek;
   bool can_seek0;
   bool date_set;
   bool do_set_date;
   bool do_verify;
   bool removing;
   bool file_removed;
   bool temp_file;
   bool do_mkdir;

   bool done;

   bool ascii;
   bool use_cache;

   bool write_allowed;

   xstring_c suggested_filename;
   bool auto_rename;

public:
   off_t range_start; // NOTE: ranges are implemented only partially. (FIXME)
   off_t range_limit;

   bool CanSeek() { return can_seek; }
   bool CanSeek0() { return can_seek0; }
   bool CanSeek(off_t p) { return p==0 ? CanSeek0() : CanSeek(); }
   off_t GetSeekPos() { return seek_pos; }
   virtual void Seek(off_t offs);
   virtual off_t GetRealPos() { return pos; }
   virtual int Buffered() { return Size(); }
   virtual bool IOReady() { return true; }

   virtual void WantDate() { want_date=true; date=NO_DATE_YET; }
   virtual void WantSize() { want_size=true; size=NO_SIZE_YET; }
   time_t GetDate() { return date; }
   off_t GetSize();

   void SetDate(time_t d,int p=0);
   void SetSize(off_t s);
   void SetEntitySize(off_t s) { if(!ascii) e_size=s; }

   void DontCopyDate() { do_set_date=false; }
   void DontVerify() { do_verify=false; }
   bool NeedDate() { return do_set_date; }
   void MakeTargetDir() { do_mkdir=true; }

   void SetRange(const off_t s,const off_t lim);

   FileCopyPeer(dir_t m);
   virtual ~FileCopyPeer() {}

   bool Done();

   void Ascii() { ascii=true; }
   virtual void NoCache() { use_cache=false; }

   virtual const char *GetStatus() { return 0; }
   virtual bool NeedSizeDateBeforehand() { return false; }

   virtual pid_t GetProcGroup() { return 0; }
   virtual void Kill(int sig) {}

   virtual void RemoveFile() { file_removed=true; }
   virtual void NeedSeek() {} // fd is shared, seek before access.

   void CannotSeek(int p)
      {
	 can_seek=false;
	 if(p==0)
	    can_seek0=false;
      }

   // for fxp:
   virtual const FileAccessRef& GetSession() { return FileAccessRef::null; }
   virtual void OpenSession() {}
   virtual void SetFXP(bool) {}

   virtual void Fg() {}
   virtual void Bg() {}

   void AllowWrite(bool y=true) { write_allowed=y; }
   bool WriteAllowed() { return write_allowed; }
   bool WritePending() { return mode==PUT && Size()>0; }

   bool FileRemoved() { return file_removed; }

   void DontStartTransferYet() { start_transfer=false; }
   void StartTransfer() { start_transfer=true; }

   virtual const char *GetURL() { return 0; }
   virtual FileCopyPeer *Clone() { return 0; }
   virtual const Ref<FDStream>& GetLocal() const { return Ref<FDStream>::null; }

   const char *GetSuggestedFileName() { return suggested_filename; }
   void SetSuggestedFileName(const char *f) { if(f) suggested_filename.set(f); }
   void AutoRename(bool yes=true) { auto_rename=yes; }
   bool IsAutoRename() const { return auto_rename; }
   virtual const char *UseTempFile(const char *);
   bool ShouldRename() const;
};

class FileCopy : public SMTask
{
public:
   SMTaskRef<FileCopyPeer> get;
   SMTaskRef<FileCopyPeer> put;

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
   bool cont;

   xstring_c error_text;

   Speedometer rate;
   Speedometer rate_for_eta;
   int put_buf;
   off_t put_eof_pos;

   off_t high_watermark;
   Timer high_watermark_timeout;

   Time start_time;
   Time end_time;

   bool fail_if_cannot_seek;
   bool fail_if_broken;
   bool remove_source_later;
   bool remove_target_first;

   Ref<Buffer> line_buffer;
   int  line_buffer_max;

   bool CheckFileSizeAtEOF() const;

protected:
   void RateAdd(int a);
   void RateReset();
   off_t bytes_count;

public:
   off_t GetPos() const;
   off_t GetSize() const;
   int  GetPercentDone() const;
   const char *GetPercentDoneStr() const;
   float GetRate();
   const char *GetRateStr();
   off_t GetBytesRemaining();
   long GetETA() { return GetETA(GetBytesRemaining()); }
   long GetETA(off_t b);
   const char *GetETAStr();
   const char *GetETAStrSFromTime(time_t t) { return rate_for_eta.GetETAStrSFromTime(t); }
   const char *GetStatus();
   FgData *GetFgData(bool fg);
   pid_t GetProcGroup();
   void Kill(int sig);
   off_t GetBytesCount() { return bytes_count; }
   double GetTimeSpent();
   double GetTransferRate() { return rate.Get(); }

   void SetDate(time_t t,int p=0) { get->SetDate(t,p); }
   void SetDate(const FileTimestamp &t) { SetDate(t.ts,t.ts_prec); }
   void SetSize(off_t s) { get->SetSize(s); }

   bool SetContinue(bool new_cont) { return replace_value(cont,new_cont); }

   bool Done() { return state==ALL_DONE; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }
   void SetError(const char *str);

   void DontCopyDate() { put->DontCopyDate(); }
   void DontVerify() { put->DontVerify(); }
   void Ascii() { get->Ascii(); put->Ascii(); }
   void DontFailIfBroken() { fail_if_broken=false; }
   void FailIfCannotSeek() { fail_if_cannot_seek=true; }
   void SetRange(off_t s,off_t lim);
   void SetRangeLimit(off_t lim) { get->range_limit=lim; }
   off_t GetRangeStart() const { return get->range_start; }
   off_t GetRangeLimit() const { return get->range_limit; }
   void RemoveSourceLater() { remove_source_later=true; }
   void RemoveTargetFirst() { remove_target_first=true; put->Resume(); put->RemoveFile(); }
   void LineBuffered(int size=0x1000);
   bool IsLineBuffered() const { return line_buffer; }

   FileCopy(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
   ~FileCopy();

   int Do();
   void SuspendInternal();
   void ResumeInternal();
   void Fg();
   void Bg();

   static FileCopy *New(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
   static FileCopy *(*fxp_create)(FileCopyPeer *src,FileCopyPeer *dst,bool cont);

   void AllowWrite(bool y=true) { if(put) put->AllowWrite(y); }
   bool WriteAllowed() { return !put || put->WriteAllowed(); }
   bool WritePending() { return put && put->WritePending(); }

   void LogTransfer();
   static Ref<Log> transfer_log;

   static const char *TempFileName(const char *file);
};

class FileVerificator : public SMTask
{
   bool done;
   xstring error_text;
   SMTaskRef<IOBufferFDStream> verify_buffer;
   Ref<InputFilter> verify_process;
   void Init0();
   void InitVerify(const char *f);
public:
   FileVerificator(const char *f);
   FileVerificator(const FDStream *);
   FileVerificator(const FileAccess *,const char *f);
   ~FileVerificator();
   int Do();
   bool Done() { return done; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }
   const char *Status() { return _("Verifying..."); };
};

class FileCopyPeerFA : public FileCopyPeer
{
   xstring_c file;
   xstring orig_url;
   FileAccessRef my_session;
   FileAccessRefC session;
   int FAmode;

   // support the temporary redirects, this info is needed for a fallback
   struct RedirBase {
      FileAccessRef session;
      xstring_c file;
      xstring_c url;
      int FAmode;
      int redirections;
      off_t pos;

      RedirBase() : FAmode(FA::CLOSED), redirections(0), pos(0) {}
      operator bool() const { return session!=0; }
      void save(FileCopyPeerFA *c) {
	 session=c->session->Clone();
	 file.set(c->file);
	 url.set(c->orig_url);
	 FAmode=c->FAmode;
	 pos=c->pos;
	 redirections=c->redirections;
      }
      void revert(FileCopyPeerFA *c) {
	 c->my_session=session.borrow();
	 c->session=c->my_session;
	 c->file.move_here(file);
	 c->orig_url.move_here(url);
	 FAmode=FA::CLOSED;
	 // if there was a progress, reset the redirection count.
	 c->redirections=(c->pos > pos ? 0 : redirections);
      }
   } base;
   friend struct RedirBase;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);
   int PutEOF_LL();

   // to read data in larger quantities, delay the read op
   Timer get_ll_timer;
   int get_delay;

   FileSet info;

   bool fxp;   // FXP (ftp<=>ftp copy) active

   UploadState upload_state;
   int redirections;

   SMTaskRef<FileVerificator> verify;

protected:
   void PrepareToDie();
   ~FileCopyPeerFA();

public:
   void Init();
   FileCopyPeerFA(FileAccess *s,const char *f,int m);
   FileCopyPeerFA(const FileAccessRef& s,const char *f,int m);
   FileCopyPeerFA(const class ParsedURL *u,int m);
   int Do();
   bool IOReady();
   off_t GetRealPos();
   void Seek(off_t new_pos);

   int Buffered() { return Size()+session->Buffered(); }

   void SuspendInternal();
   void ResumeInternal();

   const char *GetStatus();
   const char *GetProto() const { return session->GetProto(); }

   bool NeedSizeDateBeforehand() { return session->NeedSizeDateBeforehand(); }
   void WantSize();
   void RemoveFile();

   static FileCopyPeerFA *New(FA *s,const char *url,int m);
   static FileCopyPeerFA *New(const FileAccessRef& s,const char *url,int m);

   void OpenSession();
   const FileAccessRef& GetSession() { return session; }
   void Fg() { session->SetPriority(1); }
   void Bg() { session->SetPriority(0); }
   void SetFXP(bool on) { fxp=on; }

   const char *GetURL() {
      return orig_url ? orig_url : session->GetFileURL(file);
   }
   FileCopyPeer *Clone();
   const char *UseTempFile(const char *) override;
};

class FileCopyPeerFDStream : public FileCopyPeer
{
   Ref<FDStream> my_stream;
   const Ref<FDStream>& stream;
   off_t seek_base;
   Ref<Timer> put_ll_timer;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);
   void Seek_LL();

   int getfd();

   bool create_fg_data;
   bool need_seek;
   bool close_when_done;

   SMTaskRef<FileVerificator> verify;

public:
   void Init();
   FileCopyPeerFDStream(const Ref<FDStream>& o,dir_t m);
   FileCopyPeerFDStream(FDStream *o,dir_t m);
   int Do();
   bool IOReady();
   void Seek(off_t new_pos);
   FgData *GetFgData(bool fg);
   pid_t GetProcGroup() { return stream->GetProcGroup(); }
   void Kill(int sig);

   void DontCreateFgData() { create_fg_data=false; }
   void NeedSeek() { need_seek=true; }
   void CloseWhenDone() { close_when_done=true; }
   void WantSize();
   void RemoveFile();
   void SetBase(off_t b) { seek_base=b; }

   const char *GetStatus();

   static FileCopyPeerFDStream *NewPut(const char *file,bool cont=false);
   static FileCopyPeerFDStream *NewGet(const char *file);

   const char *GetURL()
      {
	 return stream->full_name;
      }
   const Ref<FDStream>& GetLocal() const { return stream; }
   FileCopyPeer *Clone();
};

class FileCopyPeerDirList : public FileCopyPeer
{
private:
   FileAccessRef session;
   SMTaskRef<DirList> dl;

public:
   FileCopyPeerDirList(FA *s,ArgV *v); // consumes s and v.

   int Do();
   void NoCache() { use_cache=false; if(dl) dl->UseCache(false); }
   void Fg() { session->SetPriority(1); }
   void Bg() { session->SetPriority(0); }
   const char *GetStatus() { return session->CurrentStatus(); }
   void UseColor(bool c=true) { if(dl) dl->UseColor(c); }
};

class FileCopyPeerMemory : public FileCopyPeer
{
private:
   int max_size;

public:
   FileCopyPeerMemory(int m) : FileCopyPeer(PUT), max_size(m) {}
   FileCopyPeerMemory(const xstring& s) : FileCopyPeer(GET), max_size(0) {
      Put(s);
      PutEOF();
      size=s.length();
      pos=0;
   }
   int Do();
   bool Done() { return true; }
};

#endif
