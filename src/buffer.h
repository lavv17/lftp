/*
 * lftp and utils
 *
 * Copyright (c) 1998-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef BUFFER_H
#define BUFFER_H

#include "SMTask.h"
#include "Filter.h"
#include "fg.h"
#include "xstring.h"

#include <stdarg.h>

#define GET_BUFSIZE 0x10000

class Speedometer;

class Buffer : public SMTask
{
protected:
   char *error_text;
   int  saved_errno;
   bool error_fatal;

   char *buffer;
   int buffer_allocated;
   int in_buffer;
   int buffer_ptr;
   bool eof;	  // no reads possible (except from mem buffer)
   bool broken;	  // no writes possible

   bool save;  // save skipped data
   int save_max;

   off_t pos;

   // low-level for derived classes
   virtual int Put_LL(const char *buf,int size) { return 0; }
   virtual int PutEOF_LL() { return 0; }

   Speedometer *rate;
   void RateAdd(int n);

   void Allocate(int size);

   void SaveMaxCheck(int addsize);

   virtual ~Buffer();

public:
   virtual int Do();
   virtual bool Done() { return in_buffer==0; }
   bool Error() { return error_text!=0; }
   bool ErrorFatal() { return error_fatal; }
   void SetError(const char *e,bool fatal=false);
   void SetError2(const char *e1,const char *e2,bool fatal=false);
   const char *ErrorText() { return error_text; }
   int Size() { return in_buffer; }
   bool Eof() { return eof; }
   bool Broken() { return broken; }

   void Get(const char **buf,int *size);
   void Skip(int len); // Get(); consume; Skip()
   void UnSkip(int len); // this only works if there were no Put's.
   void Put(const char *buf,int size);
   void Put(const char *buf) { Put(buf,strlen(buf)); }
   void Format(const char *f,...) PRINTF_LIKE(2,3);
   void vFormat(const char *f, va_list v);
   void PutEOF() { eof=true; PutEOF_LL(); }

   // useful for cache.
   void Save(int m) { save=true; save_max=m; }
   bool IsSaving() const { return save; }
   void GetSaved(const char **buf,int *size) const;
   void SaveRollback(int p);

   void SetPos(off_t p) { pos=p; }
   off_t GetPos() const { return pos; }

   void SetSpeedometer(Speedometer *s) { rate=s; }
   const char *GetRateStrS();

   void Empty();

   virtual FgData *GetFgData(bool) { return 0; }

   virtual time_t EventTime() { return now; }

   Buffer();
};

class IOBuffer : public Buffer
{
public:
   enum dir_t { GET, PUT };

protected:
   time_t event_time; // used to detect timeouts
   dir_t mode;

public:
   IOBuffer(dir_t m)
      {
	 event_time=now;
	 mode=m;
      }
   time_t EventTime()
      {
	 if(suspended)
	    return now;
	 return event_time;
      }
   bool Done()
      {
	 return(broken || Error() || (eof && (mode==GET || in_buffer==0)));
      }
};

class IOBufferFDStream : public IOBuffer
{
   FDStream *stream;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

protected:
   ~IOBufferFDStream() { delete stream; }

public:
   IOBufferFDStream(FDStream *o,dir_t m) : IOBuffer(m) { stream=o; }
   int Do();
   bool Done();
   FgData *GetFgData(bool fg);
};

class FileAccess;

class IOBufferFileAccess : public IOBuffer
{
   FileAccess *session;

   int Get_LL(int size);

protected:
   ~IOBufferFileAccess();

public:
   IOBufferFileAccess(FileAccess *i) : IOBuffer(GET) { session=i; }
   int Do();

   void Suspend();
   void Resume();
};

#endif // BUFFER_H
