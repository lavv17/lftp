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

class FDStream
{
public:
   int fd;
   char *name;
   char *error_text;

   bool	 error() { return error_text!=0; }

   virtual int getfd() { return fd; }

   FDStream();
   FDStream(int new_fd,const char *new_name);
   virtual ~FDStream();

   void MakeErrorText();

   virtual long getsize_and_seek_end();
   virtual void setmtime(time_t) {}
   virtual bool can_setmtime() { return false; }
   virtual void remove_if_empty() {}
   virtual bool Done() { return true; }
   virtual bool usesfd(int n_fd) { return fd==n_fd; }
   virtual void Kill(int=SIGTERM) {}
   virtual pid_t GetProcGroup() { return 0; }
   virtual bool broken() { return false; }
   virtual bool can_seek() { return false; }
};

class OutputFilter : public FDStream
{
   ProcWait *w;
   pid_t pg;
   FDStream *second;

   char *oldcwd;

   bool closed;

   void Init();

   virtual void Parent(int *p);	 // what to do with pipe if parent
   virtual void Child (int *p);	 // same for child

protected:
   int second_fd;

public:
   OutputFilter(const char *filter,int second_fd=-1);
   OutputFilter(const char *filter,FDStream *second);
   virtual ~OutputFilter();

   void SetCwd(const char *);

   long getsize_and_seek_end() { return 0; }

   int getfd();
   bool Done();

   bool usesfd(int n_fd) { return FDStream::usesfd(n_fd) || n_fd<=2; }
   void Kill(int sig=SIGTERM) { if(w) w->Kill(sig); }
   pid_t GetProcGroup() { return pg; }

   bool broken();
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
};

class FileStream : public FDStream
{
   int mode;
public:
   char *full_name;
   FileStream(const char *fname,int open_mode);
   ~FileStream();

   void setmtime(time_t t);
   bool can_setmtime() { return true; }
   void remove_if_empty();
   int getfd();
   bool can_seek();
};

#endif /* FILTER_H */
