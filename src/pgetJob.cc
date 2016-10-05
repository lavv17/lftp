/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stddef.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include "pgetJob.h"
#include "url.h"
#include "misc.h"
#include "log.h"

ResType pget_vars[] = {
   {"pget:save-status",	"10s",   ResMgr::TimeIntervalValidate,ResMgr::NoClosure},
   {"pget:default-n",   "5",	 ResMgr::UNumberValidate,ResMgr::NoClosure},
   {"pget:min-chunk-size", "1M", ResMgr::UNumberValidate,ResMgr::NoClosure},
   {0}
};
ResDecls pget_vars_register(pget_vars);

#undef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))

#define super CopyJob

int pgetJob::Do()
{
   int m=STALL;

   if(Done())
      return m;

   if(status_timer.Stopped())
   {
      SaveStatus();
      status_timer.Reset();
   }

   if(c->Done())
   {
      if(status_file)
      {
	 remove(status_file);
	 status_file.set(0);
      }
   }

   if(no_parallel || max_chunks<2)
   {
      c->Resume();
      return super::Do();
   }

   if(chunks_done && chunks && c->GetPos()>=limit0)
   {
      c->SetRangeLimit(limit0);    // make it stop.
      c->Resume();
      c->Do();
      free_chunks();
      m=MOVED;
   }

   if(chunks==0 || c->GetPos()<limit0)
   {
      c->Resume();
      m|=super::Do();
   }
   else if(chunks.count()>0)
   {
      if(chunks[0]->Error())
      {
	 Log::global->Format(0,"pget: chunk[%d] error: %s\n",0,chunks[0]->ErrorText());
	 no_parallel=true;
	 c->Resume();
      }
      else if(!chunks[0]->Done() && chunks[0]->GetBytesCount()<limit0/16)
      {
	 c->Resume();
	 if(chunks.count()==1)
	 {
	    free_chunks();
	    no_parallel=true;
	 }
	 else
	 {
	    limit0=chunks[0]->c->GetRangeLimit();
	    chunks.remove(0);
	 }
	 m=MOVED;
      }
      else
	 c->Suspend();
   }

   if(Done())
      return m;

   off_t offset=c->GetPos();
   off_t size=c->GetSize();

   if(chunks==0 && !chunks_done)
   {
      if(size==NO_SIZE_YET)
	 return m;

      if(size==NO_SIZE || (c->put && c->put->GetLocal()==0))
      {
	 Log::global->Write(0,_("pget: falling back to plain get"));
	 Log::global->Write(0," (");
	 if(c->put && c->put->GetLocal()==0)
	 {
	    Log::global->Write(0,_("the target file is remote"));
	    if(size==NO_SIZE)
	       Log::global->Write(0,", ");
	 }
	 if(size==NO_SIZE)
	    Log::global->Write(0,_("the source file size is unknown"));
	 Log::global->Write(0,")\n");

	 no_parallel=true;
	 return m;
      }

      // Make sure the destination file is open before starting chunks,
      // it disables temp-name creation in the chunk's Init.
      if(c->put->GetLocal()->getfd()==-1)
	 return m;

      c->put->NeedSeek(); // seek before writing

      if(pget_cont)
	 LoadStatus();
      else if(status_file)
	 remove(status_file);
      if(!chunks)
	 InitChunks(offset,size);

      m=MOVED;

      if(!chunks)
      {
	 no_parallel=true;
	 return m;
      }
      if(!pget_cont)
      {
	 SaveStatus();
	 status_timer.Reset();
	 if(ResMgr::QueryBool("file:use-fallocate",0)) {
	    // allocate space after creating *.lftp-pget-status file,
	    // so that the incomplete status is more obvious.
	    const Ref<FDStream>& local=c->put->GetLocal();
	    if(lftp_fallocate(local->getfd(),size)==-1 && errno!=ENOSYS && errno!=EOPNOTSUPP) {
	       eprintf(_("pget: warning: space allocation for %s (%lld bytes) failed: %s\n"),
		  local->name.get(),(long long)size,strerror(errno));
	    }
	 }
      }
   }

   /* cycle through the chunks */
   chunks_done=true;
   total_xferred=MIN(offset,limit0);
   off_t got_already=c->GetSize()-limit0;
   total_xfer_rate=c->GetRate();

   off_t rem=limit0-c->GetPos();
   if(rem<=0)
      total_eta=0;
   else
      total_eta=c->GetETA(rem);

   for(int i=0; i<chunks.count(); i++)
   {
      if(chunks[i]->Error())
      {
	 Log::global->Format(0,"pget: chunk[%d] error: %s\n",i,chunks[i]->ErrorText());
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
   c->GetETAStrSFromTime(total_eta)

void pgetJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Done() || no_parallel || max_chunks<2 || !chunks)
   {
      super::ShowRunStatus(s);
      return;
   }

   const char *name=SqueezeName(s->GetWidthDelayed()-58);
   off_t size=GetSize();
   StringSet status;
   status.AppendFormat(PGET_STATUS);

   int w=s->GetWidthDelayed();
   char *bar=string_alloca(w--);
   memset(bar,'+',w);
   bar[w]=0;

   int i;
   int p=c->GetPos()*w/size;
   for(i=start0*w/size; i<p; i++)
      bar[i]='o';
   p=limit0*w/size;
   for( ; i<p; i++)
      bar[i]='.';

   for(int chunk=0; chunk<chunks.count(); chunk++)
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
xstring& pgetJob::FormatJobs(xstring& s,int verbose,int indent)
{
   indent--;
   if(!chunks)
      return Job::FormatJobs(s,verbose,indent);
   if(verbose>1)
   {
      if(c->GetPos()<limit0)
      {
	 s.appendf("%*s\\chunk %lld-%lld\n",indent,"",(long long)start0,(long long)limit0);
	 c->SetRangeLimit(limit0); // to see right ETA.
	 CopyJob::FormatStatus(s,verbose,"\t");
	 c->SetRangeLimit(FILE_END);
      }
      Job::FormatJobs(s,verbose,indent);
   }
   return s;
}

xstring& pgetJob::FormatStatus(xstring& s,int verbose,const char *prefix)
{
   if(Done() || no_parallel || max_chunks<2 || !chunks)
      return super::FormatStatus(s,verbose,prefix);

   s.append(prefix);
   const char *name=GetDispName();
   off_t size=GetSize();
   s.appendf(PGET_STATUS);
   return s.append('\n');
}

void pgetJob::free_chunks()
{
   if(chunks)
   {
      for(int i=0; i<chunks.count(); i++)
	 chunks_bytes+=chunks[i]->GetBytesCount();
      chunks.unset();
   }
}

pgetJob::pgetJob(FileCopy *c1,const char *n,int m)
   : CopyJob(c1,n,"pget")
{
   chunks_bytes=0;
   start0=limit0=0;
   total_xferred=0;
   total_xfer_rate=0;
   no_parallel=false;
   chunks_done=false;
   pget_cont=c->SetContinue(false);
   max_chunks=m?m:ResMgr::Query("pget:default-n",0);
   total_eta=-1;
   status_timer.SetResource("pget:save-status",0);
   const Ref<FDStream>& local=c->put->GetLocal();
   if(local && local->full_name)
   {
      status_file.vset(local->full_name.get(),".lftp-pget-status",NULL);
      if(pget_cont)
	 LoadStatus0();
   }
}
void pgetJob::PrepareToDie()
{
   free_chunks();
   super::PrepareToDie();
}
pgetJob::~pgetJob()
{
}

pgetJob::ChunkXfer *pgetJob::NewChunk(const char *remote,off_t start,off_t limit)
{
   const Ref<FDStream>& local=c->put->GetLocal();
   FileCopyPeerFDStream
		*dst_peer=new FileCopyPeerFDStream(local,FileCopyPeer::PUT);
   dst_peer->NeedSeek(); // seek before writing
   dst_peer->SetBase(0);

   FileCopy *c1=FileCopy::New(c->get->Clone(),dst_peer,false);
   c1->SetRange(start,limit);
   c1->SetSize(GetSize());
   c1->DontCopyDate();
   c1->DontVerify();
   c1->FailIfCannotSeek();

   ChunkXfer *chunk=new ChunkXfer(c1,remote,start,limit);
   chunk->cmdline.setf("\\chunk %lld-%lld",(long long)start,(long long)(limit-1));
   return chunk;
}

pgetJob::ChunkXfer::ChunkXfer(FileCopy *c1,const char *name,
			      off_t s,off_t lim)
   : CopyJob(c1,name,"pget-chunk")
{
   start=s;
   limit=lim;
}

void pgetJob::SaveStatus()
{
   if(!status_file)
      return;

   FILE *f=fopen(status_file,"w");
   if(!f)
      return;

   off_t size=GetSize();
   fprintf(f,"size=%lld\n",(long long)size);

   int i=0;
   fprintf(f,"%d.pos=%lld\n",i,(long long)GetPos());
   if(!chunks)
      goto out_close;
   fprintf(f,"%d.limit=%lld\n",i,(long long)limit0);
   for(int chunk=0; chunk<chunks.count(); chunk++)
   {
      if(chunks[chunk]->Done())
	 continue;
      i++;
      fprintf(f,"%d.pos=%lld\n",i,(long long)chunks[chunk]->GetPos());
      fprintf(f,"%d.limit=%lld\n",i,(long long)chunks[chunk]->limit);
   }
out_close:
   fclose(f);
}
void pgetJob::LoadStatus0()
{
   if(!status_file)
      return;

   FILE *f=fopen(status_file,"r");
   if(!f) {
      int saved_errno=errno;
      // Probably the file is already complete
      // or it was previously downloaded by plain get.
      struct stat st;
      if(stat(c->put->GetLocal()->full_name,&st)==-1)
	 return;
      Log::global->Format(0,"pget: %s: cannot open (%s), resuming at the file end\n",
	 status_file.get(),strerror(saved_errno));
      c->SetRange(st.st_size,FILE_END);
      return;
   }

   long long size;
   if(fscanf(f,"size=%lld\n",&size)<1)
      goto out_close;

   long long pos;
   int j;
   if(fscanf(f,"%d.pos=%lld\n",&j,&pos)<2 || j!=0)
      goto out_close;
   Log::global->Format(10,"pget: got chunk[%d] pos=%lld\n",j,pos);
   c->SetRange(pos,FILE_END);

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
   int max_chunks=st.st_size/20; // highest estimate - min 20 bytes per chunk in status file.
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
   if(size<c->GetSize())  // file grew?
   {
      if(limit[i-1]==size)
	 limit[i-1]=c->GetSize();
      else
      {
	 pos[i]=size;
	 limit[i]=c->GetSize();
	 i++;
      }
   }
   int num_of_chunks=i-1;
   start0=pos[0];
   limit0=limit[0];
   c->SetRange(pos[0],FILE_END);
   if(num_of_chunks<1)
      goto out_close;
   for(i=0; i<num_of_chunks; i++)
   {
      ChunkXfer *c=NewChunk(GetName(),pos[i+1],limit[i+1]);
      c->SetParentFg(this,false);
      chunks.append(c);
   }
   goto out_close;
}

void pgetJob::InitChunks(off_t offset,off_t size)
{
   /* initialize chunks */
   off_t chunk_size=(size-offset)/max_chunks;
   int min_chunk_size=ResMgr::Query("pget:min-chunk-size",0);
   if(chunk_size<min_chunk_size)
      chunk_size=min_chunk_size;
   int num_of_chunks=(size-offset)/chunk_size-1;
   if(num_of_chunks<1)
      return;
   start0=0;
   limit0=size-chunk_size*num_of_chunks;
   off_t curr_offs=limit0;
   for(int i=0; i<num_of_chunks; i++)
   {
      ChunkXfer *c=NewChunk(GetName(),curr_offs,curr_offs+chunk_size);
      c->SetParentFg(this,false);
      chunks.append(c);
      curr_offs+=chunk_size;
   }
   assert(curr_offs==size);
}
