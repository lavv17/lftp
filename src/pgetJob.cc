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

#include <config.h>
#include "pgetJob.h"

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))

int pgetJob::Do()
{
   int m=STALL;

   if(curr==0)
      return GetJob::Do();

   if(no_parallel || max_chunks<2)
      return GetJob::Do();

   RateDrain();

   if(chunks==0 || offset<chunks[0]->start)
      m=GetJob::Do();
   else
      session->Suspend();

   if(Done())
      return m;

   if(chunks==0)
   {
      if(size==-1)
	 return m;

      /* initialize chunks */
      long chunk_size=(size-offset)/max_chunks;
      if(chunk_size<0x10000)
	 chunk_size=0x10000;
      num_of_chunks=(size-offset)/chunk_size-1;
      if(num_of_chunks<1)
      {
	 no_parallel=true;
	 return MOVED;
      }
      chunks=(ChunkXfer**)xmalloc(sizeof(*chunks)*num_of_chunks);
      long curr_offs=size;
      for(int i=num_of_chunks; i-->0; )
      {
	 chunks[i]=new ChunkXfer(session->Clone(),curr,local,
		     curr_offs-chunk_size,curr_offs);
	 chunks[i]->parent=this;
	 chunks[i]->cmdline=(char*)xmalloc(7+2*20+1);
	 sprintf(chunks[i]->cmdline,"chunk %ld-%ld",
				    curr_offs-chunk_size,curr_offs);
	 curr_offs-=chunk_size;
      }
      m=MOVED;
   }

   /* cycle through the chunks */
   chunks_done=true;
   total_xferred=MIN(offset,chunks[0]->start);
   total_xfer_rate=minute_xfer_rate;
   for(int i=0; i<num_of_chunks; i++)
   {
      if(chunks[i]->error)
      {
	 no_parallel=true;
	 break;
      }
      total_xferred+=MIN(chunks[i]->offset,chunks[i]->limit)-chunks[i]->start;
      if(last_bytes<chunks[i]->last_bytes)
	 last_bytes=chunks[i]->last_bytes;
      if(!chunks[i]->Done())
      {
	 total_xfer_rate+=chunks[i]->minute_xfer_rate;
	 chunks_done=false;
      }
   }

   if(no_parallel)
   {
      free_chunks();
      return MOVED;
   }

   if(chunks_done && offset>=chunks[0]->start)
   {
      if(file_time>=0)
	 local->setmtime(file_time);
      RemoveBackupFile();
      NextFile();
      return MOVED;
   }

   return m;
}

void pgetJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;

   if(Done() || no_parallel || max_chunks<2 || !chunks)
   {
      GetJob::ShowRunStatus(s);
      return;
   }

   if(!curr || !session->IsOpen())
      return;

   int w=s->GetWidth()-40;
   if(w<=0)
      return;
   char *n=curr;
   if((int)strlen(n)>w)
      n=n+strlen(n)-w;
   // xgettext:c-format
   s->Show(_("`%s', got %lu of %lu (%d%%) %s%s"),n,total_xferred,size,
	 percent(total_xferred,size),CurrRate(total_xfer_rate),
	 CurrETA(total_xfer_rate,total_xferred));
}

// list subjobs (chunk xfers) only when verbose
void  pgetJob::ListJobs(int verbose,int indent)
{
   if(verbose>1)
      Job::ListJobs(verbose,indent);
}

void  pgetJob::PrintStatus(int verbose)
{
   if(Done() || no_parallel || max_chunks<2 || !chunks)
   {
      GetJob::PrintStatus(verbose);
      return;
   }

   SessionJob::PrintStatus(verbose);

   if(curr && session->IsOpen())
   {
      putchar('\t');
      printf(_("`%s', got %lu of %lu (%d%%) %s%s"),curr,total_xferred,size,
	    percent(total_xferred,size),CurrRate(total_xfer_rate),
	    CurrETA(total_xfer_rate,total_xferred));
      putchar('\n');
      if(verbose>1)
      {
	 // xgettext:c-format
	 printf(_("\tat %ld (%d%%) [%s]\n"),MIN(offset,chunks[0]->start),
	       percent(offset,chunks[0]->start),session->CurrentStatus());
      }
   }
}

void pgetJob::free_chunks()
{
   if(chunks)
   {
      for(int i=0; i<num_of_chunks; i++)
	 delete chunks[i];
      free(chunks);
      chunks=0;
   }
}

pgetJob::~pgetJob()
{
   free_chunks();
}

void pgetJob::NextFile()
{
   free_chunks();

   GetJob::NextFile();
   no_parallel=false;
   total_xferred=0;
}

pgetJob::ChunkXfer::ChunkXfer(FileAccess *session,char *remote,FDStream *local,
		     long start,long limit)
   : XferJob(session)
{
   this->curr=remote;
   this->local=local;
   this->start=start;
   this->limit=limit;

   offset=start;

   error_text=0;
   done=false;
   error=false;
   no_parallel=false;

   need_seek=true;   // seek before writing
}

pgetJob::ChunkXfer::~ChunkXfer()
{
   if(error_text)
      free(error_text);
}

int pgetJob::ChunkXfer::Do()
{
   RateDrain();

   int m=STALL;
   int res;

   if(done)
      return STALL;

   if(in_buffer==0 && got_eof)
   {
      session->Close();
      done=true;
      return MOVED;
   }
   if(!got_eof)
   {
      if(session->IsClosed())
      {
	 session->Open(curr,FA::RETRIEVE,start);
      }
      res=TryRead(session);
      if(res<0 && res!=FA::DO_AGAIN)
      {
	 error=true;
	 done=true;
	 return MOVED;
      }
      else if(res>=0)
      {
	 ((pgetJob*)parent)->bytes_transferred+=res;
	 m=MOVED;
      }
   }
   /* check if restart did not work */
   if(session->GetPos()>session->GetRealPos())
   {
      error=true;
      done=true;
      return MOVED;
   }
   if(session->GetPos()>=limit)
   {
      got_eof=true;
      session->Suspend();
   }

   res=TryWrite(local);
   if(res<0)
   {
      error=true;
      done=true;
      return MOVED;
   }
   if(res>0)
      m=MOVED;

   return m;
}

void  pgetJob::ChunkXfer::PrintStatus(int)
{
   if(done)
   {
      // xgettext:c-format
      printf(_("\tDone\n"));
      return;
   }

   if(curr && session->IsOpen())
   {
      printf(_("\tat %ld (%d%%) [%s]\n"),offset,percent(offset-start,limit-start),
			      session->CurrentStatus());
   }
}
