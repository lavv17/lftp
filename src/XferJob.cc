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
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>

#include "xmalloc.h"

#include "XferJob.h"
#include "plural.h"
#include "ResMgr.h"

static ResDecl
   res_use_urls	  ("xfer:use-urls",    "no", ResMgr::BoolValidate,0),
   res_eta_terse  ("xfer:eta-terse",   "yes",ResMgr::BoolValidate,0);

char *XferJob::Percent()
{
   static char p[10];
   p[0]=0;
   if(size>0)
      sprintf(p,"(%d%%) ",percent(Offset(),size));
   return p;
}

bool XferJob::CanShowRate(float rate)
{
   return(now-start_time>3 && now-last_bytes<30 && rate>1);
}

char *XferJob::CurrRate(float rate)
{
   static char speed[40];
   speed[0]=0;
   rate_shown=false;
   if(CanShowRate(rate))
   {
      rate_shown=true;
      if(rate<1024)
	 sprintf(speed,_("%.0fb/s "),rate);
      else if(rate<1024*1024)
	 sprintf(speed,_("%.1fK/s "),rate/1024.);
      else
	 sprintf(speed,_("%.2fM/s "),rate/1024./1024.);
   }
   return speed;
}

#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)

char *XferJob::CurrETA(float rate,long offs)
{
   static char eta_str[20];
   eta_str[0]=0;
   if(size>0 && size>=offs && CanShowRate(rate))
   {
      long eta=(long)((size-offs) / rate + 0.5);
      long eta2=0;
      long ueta=0;
      long ueta2=0;
      char letter=0;
      char letter2=0;

      // for translator: only first letter matters
      const char day_c=_("day")[0];
      const char hour_c=_("hour")[0];
      const char minute_c=_("minute")[0];
      const char second_c=_("second")[0];

      const char *tr_eta=_("eta:");

      if((bool)res_eta_terse.Query(0))
      {
	 if(eta>=DAY)
	 {
	    ueta=(eta+DAY/2)/DAY;
	    eta2=eta-ueta*DAY;
	    letter=day_c;
	    if(ueta<10)
	    {
	       letter2=hour_c;
	       ueta2=((eta2<0?eta2+DAY:eta2)+HOUR/2)/HOUR;
	       if(ueta2>0 && eta2<0)
		  ueta--;
	    }
	 }
	 else if(eta>=HOUR)
	 {
	    ueta=(eta+HOUR/2)/HOUR;
	    eta2=eta-ueta*HOUR;
	    letter=hour_c;
	    if(ueta<10)
	    {
	       letter2=minute_c;
	       ueta2=((eta2<0?eta2+HOUR:eta2)+MINUTE/2)/MINUTE;
	       if(ueta2>0 && eta2<0)
		  ueta--;
	    }
	 }
	 else if(eta>=MINUTE)
	 {
	    ueta=(eta+MINUTE/2)/MINUTE;
	    letter=minute_c;
	 }
	 else
	 {
	    ueta=eta;
	    letter=second_c;
	 }
	 if(letter2 && ueta2>0)
	    sprintf(eta_str,"%s%ld%c%ld%c ",tr_eta,ueta,letter,ueta2,letter2);
	 else
	    sprintf(eta_str,"%s%ld%c ",tr_eta,ueta,letter);
      }
      else // verbose eta (by Ben Winslow)
      {
	 long unit;
	 strcpy(eta_str, tr_eta);

	 if(eta>=DAY)
	 {
	    unit=eta/DAY;
	    sprintf(eta_str+strlen(eta_str), "%ld%c", unit, day_c);
	 }
	 if(eta>=HOUR)
	 {
	    unit=(eta/HOUR)%24;
	    sprintf(eta_str+strlen(eta_str), "%ld%c", unit, hour_c);
	 }
	 if(eta>=MINUTE)
	 {
	    unit=(eta/MINUTE)%60;
	    sprintf(eta_str+strlen(eta_str), "%ld%c", unit, minute_c);
	 }
	 unit=eta%60;
	 sprintf(eta_str+strlen(eta_str), "%ld%c ", unit, second_c);
      }
   }
   return eta_str;
}

void  XferJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;
   if(Done())
      return;

   const char *st=session->CurrentStatus();

   if(curr && session->IsOpen())
   {
      int w=s->GetWidthDelayed()-40;
      if(w<=0)
	 return;
      const char *n=curr;
      if((int)strlen(n)>w-2)
	 n+=strlen(n)-(w-2);

      s->Show(_("`%s' at %lu %s%s%s[%s]"),n,Offset(),Percent(),CurrRate(),CurrETA(),st);
   }
}

void  XferJob::SayFinal()
{
   if(!print_run_status)
      return;
   if(file_count==failed)
      return;
   if(bytes_transferred)
   {
      if(end_time>start_time)
      {
	 long sec=end_time-start_time;
	 printf(plural("%ld $#l#byte|bytes$ transferred"
			" in %ld $#l#second|seconds$ (%g bytes/s)\n",
			bytes_transferred,sec),
	    bytes_transferred,sec,xfer_rate());
      }
      else
      {
	 printf(plural("%ld $#l#byte|bytes$ transferred\n",
			bytes_transferred),
	    bytes_transferred);
      }
   }
   if(failed>0)
   {
      printf(plural("Transfer of %d of %d $file|files$ failed\n",file_count),
	 failed,file_count);
   }
   else if(file_count>1)
   {
      printf(plural("Total %d $file|files$ transferred\n",file_count),
	 file_count);
   }
}

void  XferJob::PrintStatus(int verbose)
{
   SessionJob::PrintStatus(verbose);
   if(Done())
   {
      if(file_count==0)
	 return;
      if(failed==file_count)
      {
      	 // xgettext:c-format
	 printf(_("\tNo files transferred successfully :(\n"));
	 return;
      }
      if(failed>0)
      {
	 putchar('\t');
	 printf(plural("Transfer of %d of %d $file|files$ failed\n",file_count),
	    failed,file_count);
      }
      else if(file_count>1)
      {
      	 putchar('\t');
	 printf(plural("Total %d $file|files$ transferred\n",file_count),
	    file_count);
      }
      if(end_time>start_time)
      {
      	 putchar('\t');
	 printf(_("Average transfer rate %g bytes/s\n"),xfer_rate());
      }
      return;
   }

   if(curr && session->IsOpen())
   {
      putchar('\t');
      printf(_("`%s' at %lu %s%s%s[%s]"),curr,Offset(),Percent(),CurrRate(),
	       CurrETA(),session->CurrentStatus());
      putchar('\n');
   }
}

void XferJob::NextFile(char *f)
{
   last_bytes=0;

   if(curr)
      file_count++;

   if(session)
      session->Close();
   offset=0;
   size=-1;
   got_eof=false;
   in_buffer=0;
   curr=f;
   session_buffered=0;

   if(!curr)
   {
      end_time=now;
      end_time_ms=now_ms;
   }
   else
   {
      if(use_urls)
      {
	 if(url)
	    delete url;
	 url=new ParsedURL(curr);
	 if((url->proto || non_strict_urls) && url->path)
	 {
	    FileAccess *new_session=session;
	    if(url->proto)
	       new_session=FileAccess::New(url->proto);
	    if(!new_session)
	    {
	       eprintf(_("%s: %s - not supported protocol\n"),
			op,url->proto);
	       return;
	    }
	    if(url->user && url->pass)
	       new_session->Login(url->user,url->pass);
	    if(url->host)
	       new_session->Connect(url->host,url->port);
	    curr=url->path;
	    if(new_session!=session)
	    {
	       SessionPool::Reuse(session);
	       session=new_session;
	    }
	 }
      }
   }
}

XferJob::XferJob(FileAccess *f) : SessionJob(f)
{
   in_buffer=0;
   buffer_size=0;
   buffer=0;
   file_count=0;
   failed=0;
   offset=0;
   UpdateNow();
   start_time=now;
   start_time_ms=now_ms;
   end_time=now;
   end_time_ms=now_ms;
   last_second=start_time;
   last_bytes=0;
   bytes_transferred=0;
   minute_xfer_rate=0;
   curr=0;
   got_eof=false;
   print_run_status=true;
   need_seek=false;
   line_buf=false;
   op="";
   size=-1;
   use_urls=res_use_urls.Query(0);  // Query once, can't change on fly
   non_strict_urls=false;
   url=0;
   session_buffered=0;
   rate_shown=false;
}

XferJob::~XferJob()
{
   if(url)
      delete url;
   xfree(buffer);
}

int XferJob::TryRead(FileAccess *s)
{
   if(in_buffer==buffer_size)
   {
      if(buffer_size<0x10000)
      {
	 if(buffer==0)
	    buffer=(char*)xmalloc(buffer_size=0x1000);
	 else
	    buffer=(char*)xrealloc(buffer,buffer_size*=2);
      }
      else
      {
	 s->Suspend();
	 return s->DO_AGAIN;
      }
   }
   int res=s->Read(buffer+in_buffer,buffer_size-in_buffer);
   if(res==s->DO_AGAIN)
      return res;
   if(res<0)
   {
      fprintf(stderr,"%s: %s\n",op,s->StrError(res));
      failed++;
      s->Close();
      return res;
   }
   if(res==0)
   {
      // EOF
      got_eof=true;
      s->Suspend();
      return res;
   }
   in_buffer+=res;
   offset+=res;

   CountBytes(res);

   return res;
}

void XferJob::CountBytes(long res)
{
   bytes_transferred+=res;

   if(res<0)
   {
      minute_xfer_rate=0;
      last_bytes=0;
      return;
   }

   if(session)
   {
      int now_buffered=session->Buffered();
      res-=(now_buffered-session_buffered);
      if(res<0)
	 res=0;
      session_buffered=now_buffered;
   }

   float div=60;
   if(now-start_time<div)
      div=now-start_time+1;
   if(now-last_second>div)
      div=now-last_second;

   minute_xfer_rate*=1.-(now-last_second)/div;
   minute_xfer_rate+=res/div;
   last_second=now;

   if(res>0)
      last_bytes=now;
}

void  XferJob::RateDrain()
{
   if(!session || session->IsOpen())
   {
      CountBytes(0);
      if(rate_shown)
	 block+=TimeOut(1000);
   }
   else
   {
      last_second=now;
   }
}

int XferJob::TryWrite(FDStream *f)
{
   if(f->broken())
   {
      failed++;
      return -1;
   }

   if(in_buffer==0)
   {
      if(session)
	 session->Resume();
      return 0;
   }

   // try to write the buffer contents
   int fd=f->getfd();
   if(fd==-1)
   {
      if(!f->error())
      {
	 block+=TimeOut(1000);
	 return 0;
      }
      fprintf(stderr,"%s: %s\n",op,f->error_text);
      failed++;
      return -1;
   }

   if(fg_data==0)
      fg_data=new FgData(f->GetProcGroup(),fg);

   if(need_seek)
      lseek(fd,session->GetPos()-in_buffer,SEEK_SET);

   int to_write=in_buffer;
   if(line_buf)
   {
      char *nl=buffer+in_buffer-1;
      while(nl>=buffer && *nl!='\n')
	 nl--;
      if(nl<buffer)
      {
	 if(!got_eof && in_buffer!=buffer_size)
	    return 0;
      }
      else
	 to_write=nl-buffer+1;
   }

   // fd should be non-blocking.
   res=write(fd,buffer,to_write);
   if(res==-1)
   {
      if(errno==EAGAIN || errno==EINTR)
	 goto normal_return;
      perror(f->name);
      failed++;
      return -1;
   }
   if(res==0)
   {
      fprintf(stderr,_("%s: cannot write -- disk full?\n"),op);
      failed++;
      return -1;
   }
   in_buffer-=res;
   memmove(buffer,buffer+res,in_buffer);

normal_return:
   if(in_buffer>0)
      block+=PollVec(fd,POLLOUT);
   if(in_buffer<buffer_size && session)
      session->Resume();
   return res;
}

int   XferJob::Done()
{
   if(curr!=0)
      return false;
   NextFile();
   return curr==0;
}

float XferJob::xfer_rate()
{
   float how_long = (end_time-start_time) + (end_time_ms-start_time_ms)/1000.0;
   return bytes_transferred/how_long;
}

long XferJob::Offset()
{
   int buf=0;
   long off=offset;
   if(session)
      buf=session->Buffered();
   off-=buf;
   if(off<0)
      off=0;
   return off;
}
