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
#include "url.h"

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))

#define super GetJob

int pgetJob::Do()
{
   int m=STALL;

   if(Done())
      return m;

   if(cp==0)
      return super::Do();

   if(no_parallel || max_chunks<2)
      return super::Do();

   if(cp->Done())
      RemoveBackupFile();

   long size=cp->GetSize();
   long offset=cp->GetPos();

   if(chunks==0 || offset<chunks[0]->start)
      m=super::Do();
   else
      cp->Suspend();

   if(chunks_done)
      return m;

   if(chunks==0)
   {
      if(size==NO_SIZE_YET)
	 return m;

      if(size==NO_SIZE || local==0)
      {
	 no_parallel=true;
	 return m;
      }

      cp->GetPut()->NeedSeek(); // seek before writing

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
	 const char *name=cp->GetName();
	 FileAccess *s=0;
	 ParsedURL u(name,true);
	 if(u.proto && u.path)
	 {
	    s=FA::New(&u);
	    if(s)
	       name=u.path;
	 }
	 if(!s)
	    s=session->Clone();
	 chunks[i]=NewChunk(s,name,local,curr_offs-chunk_size,curr_offs);
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
   total_xfer_rate=cp->GetRate();
   total_eta=-1;
   if(total_xfer_rate<1)
      total_eta=-2;
   else
   {
      long rem=chunks[0]->start-cp->GetPos();
      if(rem<=0)
	 total_eta=0;
      else
	 total_eta=long(rem/total_xfer_rate+.5);
   }
   for(int i=0; i<num_of_chunks; i++)
   {
      if(chunks[i]->Error())
      {
	 no_parallel=true;
	 break;
      }
      if(!chunks[i]->Done())
      {
	 if(chunks[i]->GetPos()>=chunks[i]->start)
	    total_xferred+=MIN(chunks[i]->GetPos(),chunks[i]->limit)
			   -chunks[i]->start;
	 if(total_eta!=-2)
	 {
	    long eta=chunks[i]->GetETA();
	    if(eta<0)
	       total_eta=-2;
	    else if(eta>total_eta)
	       total_eta=eta;	// total eta is the maximum.
	 }
	 total_xfer_rate+=chunks[i]->GetRate();
	 chunks_done=false;
      }
      else  // done
      {
	 total_xferred+=chunks[i]->limit-chunks[i]->start;
      }
   }

   if(no_parallel)
   {
      free_chunks();
      return MOVED;
   }

   if(chunks_done && cp->GetPos()>=chunks[0]->start)
   {
      cp->SetRange(0,cp->GetPos());
      cp->Resume();
      free_chunks();
      return MOVED;
   }

   return m;
}

void pgetJob::ShowRunStatus(StatusLine *s)
{
   if(Done() || no_parallel || max_chunks<2 || !chunks)
   {
      GetJob::ShowRunStatus(s);
      return;
   }

   if(!cp)
      return;

#define percent(offset,size) ((offset)>(size)?100: \
				 int(float(offset)*100/(size)))

   const char *name=cp->SqueezeName(s->GetWidthDelayed()-40);
   long size=cp->GetSize();
   // xgettext:c-format
   s->Show(_("`%s', got %lu of %lu (%d%%) %s%s"),name,total_xferred,size,
	 percent(total_xferred,size),Speedometer::GetStrS(total_xfer_rate),
	 Speedometer::GetETAStrSFromTime(total_eta));
}

// list subjobs (chunk xfers) only when verbose
void  pgetJob::ListJobs(int verbose,int indent)
{
   if(verbose>1)
      Job::ListJobs(verbose,indent);
}

void  pgetJob::PrintStatus(int verbose)
{
#if 0
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
#endif
}

void pgetJob::free_chunks()
{
   if(chunks)
   {
      for(int i=0; i<num_of_chunks; i++)
      {
	 bytes+=chunks[i]->GetBytesCount();
	 delete chunks[i];
      }
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

   super::NextFile();
   no_parallel=false;
   chunks_done=false;
   total_xferred=0;
   total_eta=-1;
}

pgetJob::ChunkXfer *pgetJob::NewChunk(FileAccess *session,const char *remote,
				       FDStream *local,long start,long limit)
{
   FileCopyPeerFDStream
	       	*dst_peer=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   FileCopyPeer *src_peer=new FileCopyPeerFA(session,remote,FA::RETRIEVE);

   dst_peer->DontDeleteStream();
   dst_peer->NeedSeek(); // seek before writing
   dst_peer->SetBase(0);

   FileCopy *c=new FileCopy(src_peer,dst_peer,false);
   c->SetRange(start,limit);
   c->SetSize(cp->GetSize());
   c->DontCopyDate();
   c->FailIfCannotSeek();

   return new ChunkXfer(c,remote,start,limit);
}

pgetJob::ChunkXfer::ChunkXfer(FileCopy *c1,const char *name,
			      long s,long lim)
   : CopyJob(c1,name)
{
   start=s;
   limit=lim;
}

pgetJob::ChunkXfer::~ChunkXfer()
{
}

void  pgetJob::ChunkXfer::PrintStatus(int)
{
#if 0
   if(curr && session->IsOpen())
   {
      printf(_("\tat %ld (%d%%) [%s]\n"),offset,percent(offset-start,limit-start),
			      session->CurrentStatus());
   }
#endif
}
