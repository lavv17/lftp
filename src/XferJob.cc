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

#include <config.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <assert.h>
#include <ctype.h>
#include <netdb.h>

#include "xmalloc.h"

#include "XferJob.h"
#include "ProtoList.h"
#include "rglob.h"

char *XferJob::Percent()
{
   static char p[10];
   p[0]=0;
   if(size>0)
      sprintf(p,"(%d%%) ",percent(offset,size));
   return p;
}

char *XferJob::CurrRate(float rate)
{
   static char speed[40];
   speed[0]=0;
   if(time(0)-start_time>3 && time(0)-last_bytes<30 && rate!=0)
   {
      if(rate<1024)
	 sprintf(speed,"%.0fb/s ",rate);
      else if(rate<1024*1024)
	 sprintf(speed,"%.1fK/s ",rate/1024.);
      else
	 sprintf(speed,"%.2fM/s ",rate/1024./1024.);
   }
   return speed;
}

void  XferJob::ShowRunStatus(StatusLine *s)
{
   if(!print_run_status)
      return;
   if(Done())
   {
      s->Show("");
      return;
   }

   time_t now=time(0);
   const char *st=session->CurrentStatus();

   if(now==last_status_update && !strncmp(st,last_status,sizeof(last_status)))
      return;

   strncpy(last_status,st,sizeof(last_status));
   last_status_update=now;

   if(curr && session->IsOpen())
   {
      int w=s->GetWidth()-30;
      if(w<=0)
	 return;
      char *n=curr;
      if((int)strlen(n)>w)
	 n=n+strlen(n)-w;

      s->Show(_("`%s' at %lu %s%s[%s]"),n,offset,Percent(),CurrRate(),st);
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
      if(end_time>start_time+1)
      {
	 printf(_("%ld bytes transferred in %ld seconds (%g bytes/s)\n"),
	    bytes_transferred,(long)(end_time-start_time),xfer_rate());
      }
      else
	 printf(_("%ld bytes transferred\n"),bytes_transferred);
   }
   if(failed>0)
      printf(_("Transfer of %d of %d files failed\n"),failed,file_count);
   else if(file_count>1)
      printf(_("%d files total\n"),file_count);
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
	 printf(_("Transfer of %d of %d files failed\n"),failed,file_count);
      }
      else if(file_count>1)
      {
      	 putchar('\t');
	 printf(_("%d files tranferred"),file_count);
	 putchar('\n');
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
      printf(_("`%s' at %lu %s%s[%s]"),curr,offset,Percent(),CurrRate(),
	       session->CurrentStatus());
      putchar('\n');
   }
}

void XferJob::NextFile(char *f)
{
   last_bytes=0;

   if(curr)
      file_count++;

   session->Close();
   offset=0;
   size=-1;
   got_eof=false;
   in_buffer=0;
   curr=f;

   if(!curr)
   {
      time(&end_time);
   }
   else
   {
      if(use_urls)
      {
	 if(url)
	    delete url;
	 url=new ParsedURL(curr);
	 if(url->proto && url->path)
	 {
	    FileAccess *new_session=Protocol::NewSession(url->proto);
	    if(!new_session)
	    {
	       eprintf(_("%s: %s - not supported protocol\n"),
			op,url->proto);
	       return;
	    }
	    if(url->user && url->pass)
	       new_session->Login(url->user,url->pass);
	    if(url->host)
	    {
	       int port=0;
	       if(url->port)
	       {
		  if(isdigit(url->port[0]))
		     port=atoi(url->port);
		  else
		  {
		     struct servent *serv=getservbyname(url->port,"tcp");
		     if(serv)
			port=serv->s_port;
		     else
			eprintf(_("%s: %s - no such tcp service, using default\n"),
				 op,url->port);
		  }
	       }
	       new_session->Connect(url->host,port);
	    }
	    curr=url->path;
	    SessionPool::Reuse(session);
	    session=new_session;
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
   time(&start_time);
   end_time=start_time;
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
   use_urls=false;
   url=0;
   last_status_update=0;
   last_status[0]=0;
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

   time_t t=time(0);

   float div=60;
   if(t-start_time<div)
      div=t-start_time+1;
   if(t-last_second>div)
      div=t-last_second;

   minute_xfer_rate*=1.-(t-last_second)/div;
   minute_xfer_rate+=res/div;
   last_second=t;

   if(res>0)
      last_bytes=t;
}

void  XferJob::RateDrain()
{
   if(session->IsOpen())
   {
      CountBytes(0);
      block+=TimeOut(4000);
   }
   else
   {
      last_second=time(0);
   }
}

int XferJob::TryWrite(FDStream *f)
{
   if(in_buffer==0)
   {
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

   struct pollfd pfd={fd,POLLOUT};
   int res=poll(&pfd,1,0);
   if(res==1 && pfd.revents&(POLLOUT|POLLNVAL))
   {
      res=write(fd,buffer,to_write);
      if(res==-1)
      {
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
   }
   else if(res==1)   // what's up?
   {
//       fprintf(stderr,"Broken pipe\n");
      failed++;
      return -1;
   }
   if(in_buffer>0)
      block+=PollVec(fd,POLLOUT);
   if(in_buffer<buffer_size)
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
