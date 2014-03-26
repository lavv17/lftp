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

#ifndef JOB_H
#define JOB_H

#include <stdarg.h>
#include "trio.h"
#undef printf

#include "SMTask.h"
#include "StatusLine.h"
#include "fg.h"
#include "FileAccess.h"

#define JobRef SMTaskRef // it is basically the same

class Job : public SMTask
{
   static void SortJobs();

   xlist<Job> all_jobs_node;
   static xlist_head<Job> all_jobs;

   xlist_head<Job> children_jobs;
   xlist<Job> children_jobs_node;

protected:
   bool fg;
   Ref<FgData> fg_data;

   void PrepareToDie();
   virtual ~Job();

public:
   int	 jobno;
   Job	 *parent;

   xarray<Job*> waiting;

   void AddWaiting(Job *);
   template<class T> void AddWaiting(const JobRef<T>& r) { AddWaiting(r.get_non_const()); }
   void RemoveWaiting(const Job *);
   void ReplaceWaiting(Job *from,Job *to);

   void SetParent(Job *j);
   void SetParentFg(Job *j, bool f=true)
      {
	 SetParent(j);
	 if(f && j->fg)
	    Fg();
//	 else if(f && !j->fg)
//	    Bg();
      }

   void	 AllocJobno();

   void PrintStatus(int,const char *prefix="\t");
   virtual xstring& FormatStatus(xstring& s,int v,const char *prefix="\t") { return s; }
   virtual xstring& FormatShortStatus(xstring& s);
   virtual void	ShowRunStatus(const SMTaskRef<StatusLine>&);
   void ClearStatus()
      {
	 eprintf("%s",""); /* just "" causes a -Wformat-zero-length" warning */
      }
   virtual void	  SayFinal() {}; // final phrase of fg job
   virtual int	  Done()=0;
   virtual int	  ExitCode() { return 0; }
   virtual int	  Do()=0;
   virtual int	  AcceptSig(int);
   virtual void	  Bg();
   virtual void	  Fg();

   xstring cmdline;

   virtual const xstring& GetCmdLine() { return cmdline?cmdline:xstring::get_tmp("?",1); }
   xstring& FormatJobTitle(xstring& s,int indent=0,const char *suffix=0);
   xstring& FormatOneJob(xstring& s,int verbose,int indent=0,const char *suffix=0);
   xstring& FormatOneJobRecursively(xstring& s,int verbose,int indent=0);
   virtual xstring& FormatJobs(xstring& s,int verbose,int indent=0);

   void PrintJobTitle(int indent=0,const char *suffix=0);
   void ListOneJob(int verbose,int indent=0,const char *suffix=0);
   void ListOneJobRecursively(int verbose,int indent);

   void ListDoneJobs();
   void BuryDoneJobs();

   Job *FindAnyChild();
   bool WaitsFor(Job *);
   static Job *FindWhoWaitsFor(Job *);
   bool CheckForWaitLoop(Job *parent);
   int NumAwaitedJobs() { return waiting.count(); }
   Job *FindDoneAwaitedJob();
   void WaitForAllChildren();
   void AllWaitingFg();

   static int NumberOfJobs();
   int NumberOfChildrenJobs();
   static Job *FindJob(int n);
   static bool Running(int n)
   {
      Job *j=FindJob(n);
      return j && !j->Done();
   }
   void Kill(int n);
   static void Kill(Job*);
   void SendSig(int n,int sig);
   static void KillAll();
   static void Cleanup();

   void vfprintf(FILE *file,const char *fmt,va_list v);
   // CmdExec redefines this, and traps all messages of its children.
   virtual void top_vfprintf(FILE *file,const char *fmt,va_list v);

   // C-like functions calling vfprintf
   void eprintf(const char *fmt,...) PRINTF_LIKE(2,3);
   void fprintf(FILE *file,const char *fmt,...) PRINTF_LIKE(3,4);
   void printf(const char *fmt,...) PRINTF_LIKE(2,3);
   void perror(const char *);
   void puts(const char *);

   Job();

   virtual const char *GetConnectURL() { return 0; }
   virtual void lftpMovesToBackground() { Resume(); }
   static  void lftpMovesToBackground_ToAll();

   virtual off_t GetBytesCount();
   virtual double GetTimeSpent();
   virtual double GetTransferRate();

   void WaitDone();
};

class SessionJob : public Job
{
protected:
   SessionJob(FileAccess *s) : session(s) {}
   FileAccess *Clone() const { return session->Clone(); }

public:
   FileAccessRef session;

   xstring& FormatStatus(xstring&,int,const char *);
   const char *GetConnectURL()
      {
	 if(!session)
	    return 0;
	 return session->GetConnectURL();
      }
   void Fg();
   void Bg();
};


#endif /* JOB_H */
