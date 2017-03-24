/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef COPYJOB_H
#define COPYJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "FileCopy.h"

class CopyJob : public Job
{
protected:
   SMTaskRef<FileCopy> c;
   bool done;
   xstring_c name; // file name
   xstring_c dispname; // displayed file name
   xstring_c op;   // command name
   bool no_status;
   bool no_status_on_write;
   bool clear_status_on_write;
   bool quiet;

   void SetDispName();

   void PrepareToDie();

public:
   CopyJob(FileCopy *c1,const char *n,const char *op1);
   ~CopyJob();

   void NoStatus(bool t=true) { no_status=t; }
   void NoStatusOnWrite() { no_status_on_write=true; }
   void ClearStatusOnWrite() { clear_status_on_write=true; }
   bool HasStatus() const { return !no_status; }
   void Quiet(bool q) { quiet=q; }

   int Do();
   int Done() { return done; }
   int ExitCode();

   void SuspendInternal() { c->SuspendSlave(); }
   void ResumeInternal()  { c->ResumeSlave(); }
   void Fg() { if(c) c->Fg(); Job::Fg(); }
   void Bg() { Job::Bg(); if(c) c->Bg(); }

   int AcceptSig(int sig);
   pid_t GetProcGroup() { return c?c->GetProcGroup():0; }

   bool Error() { return c->Error(); }
   const char *ErrorText() { return c->ErrorText(); }
   double GetTimeSpent() { return c->GetTimeSpent(); }
   off_t GetBytesCount() { return c->GetBytesCount(); }
   double GetTransferRate() { return c->GetTransferRate(); }
   off_t GetSize() { return c->GetSize(); }
   off_t GetPos()  { return c->GetPos(); }
   float GetRate() { return c->GetRate(); }
   long GetETA() { return c->GetETA(); }
   long GetETA(off_t rem) { return c->GetETA(rem); }
   const char *GetETAStrSFromTime(time_t t) { return c->GetETAStrSFromTime(t); }
   void SetRange(off_t s,off_t lim) { c->SetRange(s,lim); }
   void SetRangeLimit(off_t lim) { return c->SetRangeLimit(lim); }
   void SetDate(const FileTimestamp& d) { c->SetDate(d); }
   void SetSize(off_t s)   { c->SetSize(s); }
   const SMTaskRef<FileCopy>& GetCopy() const { return c; }
   const SMTaskRef<FileCopyPeer>& GetPut() const { return c->put; }
   const SMTaskRef<FileCopyPeer>& GetGet() const { return c->get; }

   const Ref<FDStream>& GetLocal() const { return GetPut()->GetLocal(); }

   const char *Status(const StatusLine *s,bool base=false);
   void ShowRunStatus(const SMTaskRef<StatusLine>&);
   xstring& FormatStatus(xstring&,int,const char *);

   const char *GetName() const { return name; }
   const char *GetDispName() const { return dispname; }
   const char *SqueezeName(int w, bool base=false);

   static CopyJob *NewGet(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewPut(FileAccess *f,const char *src,const char *dst);

   static const char *FormatBytesTimeRate(off_t bytes,double time);
};
class CopyJobCreator
{
public:
   virtual CopyJob *New(FileCopy *c,const char *n,const char *o) const = 0;
   virtual ~CopyJobCreator() {}
};

class ArgV;
class CopyJobEnv : public SessionJob
{
protected:
   CopyJob *cp;
   bool done;
   int errors;
   int count;
   int parallel;
   off_t bytes;
   TimeDate transfer_start_ts;
   double time_spent;
   const char *op;
   bool no_status;
   xstring_c cwd;
   bool cont;
   bool ascii;
   bool quiet;
   Ref<ArgV> args;

   virtual void NextFile() = 0;

   void SetCopier(FileCopy *c,const char *n);
   void AddCopier(FileCopy *c,const char *n);

   Ref<CopyJobCreator> cj_new;

public:
   int Do();
   int Done();
   virtual int ExitCode() { return errors!=0; }

   int AcceptSig(int sig);

   CopyJobEnv(FileAccess *s,ArgV *a,bool c=false);
   ~CopyJobEnv();

   void SetCopyJobCreator(CopyJobCreator *c) { cj_new=c; }

   void SayFinal();
   xstring& FormatFinalWithPrefix(xstring&,const char *);
   xstring& FormatStatus(xstring&,int,const char *);

   void Ascii() { ascii=true; }

   double GetTimeSpent() { return time_spent+(waiting.count()>0?now-transfer_start_ts:0.); }
   off_t GetBytesCount() { return bytes+Job::GetBytesCount(); }

   void Quiet(bool q) { quiet=q; }

   void SetParallel(int n) { parallel=n; }
};

#endif // COPYJOB_H
