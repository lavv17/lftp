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
#include <sys/types.h>
#include <sys/stat.h>
#include "pgetJob.h"
#include "url.h"
#include "misc.h"
#include "log.h"

ResType pget_vars[] = {
   {"pget:save-status",	"10s",   ResMgr::TimeIntervalValidate,ResMgr::NoClosure},
   {"pget:default-n",   "5",	 ResMgr::UNumberValidate,ResMgr::NoClosure},
   {0}
};
ResDecls pget_vars_register(pget_vars);

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

   if(status_timer.Stopped())
   {
      SaveStatus();
      status_timer.Reset();
   }

   if(cp->Done())
   {
      RemoveBackupFile();
      if(status_file)
      {
	 remove(status_file);
	 status_file.set(0);
      }
   }

   if(no_parallel || max_chunks<2)
   {
      cp->Resume();
      return super::Do();
   }

   if(chunks_done && chunks && cp->GetPos()>=limit0)
   {
      cp->SetRangeLimit(cp->GetPos());    // make it stop.
      cp->Resume();
      cp->Do();
      free_chunks();
      m=MOVED;
   }

   if(chunks==0 || cp->GetPos()<limit0)
   {
      cp->Resume();
      m=super::Do(); // it can call NextFile.
      if(!cp)
	 return m;
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

      if(pget_cont)
	 LoadStatus();
      else if(status_file)
	 remove(status_file);
      if(!chunks)
	 InitChunks(offset,size);

      m=MOVED;

      if(chunks)
	 cp->cmdline.set_allocated(xasprintf("\\chunk %lld-%lld",(long long)start0,(long long)(limit0-1)));
      else
      {
	 no_parallel=true;
	 return m;
      }
   }

   /* cycle through the chunks */
   chunks_done=true;
   total_xferred=MIN(offset,limit0);
   off_t got_already=cp->GetSize()-limit0;
   total_xfer_rate=cp->GetRate();

   off_t rem=limit0-cp->GetPos();
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
      got_already-=chunks[i]->limit-chunks[i]->start;
   }
   total_xferred+=got_already;

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
   memset(bar,'+',w);
   bar[w]=0;

   int i;
   int p=cp->GetPos()*w/size;
   for(i=start0*w/size; i<p; i++)
      bar[i]='o';
   p=limit0*w/size;
   for( ; i<p; i++)
      bar[i]='.';

   for(int chunk=0; chunk<num_of_chunks; chunk++)
   {
      p=(chunks[chunk]->Done()?chunks[chunk]->limit:chunks[chunk]->GetPos())*w/size;
      for(i=chunks[chunk]->start*w/size; i<p; i++)
	 bar[i]='o';
      p=chunks[chunk]->limit*w/size;
      for( ; i<p; i++)
	 bar[i]='.';
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
      // to show proper ETA, change the range temporarily.
      cp->SetRangeLimit(limit0);
      Job::ListJobs(verbose,indent);
      cp->SetRangeLimit(FILE_END);
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
      cp->cmdline.set(0);
}

pgetJob::pgetJob(FileAccess *s,ArgV *args,bool cont)
   : GetJob(s,args,/*cont=*/false)
{
   chunks=0;
   num_of_chunks=0;
   start0=limit0=0;
   total_xferred=0;
   total_xfer_rate=0;
   no_parallel=false;
   chunks_done=false;
   pget_cont=cont;
   max_chunks=ResMgr::Query("pget:default-n",0);
   total_eta=-1;
   status_timer.SetResource("pget:save-status",0);
   truncate_target_first=!cont;
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

   if(local && local->full_name)
   {
      status_file.vset(local->full_name,".lftp-pget-status",NULL);
      if(pget_cont)
	 LoadStatus0();
   }
   else
      status_file.set(0);
}

pgetJob::ChunkXfer *pgetJob::NewChunk(const char *remote,FDStream *local,off_t start,off_t limit)
{
   FileCopyPeerFDStream
	       	*dst_peer=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   dst_peer->DontDeleteStream();
   dst_peer->NeedSeek(); // seek before writing
   dst_peer->SetBase(0);

   FileCopyPeer *src_peer=CreateCopyPeer(session->Clone(),remote,FA::RETRIEVE);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->SetRange(start,limit);
   c->SetSize(cp->GetSize());
   c->DontCopyDate();
   c->FailIfCannotSeek();

   ChunkXfer *chunk=new ChunkXfer(c,remote,start,limit);
   chunk->cmdline.set_allocated(xasprintf("\\chunk %lld-%lld",(long long)start,(long long)(limit-1)));
   return chunk;
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

void pgetJob::SaveStatus()
{
   if(!status_file)
      return;

   FILE *f=fopen(status_file,"w");
   if(!f)
      return;

   off_t size=cp->GetSize();
   fprintf(f,"size=%lld\n",size);

   int i=0;
   fprintf(f,"%d.pos=%lld\n",i,cp->GetPos());
   if(!chunks)
      goto out_close;
   fprintf(f,"%d.limit=%lld\n",i,limit0);
   for(int chunk=0; chunk<num_of_chunks; chunk++)
   {
      if(chunks[chunk]->Done())
	 continue;
      i++;
      fprintf(f,"%d.pos=%lld\n",i,chunks[chunk]->GetPos());
      fprintf(f,"%d.limit=%lld\n",i,chunks[chunk]->limit);
   }
out_close:
   fclose(f);
}
void pgetJob::LoadStatus0()
{
   if(!status_file)
      return;

   FILE *f=fopen(status_file,"r");
   if(!f)
      return;

   long long size;
   if(fscanf(f,"size=%lld\n",&size)<1)
      goto out_close;

   long long pos;
   int j;
   if(fscanf(f,"%d.pos=%lld\n",&j,&pos)<2 || j!=0)
      goto out_close;
   Log::global->Format(10,"pget: got chunk[%d] pos=%lld\n",j,pos);
   cp->SetRange(pos,FILE_END);

out_close:
   fclose(f);
}
void pgetJob::LoadStatus()
{
   if(!status_file)
      return;

   FILE *f=fopen(status_file,"r");
   if(!f)
      return;

   struct stat st;
   if(fstat(fileno(f),&st)<0)
   {
   out_close:
      fclose(f);
      return;
   }

   long long size;
   if(fscanf(f,"size=%lld\n",&size)<1)
      goto out_close;

   int i=0;
   int max_chunks=st.st_size/20; // highest estimate
   long long *pos=(long long *)alloca(2*max_chunks*sizeof(*pos));
   long long *limit=pos+max_chunks;
   for(;;)
   {
      int j;
      if(fscanf(f,"%d.pos=%lld\n",&j,pos+i)<2 || j!=i)
	 break;
      if(fscanf(f,"%d.limit=%lld\n",&j,limit+i)<2 || j!=i)
	 goto out_close;
      if(i>0 && pos[i]>=limit[i])
	 continue;
      Log::global->Format(10,"pget: got chunk[%d] pos=%lld\n",j,pos[i]);
      Log::global->Format(10,"pget: got chunk[%d] limit=%lld\n",j,limit[i]);
      i++;
   }
   if(i<1)
      goto out_close;
   if(size<cp->GetSize())  // file grew?
   {
      if(limit[i-1]==size)
	 limit[i-1]=cp->GetSize();
      else
      {
	 pos[i]=size;
	 limit[i]=cp->GetSize();
	 i++;
      }
   }
   num_of_chunks=i-1;
   start0=pos[0];
   limit0=limit[0];
   cp->SetRange(pos[0],FILE_END);
   if(num_of_chunks<1)
      goto out_close;
   chunks=(ChunkXfer**)xmalloc(sizeof(*chunks)*num_of_chunks);
   for(i=num_of_chunks; i-->0; )
   {
      chunks[i]=NewChunk(cp->GetName(),local,pos[i+1],limit[i+1]);
      chunks[i]->SetParentFg(this,false);
   }
   goto out_close;
}

void pgetJob::InitChunks(off_t offset,off_t size)
{
   /* initialize chunks */
   off_t chunk_size=(size-offset)/max_chunks;
   if(chunk_size<0x10000)
      chunk_size=0x10000;
   num_of_chunks=(size-offset)/chunk_size-1;
   if(num_of_chunks<1)
      return;
   chunks=(ChunkXfer**)xmalloc(sizeof(*chunks)*num_of_chunks);
   off_t curr_offs=size;
   for(int i=num_of_chunks; i-->0; )
   {
      chunks[i]=NewChunk(cp->GetName(),local,curr_offs-chunk_size,curr_offs);
      chunks[i]->SetParentFg(this,false);
      curr_offs-=chunk_size;
   }
   start0=0;
   limit0=curr_offs;
}
