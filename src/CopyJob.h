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
   char *name;
   bool no_status;

public:
   CopyJob(FileCopy *c1,const char *n);
   ~CopyJob();

   void NoStatus() { no_status=true; }

   int Do();
   int Done();
   int ExitCode();

   void Suspend() { c->Suspend(); Job::Suspend(); }
   void Resume() { Job::Resume(); c->Resume(); }

   int AcceptSig(int sig);
   pid_t GetProcGroup() { return c?c->GetProcGroup():0; }

   bool Error() { return c->Error(); }
   const char *ErrorText() { return c->ErrorText(); }
   long GetTimeSpent() { return c->GetTimeSpent(); }
   int  GetTimeSpentMilli() { return c->GetTimeSpentMilli(); }
   long GetBytesCount() { return c->GetBytesCount(); }
   long GetSize() { return c->GetSize(); }
   long GetPos()  { return c->GetPos(); }
   float GetRate() { return c->GetRate(); }
   long GetETA() { return c->GetETA(); }
   void SetRange(long s,long lim) { c->SetRange(s,lim); }
   void SetDate(time_t d) { c->SetDate(d); }
   void SetSize(long s)   { c->SetSize(s); }
   FileCopyPeer *GetPut() { return c->put; }

   void ShowRunStatus(StatusLine *s);
   void	PrintStatus(int);

   const char *GetName() { return name; }
   const char *SqueezeName(int w);

   static CopyJob *NewGet(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewPut(FileAccess *f,const char *src,const char *dst);
   static CopyJob *NewEcho(const char *str,int len,FDStream *o);
   static CopyJob *NewEcho(const char *str,FDStream *o)
      { return NewEcho(str,strlen(str),o); }
};

class ArgV;
class CopyJobEnv : public SessionJob
{
protected:
   CopyJob *cp;
   bool done;
   int errors;
   int count;
   long bytes;
   float time_spent;
   const char *op;
   bool no_status;
   char *cwd;
   bool cont;
   bool ascii;
   ArgV *args;

   virtual void NextFile() = 0;

   void SetCopier(FileCopy *c,const char *n);

public:
   int Do();
   int Done();
   int ExitCode() { return errors!=0; }

   int AcceptSig(int sig);

   CopyJobEnv(FileAccess *s,ArgV *a,bool c=false);
   ~CopyJobEnv();

   void SayFinal();
   void	PrintStatus(int);

   void Ascii() { ascii=true; }
};

#endif // COPYJOB_H
