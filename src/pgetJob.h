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

#ifndef PGETJOB_H
#define PGETJOB_H

#include "GetJob.h"

class pgetJob : public GetJob
{
   class ChunkXfer : public XferJob
   {
      friend class pgetJob;

      long start;
      long limit;
      char *error_text;
      bool error:1;
      bool done:1;
      bool no_parallel:1;
      FDStream *local;

      ChunkXfer(FileAccess *session,const char *remote,FDStream *local,
	        long start,long limit);
      ~ChunkXfer();

      int Do();
      int Done() { return done; }
      void PrintStatus(int);
   };

   ChunkXfer   **chunks;
   int	 max_chunks;
   int	 num_of_chunks;
   long	 total_xferred;
   float total_xfer_rate;

   bool	no_parallel:1;
   bool chunks_done:1;

   void free_chunks();

protected:
   void	 NextFile();

public:
   int Do();
   void ShowRunStatus(StatusLine *s);
   void PrintStatus(int);
   void ListJobs(int verbose,int indent=0);

   pgetJob(FileAccess *s,ArgV *args)
      : GetJob(s,args,/*cont=*/false)
   {
      chunks=0;
      num_of_chunks=0;
      total_xferred=0;
      total_xfer_rate=0;
      no_parallel=false;
      max_chunks=5;
      need_seek=true; // seek before writing
   }
   ~pgetJob();

   void SetMaxConn(int n) { max_chunks=n; }
};

#endif//PGETJOB_H
