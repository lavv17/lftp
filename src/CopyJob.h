/*
 * lftp and utils
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

#ifndef COPYJOB_H
#define COPYJOB_H

#include "Job.h"
#include "StatusLine.h"
#include "FileCopy.h"

class CopyJob : public Job
{
   FileCopy *c;
   bool done;
   char *name; // file name
   char *dispname; // displayed file name
   char *op;   // command name
   bool no_status;
   bool no_status_on_write;
   bool clear_status_on_write;

   void SetDispName();

public:
   CopyJob(FileCopy *c1,const char *n,const char *op1);
   ~CopyJob();

   void NoStatus() { no_status=true; }
   void NoStatusOnWrite() { no_status_on_write=true; }
   void ClearStatusOnWrite() { clear_status_on_write=true; }
   bool HasStatus() const { return !no_status; }

   int Do();
   int Done();
   int ExitCode();

   void SuspendInternal() { c->SuspendSlave(); }
   void ResumeInternal()  { c->ResumeSlave(); }
   void Fg() { c->Fg(); Job::Fg(); }
   void Bg() { Job::Bg(); c->Bg(); }

   int AcceptSig(int sig);
   pid_t GetProcGroup() { return c?c->GetProcGroup():0; }

   bool Error() { return c->Error(); }
   const char *ErrorText() { return c->ErrorText(); }
   double GetTimeSpent() { return c->GetTimeSpent(); }
   off_t GetBytesCount() { return c->GetBytesCount(); }
   off_t GetSize() { return c->GetSize(); }
   off_t GetPos()  { return c->GetPos(); }
   float GetRate() { return c->GetRate(); }
   long GetETA() { return c->GetETA(); }
   long GetETA(off_t rem) { return c->GetETA(rem); }
   const char *GetETAStrSFromTime(time_t t) { return c->GetETAStrSFromTime(t); }
   void SetRange(off_t s,off_t lim) { c->SetRange(s,lim); }
   void SetDate(time_t d) { c->SetDate(d); }
   void SetSize(off_t s)   { c->SetSize(s); }
   FileCopy *GetCopy() { return c; }
   FileCopyPeer *GetPut() { return c->put; }
   FileCopyPeer *GetGet() { return c->get; }

   const char *Status(const StatusLine *s,bool base=false);
   void ShowRunStatus(StatusLine *s);
   void	PrintStatus(int,const char *);

   const char *GetName() const { return name; }
   const char *GetDispName() const { return dispname; }
   const char *SqueezeName(int w, bool base=false);

   static CopyJob *NewGet(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewPut(FileAccess *f,const char *src,const char *dst);

   static const char *FormatBytesTimeRate(off_t bytes,double time);
};

class ArgV;
class CopyJobEnv : public SessionJob
{
protected:
   CopyJob *cp;
   bool done;
   int errors;
   int count;
   off_t bytes;
   double time_spent;
   const char *op;
   bool no_status;
   char *cwd;
   bool cont;
   bool ascii;
   ArgV *args;

   virtual void NextFile() = 0;

   void SetCopier(FileCopy *c,const char *n);
   void AddCopier(FileCopy *c,const char *n);

public:
   int Do();
   int Done();
   virtual int ExitCode() { return errors!=0; }

   int AcceptSig(int sig);

   CopyJobEnv(FileAccess *s,ArgV *a,bool c=false);
   ~CopyJobEnv();

   void SayFinalWithPrefix(const char *p);
   void SayFinal() { SayFinalWithPrefix(""); }
   void	PrintStatus(int,const char *);

   void Ascii() { ascii=true; }

   double GetTimeSpent() { return time_spent; }
   off_t GetBytesCount() { return bytes; }
};

#endif // COPYJOB_H
