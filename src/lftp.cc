/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "modconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h> // for mkdir()
#include "xstring.h"
#include <fcntl.h>
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#include "xalloca.h"
#include "ftpclass.h"
#ifndef MODULE_PROTO_FILE
# include "LocalAccess.h"
#endif
#ifndef MODULE_PROTO_HTTP
# include "Http.h"
#endif
#include "xmalloc.h"
#include "alias.h"
#include "CmdExec.h"
#include "SignalHook.h"
#include "GetPass.h"
#include "history.h"
#include "log.h"
#include "DummyProto.h"
#include "Resolver.h"
#include "ResMgr.h"

#include "confpaths.h"

#include "lftp_rl.h"
#include "complete.h"


CmdExec	 *top_exec;


void  hook_signals()
{
   SignalHook::DoCount(SIGHUP);
   SignalHook::Ignore(SIGTTOU);
   ProcWait::Signal(true);
}

void WaitDone(CmdExec *exec)
{
   for(;;)
   {
      SMTask::Schedule();
      if(exec->Done())
	 break;
      SMTask::Block();
   }
}

class ReadlineFeeder : public CmdFeeder
{
   bool tty:1;
   bool ctty:1;
   bool add_newline:1;
   char *to_free;
   int eof_count;
public:
   ReadlineFeeder()
   {
      tty=isatty(0);
      ctty=(tcgetpgrp(0)!=(pid_t)-1);
      to_free=0;
      eof_count=0;
   }
   virtual ~ReadlineFeeder()
   {
      xfree(to_free);
   }
   bool RealEOF()
   {
      return !tty || eof_count>3;
   }

   const char *NextCmd(class CmdExec *exec,const char *prompt)
   {
      xfree(to_free);
      to_free=0;

      if(add_newline)
      {
	 add_newline=false;
	 return "\n";
      }

      ::completion_shell=exec;
      ::remote_completion=exec->remote_completion;

      char *cmd_buf;
      if(tty)
      {
	 if(ctty) // controlling terminal
	 {
	    pid_t term_pg=tcgetpgrp(0);
	    if(term_pg!=(pid_t)-1 && getpgrp()!=term_pg)
	    {
	       // looks like we are in background. Can't read from tty
	       exec->Timeout(500);
	       return "";
	    }
	 }

	 SignalHook::ResetCount(SIGINT);
	 cmd_buf=lftp_readline(prompt);
	 if(cmd_buf && *cmd_buf)
	 {
	    if(exec->csh_history)
	    {
	       char *history_value;
	       int expanded = lftp_history_expand (cmd_buf, &history_value);

	       if (expanded)
	       {
		  if (expanded < 0);
		     fprintf (stderr, "%s\n", history_value);

		  /* If there was an error, return nothing. */
		  if (expanded < 0 || expanded == 2)	/* 2 == print only */
		  {
		     exec->Timeout(0);  // and retry immediately
		     xfree(history_value);
		     return "";
		  }

		  to_free=history_value;
		  cmd_buf=history_value;
	       }
	    }
	    lftp_add_history_nodups(cmd_buf);
	 }
	 else if(cmd_buf==0 && exec->interactive)
	    puts("exit");
	 xmalloc_register_block(cmd_buf);

	 if(cmd_buf==0)
	    eof_count++;
	 else
	    eof_count=0;
      }
      else
      {
	 if(exec->interactive)
	 {
	    while(*prompt)
	    {
	       char ch=*prompt++;
	       if(ch!=1 && ch!=2)
		  putchar(ch);
	    }
	    fflush(stdout);
	 }
	 cmd_buf=readline_from_file(stdin);
      }
      to_free=cmd_buf;

      ::completion_shell=0;

      if(cmd_buf &&
	    (*cmd_buf==0 || cmd_buf[strlen(cmd_buf)-1]!='\n'))
      {
	 exec->Timeout(0);
	 add_newline=true;
      }
      return cmd_buf;
   }
   void clear()
      {
	 if(!tty)
	    return;
	 lftp_rl_clear();
      }
};

static void sig_term(int sig)
{
   time_t t=time(0);
   printf(_("[%lu] Terminated by signal %d. %s"),(unsigned long)getpid(),sig,ctime(&t));
   exit(1);
}

static void move_to_background()
{
   fflush(stdout);
   fflush(stderr);
   pid_t pid=fork();
   switch(pid)
   {
   case(0): // child
   {
      SignalHook::Ignore(SIGINT);
      SignalHook::Ignore(SIGQUIT);
      SignalHook::Ignore(SIGHUP);
      SignalHook::Ignore(SIGTSTP);

      const char *home=getenv("HOME");
      if(home)
      {
	 char *log=(char*)alloca(strlen(home)+1+9+1);
	 sprintf(log,"%s/.lftp",home);
	 if(access(log,F_OK)==-1)
	    strcat(log,"_log");
	 else
	    strcat(log,"/log");

	 int fd=open(log,O_WRONLY|O_APPEND|O_CREAT,0600);
	 if(fd>=0)
	 {
	    dup2(fd,1); // stdout
	    dup2(fd,2); // stderr
	    if(fd!=1 && fd!=2)
	       close(fd);
	 }
      }
      close(0);	  // close stdin.
      open("/dev/null",O_RDONLY); // reopen it, just in case.

#ifdef HAVE_SETSID
      setsid();	  // start a new session.
#endif

      pid=getpid();
      time_t t=time(0);
      SignalHook::Handle(SIGTERM,sig_term);
      printf(_("[%lu] Started.  %s"),(unsigned long)pid,ctime(&t));
      for(;;)
      {
	 SMTask::Schedule();
	 if(Job::NumberOfJobs()==0)
	    break;
	 SMTask::Block();
      }
      t=time(0);
      printf(_("[%lu] Finished. %s"),(unsigned long)pid,ctime(&t));
      return;
   }
   default: // parent
      printf(_("[%lu] Moving to background to complete transfers...\n"),
	       (unsigned long)pid);
      fflush(stdout);
      _exit(0);
   case(-1):
      perror("fork()");
   }
}

void  source_if_exist(CmdExec *exec,const char *rc)
{
   if(access(rc,R_OK)!=-1)
   {
      exec->FeedCmd("source ");
      exec->FeedCmd(rc);
      exec->FeedCmd("\n");
   }
}

static void tty_clear()
{
   top_exec->pre_stdout();
}

int   main(int argc,char **argv)
{
#ifdef SOCKS4
   SOCKSinit(argv[0]);
#endif

   setlocale (LC_ALL, "");
   bindtextdomain (PACKAGE, LOCALEDIR);
   textdomain (PACKAGE);

   ResMgr::ClassInit(); // resources must be inited before other classes
   SignalHook::ClassInit();
   Resolver::ClassInit();

#ifndef MODULE_PROTO_FILE
   LocalAccess::ClassInit();
#endif
#ifndef MODULE_PROTO_HTTP
   Http::ClassInit();
#endif
#ifndef MODULE_PROTO_FTP
   Ftp::ClassInit();
#endif

   lftp_readline_init();

   hook_signals();

   top_exec=new CmdExec(new DummyProto());
   top_exec->status_line=new StatusLine(1);
   Log::global=new Log();
   Log::global->SetCB(tty_clear);

   source_if_exist(top_exec,SYSCONFDIR"/lftp.conf");

   const char *home=getenv("HOME");
   if(home)
   {
      char *rc=(char*)alloca(strlen(home)+9+1);

      // create lftp own directory
      sprintf(rc,"%s/.lftp",home);
      mkdir(rc,0755);

      sprintf(rc,"%s/.lftprc",home);
      source_if_exist(top_exec,rc);
      sprintf(rc,"%s/.lftp/rc",home);
      source_if_exist(top_exec,rc);
   }

   WaitDone(top_exec);

   top_exec->SetTopLevel();
   top_exec->Fg();

   ArgV *args=new ArgV(argc,argv);
   args->setarg(0,"lftp");
   if(args->count()>1)
   {
      char *line=args->CombineQuoted();
      lftp_add_history_nodups(line);
      xfree(line);
   }
   lftp_feeder=new ReadlineFeeder;
   top_exec->ExecParsed(args);

   WaitDone(top_exec);

   top_exec->AtExit();
   WaitDone(top_exec);

   cwd_history.Save();

   if(Job::NumberOfJobs()>0)
   {
      top_exec->SetInteractive(false);
      move_to_background();
   }
   return top_exec->ExitCode();
}
