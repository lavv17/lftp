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

#include "Resolver.h"
#include "SignalHook.h"
#include <errno.h>
#include <unistd.h>
#include "xstring.h"
#include <stdio.h>
#include <time.h>
#include "xmalloc.h"
#include "ResMgr.h"

#if !defined(HAVE_HSTRERROR_DECL)
extern "C" { const char *hstrerror(int); }
#endif

static ResDecl
   res_timeout	  ("dns:timeout",      "0", ResMgr::UNumberValidate,0);

Resolver::Resolver(const char *h,int p)
{
   hostname=xstrdup(h);
   port=p;

   pipe_to_child[0]=pipe_to_child[1]=-1;
   w=0;
   memset(&sa,0,sizeof(sa));
   err_msg=0;
   done=false;
   timeout=0;
   Reconfig();
   time(&start_time);
}

Resolver::~Resolver()
{
   if(pipe_to_child[0]!=-1)
      close(pipe_to_child[0]);
   if(pipe_to_child[1]!=-1)
      close(pipe_to_child[1]);

   if(hostname)
      free(hostname);
   if(w)
   {
      w->Kill(SIGKILL);
      w->Auto();
   }
   if(err_msg)
      free(err_msg);
}

int   Resolver::Do()
{
   if(done)
      return STALL;

   int m=STALL;

   if(pipe_to_child[0]==-1)
   {
      int res=pipe(pipe_to_child);
      if(res==-1)
      {
	 if(errno==ENFILE || errno==EMFILE)
	 {
	    block+=TimeOut(1000);
	    return m;
	 }
	 MakeErrMsg("pipe()");
	 return MOVED;
      }
      m=MOVED;
   }

   if(!w)
   {
      pid_t proc=fork();
      if(proc==-1)
      {
	 block+=TimeOut(1000);
	 return m;
      }
      if(proc==0)
      {	 // child
	 SignalHook::Ignore(SIGINT);
	 SignalHook::Ignore(SIGTSTP);
	 SignalHook::Ignore(SIGQUIT);
	 SignalHook::Ignore(SIGHUP);
	 close(0);
	 close(2);
	 dup2(pipe_to_child[1],1);
	 close(pipe_to_child[1]);
	 close(pipe_to_child[0]);
	 DoGethostbyname();
	 _exit(0);
      }
      // parent
      close(pipe_to_child[1]);
      pipe_to_child[1]=-1;

      w=new ProcWait(proc);
   }

   struct pollfd pfd={pipe_to_child[0],POLLIN};
   int res=poll(&pfd,1,0);
   if(res!=1)
   {
      if(timeout>0)
      {
	 if(now-start_time > timeout)
	 {
	    err_msg=xstrdup(_("host name resolve timeout"));
	    done=true;
	    return MOVED;
	 }
	 block+=TimeOut((timeout-(now-start_time))*1000);
      }
      block+=PollVec(pipe_to_child[0],POLLIN);
      return m;
   }
   char c;
   res=read(pipe_to_child[0],&c,1);
   if(res<0)
   {
   read_error:
      MakeErrMsg("read(pipe)");
      return MOVED;
   }
   if(res<1)
      goto proto_error;
   if(c=='E') // error
   {
      char buf[512];
      res=read(pipe_to_child[0],buf,sizeof(buf)-1);
      if(res<0)
	 goto read_error;
      buf[res]=0;
      err_msg=(char*)xmalloc(strlen(hostname)+res+3);
      sprintf(err_msg,"%s: %s",hostname,buf);
      done=true;
      return MOVED;
   }
   res=read(pipe_to_child[0],&sa,sizeof(sa));
   if(res<0)
      goto read_error;
   if((unsigned)res<sizeof(sa))
   {
   proto_error:
      // protocol error
      err_msg=xstrdup(_("child returned invalid data"));
      done=true;
      return MOVED;
   }
   done=true;
   return MOVED;
}

void Resolver::MakeErrMsg(const char *f)
{
   const char *e=strerror(errno);
   err_msg=(char*)xmalloc(strlen(e)+strlen(f)+3);
   sprintf(err_msg,"%s: %s",f,e);
   done=true;
}

void Resolver::DoGethostbyname()
{
   time_t try_time;
#ifndef HAVE_H_ERRNO_DECL
   extern int h_errno;
#endif
   for(;;)
   {
      time(&try_time);
      struct hostent *ha=gethostbyname(hostname);
      if(ha)
      {
	 sa.sin_family=ha->h_addrtype;
	 memcpy(&sa.sin_addr,ha->h_addr,ha->h_length);
	 sa.sin_port=htons(port);
	 write(1,"O",1);
	 write(1,&sa,sizeof(sa));
	 return;
      }
      if(ha==NULL
#ifdef HAVE_H_ERRNO
	 && h_errno!=TRY_AGAIN
#endif
	)
      {
	 write(1,"E",1);
	 const char *e=
#ifdef HAVE_H_ERRNO
	    hstrerror(h_errno);
#else
	    "Host name lookup failure";
#endif
	 write(1,e,strlen(e));
	 return;
      }
      time_t t=time(0);
      if(t-try_time<5)
	 sleep(5-(t-try_time));
   }
}

void Resolver::Reconfig()
{
   timeout = res_timeout.Query(0);
}
