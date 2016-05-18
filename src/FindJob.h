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

#ifndef FINDJOB_H
#define FINDJOB_H

#include "Job.h"
#include "buffer.h"
#include "ArgV.h"
#include "GetFileInfo.h"
#include "PatternSet.h"

class FinderJob : public SessionJob
{
   FileAccessRef my_session;
   FileAccess::Path orig_init_dir;

   xstring_c dir;
   int errors;
   SMTaskRef<GetFileInfo> li;

   class place
      {
	 friend class FinderJob;

	 xstring_c path;
	 Ref<FileSet> fset;

	 place(const char *p,FileSet *f) : path(p), fset(f) {}
      };

   RefArray<place> stack;

   void Up();
   void Down(const char *d);
   void Push(FileSet *f);

   virtual void Enter(const char *d) { }
   virtual void Exit() { }

   bool depth_done;
   unsigned file_info_need;
   /* In certain circumstances, we can skip a LIST altogether and just
    * pass argument names on: we don't need anything other than the name
    * (no other file_info_needs) and we're not recursing (which would imply
    * needing the type.)  This means arguments that don't actually exist
    * get passed on; if this is inappropriate (ie for a simple Find),
    * call ValidateArgs(). */
   bool validate_args;

   Ref<PatternSet> exclude;

protected:
   enum state_t { START_INFO, INFO, LOOP, PROCESSING, WAIT, DONE };
   state_t state;

   const char *op;
   FileAccessRefC session;
   FileAccess::Path init_dir;

   enum prf_res { PRF_FATAL, PRF_ERR, PRF_OK, PRF_WAIT, PRF_LATER };
   virtual prf_res ProcessFile(const char *d,const FileInfo *fi);
   virtual void ProcessList(FileSet *f) { }
   virtual void Finish() {};

   bool show_sl;

   bool depth_first;
   bool use_cache;
   bool quiet;
   int maxdepth;

   void NextDir(const char *d);
   const char *GetCur() const { return dir; }
   void Need(unsigned need) { file_info_need=need; }
   void ValidateArgs() { validate_args=true; }

   bool ProcessingURL() { return session!=SessionJob::session; }

public:
   int Do();
   int Done() { return state==DONE; }
   int ExitCode() { return state!=DONE || (errors && !quiet); }

   void Init();
   FinderJob(FileAccess *s);
   void PrepareToDie() { session->Close(); SessionJob::PrepareToDie(); }
   ~FinderJob();

   void ShowRunStatus(const SMTaskRef<StatusLine>&);
   xstring& FormatStatus(xstring&,int,const char *);

   void BeQuiet() { quiet=true; }
   void SetExclude(PatternSet *p) { exclude = p; }
   void set_maxdepth(int _maxdepth) { maxdepth = _maxdepth; }

   void Fg();
   void Bg();
};

class FinderJob_List : public FinderJob
{
   SMTaskRef<IOBuffer> buf;
   Ref<ArgV> args;
   bool long_listing;
protected:
   prf_res ProcessFile(const char *d,const FileInfo *fi);
   void Finish();

public:
   FinderJob_List(FileAccess *s,ArgV *a,FDStream *o);
   void DoLongListing(bool yes=true) { long_listing=yes; }

   int Done() { return FinderJob::Done() && buf->Done(); }
};

#endif //FINDJOB_H
