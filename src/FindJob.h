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

class FinderJob : public SessionJob
{
   const char *dir;
   int errors;
   ListInfo *li;

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

   bool depth_done;

protected:
   enum state_t { INIT, CD, INFO, LOOP, WAIT, DONE };
   state_t state;

   const char *op;
   char *start_dir;
   char *init_dir;

   enum prf_res { PRF_FATAL, PRF_ERR, PRF_OK, PRF_WAIT, PRF_LATER };
   virtual prf_res ProcessFile(const char *d,const FileInfo *fi);
   virtual void Finish() {};

   bool show_sl;

   bool depth_first;
   bool use_cache;
   bool quiet;

   void NextDir(const char *d);

public:
   int Do();
   int Done() { return state==DONE; }
   int ExitCode() { return state!=DONE || errors; }

   void Init();
   FinderJob(FileAccess *s,const char *d);
   ~FinderJob();

   void ShowRunStatus(StatusLine *sl);
   void PrintStatus(int v);

   void BeQuiet() { quiet=true; }
};

class FinderJob_List : public FinderJob
{
   Buffer *buf;
protected:
   prf_res ProcessFile(const char *d,const FileInfo *fi);
   void Finish() { buf->PutEOF(); }

public:
   FinderJob_List(FileAccess *s,const char *d,FDStream *o);
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
