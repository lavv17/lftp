/*
 * lftp - file transfer program
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

#ifndef FILECOPY_H
#define FILECOPY_H

#include "SMTask.h"
#include "buffer.h"
#include "FileAccess.h"

#define NO_SIZE	     (-1L)
#define NO_SIZE_YET  (-2L)
#define NO_DATE	     ((time_t)-1L)
#define NO_DATE_YET  ((time_t)-2L)
#define FILE_END     (-1L)

class FileCopyPeer : public Buffer
{
protected:
   bool want_size;
   bool want_date;
   long size;
   time_t date;

   long seek_pos;
   bool can_seek;
   bool date_set;

   bool ascii;

public:
   enum direction { GET, PUT };

protected:
   enum direction mode;

public:
   bool CanSeek() { return can_seek; }
   long GetSeekPos() { return seek_pos; }
   virtual void Seek(long offs) { seek_pos=offs; Empty(); eof=false; broken=false; }
   virtual long GetRealPos() { return pos; }
   virtual long Buffered() { return in_buffer; }
   virtual bool IOReady() { return true; }

   virtual void WantDate() { want_date=true; date=NO_DATE_YET; }
   virtual void WantSize() { want_size=true; size=NO_SIZE_YET; }
   time_t GetDate() { return date; }
   long   GetSize() { return size; }

   void SetDate(time_t d);
   void SetSize(long s);

   FileCopyPeer(direction m);
   ~FileCopyPeer();

   bool Done();

   void Ascii() { ascii=true; }

   virtual const char *GetStatus() { return 0; }
};

class Speedometer : public SMTask
{
   int period;
   float rate;
   time_t last_second;
   time_t last_bytes;
   time_t start;
public:
   Speedometer(int p);
   float Get();
   bool Valid();
   void Add(int bytes);
   int Do();
};

class FileCopy : public SMTask
{
   FileCopyPeer *get;
   FileCopyPeer *put;

   enum state_t
      {
	 INITIAL,
	 PUT_WAIT,
	 DO_COPY,
	 CONFIRM_WAIT,
	 GET_DONE_WAIT,
	 ALL_DONE
      } state;

   int max_buf;
   bool cont;

   char *error_text;

   Speedometer *rate;
   Speedometer *rate_for_eta;
   int put_buf;

public:
   long GetPos();
   int  GetPercentDone();
   const char *GetPercentDoneStr();
   float GetRate();
   const char *GetRateStr();
   long GetETA();
   const char *GetETAStr();
   const char *GetStatus();
   FgData *GetFgData(bool fg);

   bool Done() { return state==ALL_DONE; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }
   void SetError(const char *str);

   FileCopy(FileCopyPeer *src,FileCopyPeer *dst,bool cont);
   ~FileCopy();
   void Init();

   int Do();
};

class FileCopyPeerFA : public FileCopyPeer
{
   char *file;
   int FAmode;
   FileAccess *session;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

   FileAccess::fileinfo info;

   bool reuse_later;

public:
   FileCopyPeerFA(FileAccess *s,const char *f,int m);
   FileCopyPeerFA(class ParsedURL *u,int m);
   ~FileCopyPeerFA();
   int Do();
   bool IOReady();
   long GetRealPos();
   void Seek(long new_pos);

   long Buffered() { return in_buffer+session->Buffered(); }

   void Suspend();
   void Resume();

   void ReuseSessionLater();

   const char *GetStatus() { return session->CurrentStatus(); }
};

class FileCopyPeerFDStream : public FileCopyPeer
{
   FDStream *stream;
   long seek_base;

   int Get_LL(int size);
   int Put_LL(const char *buf,int size);

   int getfd();

public:
   FileCopyPeerFDStream(FDStream *o,direction m);
   ~FileCopyPeerFDStream();
   int Do();
   bool IOReady();
   void Seek(long new_pos);
   FgData *GetFgData(bool fg);
};

class FileCopyPeerString : public FileCopyPeer
{
public:
   FileCopyPeerString(const char *s,int len=-1);
   ~FileCopyPeerString();
   void Seek(long new_pos);
};

#endif
