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

#ifndef XFERJOB_H
#define XFERJOB_H

#include "Job.h"
#include "ArgV.h"
#include "xmalloc.h"
#include "Filter.h"
#include "url.h"

#define percent(offset,size) ((offset)>(size)?100: \
				 int(float(offset)*100/(size)))

class XferJob : public SessionJob
{
protected:
   char	 *op;

   char	 *curr;

   size_t buffer_size;
   char  *buffer;
   size_t in_buffer;

   int	 file_count;
   int	 failed;

   time_t start_time;
   time_t end_time;
   int	 start_time_ms;
   int	 end_time_ms;

   long	 offset;
   long	 size;

   long	 Offset();   // uses session->Buffered()

   float minute_xfer_rate;
   time_t last_second;
   time_t last_bytes;

   int	 session_buffered;

   float xfer_rate();

   bool	 got_eof;
   bool	 print_run_status;
   bool	 need_seek;
   bool	 line_buf;

   XferJob(FileAccess *f);
   ~XferJob();

   void NextFile(char *f);
   virtual void NextFile() { NextFile(0); }

   int	 TryRead(FileAccess *s);
   int	 TryWrite(FDStream *f);

   bool	 use_urls;
   bool  non_strict_urls;
   ParsedURL *url;

   void  CountBytes(long);
   void	 RateDrain();

   char *Percent(); // return either string ending with a space or empty string
   char *CurrRate(float);
   char *CurrRate() { return CurrRate(minute_xfer_rate); }
   char *CurrETA(float rate);
   char *CurrETA() { return CurrETA(minute_xfer_rate); }

public:
   long	 bytes_transferred;

   int	 Done();
   int	 ExitCode() { return failed; }

   void	 ShowRunStatus(StatusLine *);
   void	 PrintStatus(int);
   void	 SayFinal();

   void UseURLs()
      {
	 if(use_urls)
	    non_strict_urls=true;
	 else
	    use_urls=true;
      }
};

#endif /* XFERJOB_H */
