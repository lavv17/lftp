/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FINDJOB_H
#define FINDJOB_H

#include "Job.h"
#include "buffer.h"
#include "ArgV.h"
#include "GetFileInfo.h"

class FinderJob : public SessionJob
{
   FileAccess *orig_session;
   char *orig_init_dir;

   char *dir;
   int errors;
   GetFileInfo *li;

   class place
      {
	 friend class FinderJob;

	 char *path;
	 FileSet *fset;

	 place(const char *p,FileSet *f); // eat f, dup p
	 ~place();
      };

   place **stack;
   int stack_ptr;
   int stack_allocated;

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

   char *exclude;

protected:
   enum state_t { START_INFO, INFO, LOOP, WAIT, DONE };
   state_t state;

   const char *op;
   char *init_dir;

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

public:
   int Do();
   int Done() { return state==DONE; }
   int ExitCode() { return state!=DONE || errors; }

   void Init();
   FinderJob(FileAccess *s);
   virtual ~FinderJob();

   void ShowRunStatus(StatusLine *sl);
   virtual void PrintStatus(int v);

   void BeQuiet() { quiet=true; }
   void SetExclude(const char *excl) { xfree(exclude); exclude = xstrdup(excl); }
   void set_maxdepth(int _maxdepth) { maxdepth = _maxdepth; }
};

class FinderJob_List : public FinderJob
{
   Buffer *buf;
   ArgV *args;
protected:
   prf_res ProcessFile(const char *d,const FileInfo *fi);
   void Finish();

public:
   FinderJob_List(FileAccess *s,ArgV *a,FDStream *o);
   ~FinderJob_List();

   int Done() { return FinderJob::Done() && buf->Done(); }
};

class FinderJob_Cmd : public FinderJob
{
public:
   enum cmd_t { GET, RM };
   FinderJob_Cmd(FileAccess *s,ArgV *a,cmd_t c);
   ~FinderJob_Cmd();
   int Done();
protected:
   cmd_t cmd;
   ArgV *args;
   char *saved_cwd;
   bool removing_last;

   prf_res ProcessFile(const char *d,const FileInfo *fi);
   void Finish();
};

#endif //FINDJOB_H
