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

#ifndef BUFFER_H
#define BUFFER_H

#include "SMTask.h"
#include "Filter.h"
#include "fg.h"
#include "xstring.h"

class Buffer : public SMTask
{
protected:
   char *error_text;
   int  saved_errno;

   char *buffer;
   int buffer_allocated;
   int in_buffer;
   int buffer_ptr;
   bool eof;	  // no reads possible (except from mem buffer)
   bool broken;	  // no writes possible

   // low-level for derived classes
   virtual int Put_LL(const char *buf,int size) { return 0; }
   virtual int PutEOF_LL() { return 0; }

   void Allocate(int size);

public:
   virtual int Do();
   virtual bool Done() { return in_buffer==0; }
   bool Error() { return error_text!=0; }
   int  Errno() { return saved_errno; }
   const char *ErrorText() { return error_text; }
   int Size() { return in_buffer; }
   bool Eof() { return eof; }
   bool Broken() { return broken; }

   void Get(const char **buf,int *size);
   void Skip(int len); // Get(); consume; Skip()
   void Put(const char *buf,int size);
   void Put(const char *buf) { Put(buf,strlen(buf)); }
   void PutEOF() { eof=true; PutEOF_LL(); }

   virtual FgData *GetFgData(bool) { return 0; }

   virtual time_t EventTime() { return now; }

   Buffer();
   virtual ~Buffer();
};

class FileOutputBuffer : public Buffer
{
   FDStream *out;

   int Put_LL(const char *buf,int size);

   time_t event_time; // used to detect timeouts

public:
   FileOutputBuffer(FDStream *o);
   ~FileOutputBuffer();
   int Do();
   bool Done();
   FgData *GetFgData(bool fg);

   time_t EventTime();
};

class FileAccess;

class FileInputBuffer : public Buffer
{
   FDStream *in;
   FileAccess *in_FA;

   int Get_LL(int size);

   time_t event_time; // used to detect timeouts

public:
   FileInputBuffer(FDStream *i);
   FileInputBuffer(FileAccess *i);
   ~FileInputBuffer();
   int Do();
   bool Done();
   FgData *GetFgData(bool fg);

   time_t EventTime();

   void Suspend();
   void Resume();
};

#endif // BUFFER_H
