/*
 * lftp and utils
 *
 * Copyright (c) 1996-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "misc.h"

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
   {
      cp->Resume();
      return super::Do();
   }

   if(cp->Done())
      RemoveBackupFile();

   if(chunks_done && chunks && cp->GetPos()>=chunks[0]->start)
   {
      cp->SetRange(0,cp->GetPos());
      cp->Resume();
      cp->Do();
      free_chunks();
      m=MOVED;
   }

   if(chunks==0 || cp->GetPos()<chunks[0]->start)
   {
      cp->Resume();
      m=super::Do(); // it can call NextFile.
   }
   else if(chunks)
      cp->Suspend();

   if(Done() || chunks_done)
      return m;

   off_t offset=cp->GetPos();
   off_t size=cp->GetSize();

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
      off_t chunk_size=(size-offset)/max_chunks;
      if(chunk_size<0x10000)
	 chunk_size=0x10000;
      num_of_chunks=(size-offset)/chunk_size-1;
      if(num_of_chunks<1)
      {
	 no_parallel=true;
	 return MOVED;
      }
      chunks=(ChunkXfer**)xmalloc(sizeof(*chunks)*num_of_chunks);
      off_t curr_offs=size;
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
	 chunks[i]->SetParentFg(this,false);
	 chunks[i]->cmdline=(char*)xmalloc(7+2*20+1);
	 sprintf(chunks[i]->cmdline,"\\chunk %lld-%lld",
	    (long long)(curr_offs-chunk_size),(long long)(curr_offs-1));
	 curr_offs-=chunk_size;
      }
      xfree(cp->cmdline);
      cp->cmdline=(char*)xmalloc(7+2*20+1);
      sprintf(cp->cmdline,"\\chunk 0-%lld",(long long)(chunks[0]->start-1));
      m=MOVED;
   }

   /* cycle through the chunks */
   chunks_done=true;
   total_xferred=MIN(offset,chunks[0]->start);
   total_xfer_rate=cp->GetRate();

   off_t rem=chunks[0]->start-cp->GetPos();
   if(rem<=0)
      total_eta=0;
   else
      total_eta=cp->GetETA(rem);

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
	 if(total_eta>=0)
	 {
	    long eta=chunks[i]->GetETA();
	    if(eta<0)
	       total_eta=-1;
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

   return m;
}

// xgettext:c-format
static const char pget_status_format[]=N_("`%s', got %lld of %lld (%d%%) %s%s");
#define PGET_STATUS _(pget_status_format),name, \
   (long long)total_xferred,(long long)size, \
   percent(total_xferred,size),Speedometer::GetStrS(total_xfer_rate), \
   cp->GetETAStrSFromTime(total_eta)

void pgetJob::ShowRunStatus(StatusLine *s)
{
   if(Done() || no_parallel || max_chunks<2 || !chunks)
   {
      GetJob::ShowRunStatus(s);
      return;
   }

   if(!cp)
      return;

   const char *name=cp->SqueezeName(s->GetWidthDelayed()-58);
   off_t size=cp->GetSize();
   StringSet status;
   status.AppendFormat(PGET_STATUS);

   int w=s->GetWidthDelayed();
   char *bar=string_alloca(w--);
   memset(bar,'.',w);
   bar[w]=0;

   int i;
   int p=cp->GetPos()*w/size;
   for(i=0; i<p; i++)
      bar[i]='o';

   for(int chunk=0; chunk<num_of_chunks; chunk++)
   {
      p=(chunks[chunk]->Done()?chunks[chunk]->limit:chunks[chunk]->GetPos())*w/size;
      for(i=chunks[chunk]->start*w/size; i<p; i++)
	 bar[i]='o';
   }

   status.Append(bar);

   s->Show(status);
}

// list subjobs (chunk xfers) only when verbose
void  pgetJob::ListJobs(int verbose,int indent)
{
   if(!chunks)
   {
      Job::ListJobs(verbose,indent);
      return;
   }
   if(verbose>1 && cp)
   {
      cp->SetRange(0,chunks[0]->start);
      Job::ListJobs(verbose,indent);
      cp->SetRange(0,FILE_END);
   }
}

void  pgetJob::PrintStatus(int verbose,const char *prefix)
{
   if(!cp || Done() || no_parallel || max_chunks<2 || !chunks)
   {
      GetJob::PrintStatus(verbose,prefix);
      return;
   }

   SessionJob::PrintStatus(verbose,prefix);

   printf("\t");
   const char *name=cp->GetDispName();
   off_t size=cp->GetSize();
   printf(PGET_STATUS);
   printf("\n");
}

void pgetJob::free_chunks()
{
   if(chunks)
   {
      for(int i=0; i<num_of_chunks; i++)
      {
	 bytes+=chunks[i]->GetBytesCount();
	 Delete(chunks[i]);
      }
      xfree(chunks);
      chunks=0;
   }
   if(cp)
   {
      xfree(cp->cmdline);
      cp->cmdline=0;
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
				       FDStream *local,off_t start,off_t limit)
{
   FileCopyPeerFDStream
	       	*dst_peer=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   FileCopyPeer *src_peer=new FileCopyPeerFA(session,remote,FA::RETRIEVE);

   dst_peer->DontDeleteStream();
   dst_peer->NeedSeek(); // seek before writing
   dst_peer->SetBase(0);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->SetRange(start,limit);
   c->SetSize(cp->GetSize());
   c->DontCopyDate();
   c->FailIfCannotSeek();

   return new ChunkXfer(c,remote,start,limit);
}

pgetJob::ChunkXfer::ChunkXfer(FileCopy *c1,const char *name,
			      off_t s,off_t lim)
   : CopyJob(c1,name,"pget")
{
   start=s;
   limit=lim;
}

pgetJob::ChunkXfer::~ChunkXfer()
{
}
