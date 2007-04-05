/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILTER_H
#define FILTER_H

#include "ProcWait.h"

class ArgV;
class FileTimestamp;

class FDStream
{
   bool close_fd;

public:
   int fd;
   char *name;
   char *full_name;
   char *cwd;
   char *error_text;
   const char *status;

   bool	 error() { return error_text!=0; }

   virtual int getfd() { return fd; }

   FDStream();
   FDStream(int new_fd,const char *new_name);
   virtual ~FDStream();

   void MakeErrorText(int e=0);
   bool NonFatalError(int err);
   void set_status(const char *str) { status=str; }
   void clear_status() { status=0; }
   void CloseFD() { close_fd=true; }

   void SetCwd(const char *);
   const char *GetCwd() const { return cwd; }

   virtual off_t get_size() { return -1; }
   virtual void setmtime(const FileTimestamp &) {}
   virtual bool can_setmtime() { return false; }
   virtual void remove_if_empty() {}
   virtual void remove() {}
   virtual bool Done() { return true; }
   virtual bool usesfd(int n_fd) { return fd==n_fd; }
   virtual void Kill(int=SIGTERM) {}
   virtual pid_t GetProcGroup() const { return 0; }
   virtual bool broken() { return false; }
   virtual bool can_seek() { return false; }
};

class OutputFilter : public FDStream
{
   ArgV *a;
   ProcWait *w;
   pid_t pg;
   FDStream *second;
   bool delete_second;
   bool stderr_to_stdout;
   bool stdout_to_null;

   bool closed;

   void Init();

   virtual void Parent(int *p);	 // what to do with pipe if parent
   virtual void Child (int *p);	 // same for child

protected:
   int second_fd;

public:
   OutputFilter(const char *filter,int second_fd=-1);
   OutputFilter(const char *filter,FDStream *second);
   OutputFilter(ArgV *a,int second_fd=-1);
   OutputFilter(ArgV *a,FDStream *second);
   virtual ~OutputFilter();

   void StderrToStdout() { stderr_to_stdout=true; }
   void StdoutToNull() { stdout_to_null=true; }

   int getfd();
   bool Done();

   bool usesfd(int n_fd);
   void Kill(int sig=SIGTERM);
   pid_t GetProcGroup() const { return pg; }
   void SetProcGroup(pid_t new_pg) { pg=new_pg; }
   ProcWait::State GetProcState() { return w->GetState(); }
   int GetProcExitCode() { return w->GetInfo()>>8; }

   bool broken();

   void DeleteSecondaryStream() { delete_second=true; }
};

class InputFilter : public OutputFilter
{
   virtual void Parent(int *p);
   virtual void Child (int *p);
public:
   InputFilter(const char *filter,int second_fd=-1)
      : OutputFilter(filter,second_fd) {}
   InputFilter(const char *filter,FDStream *second)
      : OutputFilter(filter,second) {}
   InputFilter(ArgV *a,int second_fd=-1)
      : OutputFilter(a,second_fd) {}
   InputFilter(ArgV *a,FDStream *second)
      : OutputFilter(a,second) {}
};

class FileStream : public FDStream
{
   int mode;
public:
   FileStream(const char *fname,int open_mode);
   ~FileStream();

   void setmtime(const FileTimestamp &);
   bool can_setmtime() { return true; }
   void remove_if_empty();
   void remove();
   int getfd();
   bool can_seek();
   off_t get_size();
};

#endif /* FILTER_H */
