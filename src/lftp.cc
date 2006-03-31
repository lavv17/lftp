/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "trio.h"
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h> // for mkdir()
#include "xstring.h"
#include <fcntl.h>
#include <locale.h>

#include "xmalloc.h"
#include "alias.h"
#include "CmdExec.h"
#include "SignalHook.h"
#include "GetPass.h"
#include "history.h"
#include "log.h"
#include "DummyProto.h"
#include "ResMgr.h"
#include "LsCache.h"
#include "DirColors.h"
#include "IdNameCache.h"
#include "LocalDir.h"
#include "ConnectionSlot.h"
#include "misc.h"

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

ResDecl res_save_cwd_history
   ("cmd:save-cwd-history","yes",ResMgr::BoolValidate,ResMgr::NoClosure);
ResDecl res_save_rl_history
   ("cmd:save-rl-history","yes",ResMgr::BoolValidate,ResMgr::NoClosure);

class ReadlineFeeder : public CmdFeeder
{
   bool tty:1;
   bool ctty:1;
   bool add_newline:1;
   char *to_free;
   int eof_count;
   char *for_history;

   static bool readline_inited;
   void readline_init()
   {
      if(readline_inited)
	 return;
      readline_inited=true;
      lftp_readline_init();
      lftp_rl_read_history();
      if(for_history)
      {
	 lftp_add_history_nodups(for_history);
	 xfree(for_history);
	 for_history=0;
      }
   }

public:
   ReadlineFeeder(const ArgV *args)
   {
      tty=isatty(0);
      ctty=(tcgetpgrp(0)!=(pid_t)-1);
      add_newline=false;
      to_free=0;
      eof_count=0;
      for_history=0;
      if(args && args->count()>1)
	 for_history=args->CombineQuoted();
   }
   virtual ~ReadlineFeeder()
   {
      if(readline_inited)
      {
	 if(res_save_cwd_history.QueryBool(0))
	    cwd_history.Save();
	 if(res_save_rl_history.QueryBool(0))
	    lftp_rl_write_history();
      }
      xfree(to_free);
      xfree(for_history);
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
	 readline_init();

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
      else // not a tty
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

      if(cmd_buf && last_char(cmd_buf)!='\n')
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
bool ReadlineFeeder::readline_inited;

static void sig_term(int sig)
{
   time_t t=time(0);
   printf(_("[%lu] Terminated by signal %d. %s"),(unsigned long)getpid(),sig,ctime(&t));
   exit(1);
}

static void move_to_background()
{
   // notify jobs
   Job::lftpMovesToBackground_ToAll();
   // wait they do something, but no more than 1 sec.
   SMTask::RollAll(1);
   // if all jobs terminated, don't really move to bg.
   if(Job::NumberOfJobs()==0)
      return;

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

      const char *home=get_lftp_home();
      if(home)
      {
	 char *log=(char*)alloca(strlen(home)+1+3+1);
	 sprintf(log,"%s",home);
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
	 Log::global->ShowPID();
	 Log::global->ShowTime();
	 Log::global->ShowContext();
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

int lftp_slot(int count,int key)
{
   if(!top_exec)
      return 0;
   char slot[2];
   slot[0]=key;
   slot[1]=0;
   top_exec->ChangeSlot(slot);
   lftp_rl_set_prompt(top_exec->MakePrompt());
   return 0;
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
   if(top_exec)
      top_exec->pre_stdout();
}

int   main(int argc,char **argv)
{
#ifdef SOCKS4
   SOCKSinit(argv[0]);
#endif

   setlocale (LC_ALL, "");
   setlocale (LC_NUMERIC, "C");
   bindtextdomain (PACKAGE, LOCALEDIR);
   textdomain (PACKAGE);

   // order is significant.
   SignalHook::ClassInit();
   ResMgr::ClassInit();
   FileAccess::ClassInit();

   hook_signals();

   LocalDirectory *cwd=new LocalDirectory;
   cwd->SetFromCWD();
   top_exec=new CmdExec(new DummyProto(),cwd);
   top_exec->SetStatusLine(new StatusLine(1));
   Log::global->SetCB(tty_clear);

   source_if_exist(top_exec,SYSCONFDIR"/lftp.conf");

   const char *home=getenv("HOME");
   if(home)
   {
      char *rc=(char*)alloca(strlen(home)+9+1);

      sprintf(rc,"%s/.lftprc",home);
      source_if_exist(top_exec,rc);
   }

   home=get_lftp_home();
   if(home)
   {
      char *rc=(char*)alloca(strlen(home)+3+1);

      sprintf(rc,"%s/rc",home);
      source_if_exist(top_exec,rc);
   }

   WaitDone(top_exec);

   top_exec->SetTopLevel();
   top_exec->Fg();

   ArgV *args=new ArgV(argc,argv);
   args->setarg(0,"lftp");

   lftp_feeder=new ReadlineFeeder(args);

   top_exec->ExecParsed(args);

   WaitDone(top_exec);

   int exit_code=top_exec->ExitCode();

   top_exec->AtExit();
   WaitDone(top_exec);

   if(Job::NumberOfJobs()>0)
   {
      top_exec->SetInteractive(false);
      move_to_background();
   }
   top_exec->KillAll();
   SMTask::Delete(top_exec);
   top_exec=0;
   Job::Cleanup();
   ConnectionSlot::Cleanup();
   SessionPool::ClearAll();
   LsCache::Flush();
   ProcWait::DeleteAll();
   DirColors::DeleteInstance();
   IdNameCacheCleanup();
   SignalHook::Cleanup();
   Log::Cleanup();
   SMTask::Cleanup();

   // the tasks left: LsCache::ExpireHelper, lftp_ssl_instance(if created)
   int task_count=SMTask::TaskCount();
   if(task_count>2)
      printf("WARNING: task_count=%d\n",task_count);

   return exit_code;
}
