/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#include <fcntl.h>
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#include "xalloca.h"
#include "ftpclass.h"
#include "LocalAccess.h"
#include "lftp.h"
#include "xmalloc.h"
#include "alias.h"
#include "CmdExec.h"
#include "SignalHook.h"
#include "GetPass.h"
#include "history.h"
#include "log.h"

#include "confpaths.h"

extern "C" {
#include "readline/readline.h"
#include "readline/history.h"
}

int   remote_completion=0;

void  hook_signals()
{
   SignalHook::DoCount(SIGHUP);
   SignalHook::DoCount(SIGPIPE);
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
   void *to_free;
public:
   ReadlineFeeder()
   {
      tty=isatty(0);
      ctty=(tcgetpgrp(0)!=(pid_t)-1);
      to_free=0;
   }
   virtual ~ReadlineFeeder()
   {
      xfree(to_free);
   }

   char *NextCmd(class CmdExec *exec,const char *prompt)
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
	       exec->block+=TimeOut(500);
	       return "";
	    }
	 }

	 SignalHook::ResetCount(SIGINT);
	 cmd_buf=readline(prompt);
	 if(cmd_buf && *cmd_buf)
	 {
	    if(exec->csh_history)
	    {
	       char *history_value;
	       int expanded = history_expand (cmd_buf, &history_value);

	       if (expanded)
	       {
		  if (expanded < 0);
		     fprintf (stderr, "%s\n", history_value);

		  /* If there was an error, return nothing. */
		  if (expanded < 0 || expanded == 2)	/* 2 == print only */
		  {
		     exec->block+=NoWait();  // and retry immediately
		     xfree(history_value);
		     return "";
		  }

		  cmd_buf=history_value;
		  to_free=cmd_buf;
	       }
	    }
	    using_history();
	    HIST_ENTRY *temp=previous_history();
	    if(temp==0 || strcmp(temp->line,cmd_buf))
	       add_history(cmd_buf);
	    using_history();
	 }
	 else if(cmd_buf==0 && exec->interactive)
	    puts("exit");
      }
      else
      {
	 cmd_buf=readline_from_file(stdin);
	 to_free=cmd_buf;
      }

      ::completion_shell=0;

      if(cmd_buf &&
	    (*cmd_buf==0 || cmd_buf[strlen(cmd_buf)-1]!='\n'))
      {
	 exec->block+=NoWait();
	 add_newline=true;
      }
      return cmd_buf;
   }
};

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

      char *home=getenv("HOME");
      if(!home) home=".";

      char *log=(char*)alloca(strlen(home)+1+9+1);
      sprintf(log,"%s/.lftp",home);
      if(access(log,F_OK)==-1)
	 strcat(log,"_log");
      else
	 strcat(log,"/log");

      int fd=open(log,O_WRONLY|O_APPEND|O_CREAT,0600);
      if(fd>=0)
      {
	 dup2(fd,1);
	 dup2(fd,2);
	 if(fd!=1 && fd!=2)
	    close(fd);
      }
      pid=getpid();
      time_t t=time(0);
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

void  source_if_exist(CmdExec *exec,char *rc)
{
   if(access(rc,R_OK)!=-1)
   {
      exec->FeedCmd("source ");
      exec->FeedCmd(rc);
      exec->FeedCmd("\n");
   }
}

int   main(int argc,char **argv)
{
   setlocale (LC_ALL, "");
   bindtextdomain (PACKAGE, LOCALEDIR);
   textdomain (PACKAGE);

   LocalAccess::ClassInit();
   Ftp::ClassInit();

   char  *home=getenv("HOME")?:".";

   CmdExec *top_exec=new CmdExec(new Ftp());
   top_exec->jobno=-1;
   top_exec->status_line=new StatusLine(1);
   Log::global=new Log(top_exec->status_line);

   initialize_readline();

   hook_signals();

   source_if_exist(top_exec,SYSCONFDIR"/lftp.conf");

   char	 *rc=(char*)alloca(strlen(home)+9+1);

   // create lftp own directory
   sprintf(rc,"%s/.lftp",home);
   mkdir(rc,0755);

   sprintf(rc,"%s/.lftprc",home);
   source_if_exist(top_exec,rc);
   sprintf(rc,"%s/.lftp/rc",home);
   source_if_exist(top_exec,rc);

   WaitDone(top_exec);

   top_exec->Fg();

   ArgV *args=new ArgV(argc,argv);
   args->setarg(0,"lftp");
   if(args->count()>1)
   {
      char *line=args->Combine();
      add_history(line);
      free(line);
   }
   lftp_feeder=new ReadlineFeeder;
   top_exec->ExecParsed(args);

   WaitDone(top_exec);

   cwd_history.Save();

   if(Job::NumberOfJobs()>0)
   {
      top_exec->interactive=false;
      move_to_background();
   }
   return top_exec->ExitCode();
}
