/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "modconfig.h"

#include "trio.h"
#include <sys/types.h>
#include <sys/stat.h> // for mkdir()
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <locale.h>
#include <ctype.h>

#include "xstring.h"
#include "xmalloc.h"
#include "alias.h"
#include "CmdExec.h"
#include "SignalHook.h"
#include "GetPass.h"
#include "History.h"
#include "log.h"
#include "DummyProto.h"
#include "ResMgr.h"
#include "LsCache.h"
#include "IdNameCache.h"
#include "LocalDir.h"
#include "ConnectionSlot.h"
#include "misc.h"
#include "ArgV.h"
#include "attach.h"

#include "configmake.h"

#include "lftp_rl.h"
#include "complete.h"

CDECL_BEGIN
#include <glob.h>
CDECL_END

#define top_exec CmdExec::top

void  hook_signals()
{
   SignalHook::DoCount(SIGHUP);
   SignalHook::Ignore(SIGTTOU);
   ProcWait::Signal(true);
}

ResDecl res_save_cwd_history
   ("cmd:save-cwd-history","yes",ResMgr::BoolValidate,ResMgr::NoClosure);
ResDecl res_save_rl_history
   ("cmd:save-rl-history","yes",ResMgr::BoolValidate,ResMgr::NoClosure);
ResDecl res_stifle_rl_history
   ("cmd:stifle-rl-history","500",ResMgr::UNumberValidate,ResMgr::NoClosure);

class ReadlineFeeder : public CmdFeeder, private ResClient
{
   bool tty:1;
   bool ctty:1;
   bool add_newline:1;
   int eof_count;
   xstring_c cmd_buf;
   xstring_c for_history;

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
	 for_history.set(0);
      }
      Reconfig(0);
   }

public:
   ReadlineFeeder(const ArgV *args)
   {
      tty=isatty(0);
      ctty=(tcgetpgrp(0)!=(pid_t)-1);
      add_newline=false;
      eof_count=0;
      if(args && args->count()>1)
	 args->CombineQuotedTo(for_history);
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
   }
   bool IsInteractive() const
   {
      return tty;
   }
   bool RealEOF()
   {
      return !tty || eof_count>3;
   }

   const char *NextCmd(class CmdExec *exec,const char *prompt)
   {
      if(add_newline)
      {
	 add_newline=false;
	 return "\n";
      }

      ::completion_shell=exec;
      ::remote_completion=exec->remote_completion;

      if(tty)
      {
	 readline_init();

	 if(ctty) // controlling terminal
	 {
	    if(!in_foreground_pgrp())
	    {
	       // looks like we are in background. Can't read from tty
	       exec->Timeout(500);
	       return "";
	    }
	 }

	 SignalHook::ResetCount(SIGINT);
	 cmd_buf.set_allocated(lftp_readline(prompt));
	 xmalloc_register_block(cmd_buf.get_non_const());
	 if(cmd_buf && *cmd_buf)
	 {
	    if(exec->csh_history)
	    {
	       char *history_value0=0;
	       int expanded = lftp_history_expand (cmd_buf, &history_value0);
	       if (expanded)
	       {
		  if(history_value0)
		     xmalloc_register_block(history_value0);
		  xstring_ca history_value(history_value0);

		  if (expanded < 0)
		     fprintf (stderr, "%s\n", history_value.get());

		  /* If there was an error, return nothing. */
		  if (expanded < 0 || expanded == 2)	/* 2 == print only */
		  {
		     exec->Timeout(0);  // and retry immediately
		     return "";
		  }

		  cmd_buf.move_here(history_value);
	       }
	    }
	    lftp_add_history_nodups(cmd_buf);
	 }
	 else if(cmd_buf==0 && exec->interactive)
	    puts("exit");

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
	 cmd_buf.set_allocated(readline_from_file(0));
      }

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
   void Reconfig(const char *) {
      lftp_rl_history_stifle(res_stifle_rl_history.Query(0));
   }
};
bool ReadlineFeeder::readline_inited;

#define args	  (parent->args)
#define exit_code (parent->exit_code)
#define output	  (parent->output)
#define session	  (parent->session)
#define eprintf	  parent->eprintf
CMD(history)
{
   enum { READ, WRITE, CLEAR, LIST } mode = LIST;
   const char *fn = NULL;
   static struct option history_options[]=
   {
      {"read",required_argument,0,'r'},
      {"write",required_argument,0,'w'},
      {"clear",no_argument,0,'c'},
      {"list",required_argument,0,'l'},
      {0,0,0,0}
   };

   exit_code=0;
   int opt;
   while((opt=args->getopt_long("+r:w:cl",history_options,0))!=EOF) {
      switch(opt) {
      case 'r':
	 mode = READ;
	 fn = optarg;
	 break;
      case 'w':
	 mode = WRITE;
	 fn = optarg;
	 break;
      case 'c':
	 mode = CLEAR;
	 break;
      case 'l':
	 mode = LIST;
	 break;
      case '?':
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   int cnt = 16;
   if(const char *arg = args->getcurr()) {
      if(!strcasecmp(arg, "all"))
	 cnt = -1;
      else if(isdigit((unsigned char)arg[0]))
	 cnt = atoi(arg);
      else {
	 eprintf(_("%s: %s - not a number\n"), args->a0(), args->getcurr());
	 exit_code=1;
	 return 0;
      }
   }

   switch(mode) {
   case READ:
      if(int err = lftp_history_read(fn)) {
	 eprintf("%s: %s: %s\n", args->a0(), fn, strerror(err));
	 exit_code=1;
      }
      break;

   case WRITE:
      if(int err = lftp_history_write(fn)) {
	 eprintf("%s: %s: %s\n", args->a0(), fn, strerror(err));
	 exit_code=1;
      }
      break;

   case LIST:
      lftp_history_list(cnt);
      break;
   case CLEAR:
      lftp_history_clear();
      break;
   }

   return 0;
}
CMD(attach)
{
   const char *pid_s=args->getarg(1);
   if(!pid_s) {
      xstring& path=AcceptTermFD::get_sock_path(1);
      path.rtrim('1');
      path.append('*');
      glob_t g;
      glob(path, 0, NULL, &g);
      for(size_t i=0; i<g.gl_pathc; i++) {
	 const char *sock_path=g.gl_pathv[i];
	 pid_s=strrchr(sock_path,'-');
	 if(!pid_s)
	    continue;
	 pid_s++;
	 int p=atoi(pid_s);
	 if(p<=1) {
	    pid_s=0;
	    continue;
	 }
	 if(kill(p,0)==-1) {
	    if(errno==ESRCH) {
	       eprintf("%s: removing stale socket `%s'.\n",args->a0(),sock_path);
	       if(unlink(sock_path)==-1)
		  eprintf("%s: unlink(%s): %s\n",args->a0(),sock_path,strerror(errno));
	    }
	    pid_s=0;
	    continue;
	 }
	 pid_s=alloca_strdup(pid_s);
	 break;
      }
      globfree(&g);
      if(!pid_s) {
	 eprintf("%s: no backgrounded lftp processes found.\n",args->a0());
	 return 0;
      }
   }
   int pid=atoi(pid_s);
   SMTaskRef<SendTermFD> term_sender(new SendTermFD(pid));
   while(!term_sender->Done()) {
      SMTask::Schedule();
      SMTask::Block();
   }
   exit_code=0;
   if(term_sender->Failed()) {
      eprintf("%s\n",term_sender->ErrorText());
      exit_code=1;
   }
   return 0;
}
#undef args
#undef exit_code
#undef output
#undef session
#undef eprintf


static void sig_term(int sig)
{
   printf(_("[%u] Terminated by signal %d. %s\n"),(unsigned)getpid(),sig,SMTask::now.IsoDateTime());
   if(top_exec) {
      top_exec->KillAll();
      alarm(30);
      while(Job::NumberOfJobs()>0) {
	 SMTask::Schedule();
	 SMTask::Block();
      }
   }
   exit(1);
}

static void detach()
{
   SignalHook::Ignore(SIGINT);
   SignalHook::Ignore(SIGHUP);
   SignalHook::Ignore(SIGTSTP);

   const char *home=get_lftp_data_dir();
   if(home)
   {
      xstring& log=xstring::get_tmp(home);
      if(access(log,F_OK)==-1)
	 log.append("_log");
      else
	 log.append("/log");

      int fd=open(log,O_WRONLY|O_APPEND|O_CREAT,0600);
      if(fd>=0)
      {
	 dup2(fd,1); // stdout
	 dup2(fd,2); // stderr
	 if(fd!=1 && fd!=2)
	    close(fd);
      }
      const char *c="debug";
      ResMgr::Set("log:show-pid",c,"yes");
      ResMgr::Set("log:show-time",c,"yes");
      ResMgr::Set("log:show-ctx",c,"yes");
   }
   close(0);	  // close stdin.
   open("/dev/null",O_RDONLY); // reopen it, just in case.

#ifdef HAVE_SETSID
   setsid();	  // start a new session.
#endif

   SignalHook::Handle(SIGTERM,sig_term);
}

static void move_to_background()
{
   // notify jobs
   Job::lftpMovesToBackground_ToAll();
   // wait they do something, but no more than 1 sec.
   SMTask::RollAll(TimeInterval(1,0));
   // if all jobs terminated, don't really move to bg.
   if(Job::NumberOfJobs()==0)
      return;

   top_exec->AtBackground();
   top_exec->WaitDone();
   if(Job::NumberOfJobs()==0)
      return;

   fflush(stdout);
   fflush(stderr);

   pid_t pid=fork();
   switch(pid)
   {
   case(0): // child
   {
      pid=getpid();
      detach();
      printf(_("[%u] Started.  %s\n"),(unsigned)pid,SMTask::now.IsoDateTime());
      SMTaskRef<AcceptTermFD> term_acceptor(new AcceptTermFD());
      for(;;)
      {
	 SMTask::Schedule();
	 if(Job::NumberOfJobs()==0)
	    break;
	 SMTask::Block();
	 if(term_acceptor->Accepted()) {
	    hook_signals();
	    top_exec->SetInteractive();
	    top_exec->SetStatusLine(new StatusLine(1));
	    top_exec->SetCmdFeeder(new ReadlineFeeder(0));
	    for(;;)
	    {
	       SMTask::Schedule();
	       if(top_exec->Done() || term_acceptor->Detached()) {
		  if(Job::NumberOfJobs()>0) {
		     printf(_("[%u] Detaching from the terminal to complete transfers...\n"),(unsigned)pid);
		  } else if(top_exec->Done()) {
		     printf(_("[%u] Exiting and detaching from the terminal.\n"),(unsigned)pid);
		  }
		  fflush(stdout);
		  term_acceptor->Detach();
		  detach();
		  printf(_("[%u] Detached from terminal. %s\n"),(unsigned)pid,SMTask::now.IsoDateTime());
		  break;
	       }
	       SMTask::Block();
	    }
	 }
      }
      top_exec->AtExitBg();
      top_exec->AtTerminate();
      top_exec->WaitDone();
      printf(_("[%u] Finished. %s\n"),(unsigned)pid,SMTask::now.IsoDateTime());
      break;
   }
   default: // parent
      printf(_("[%u] Moving to background to complete transfers...\n"),
	       (unsigned)pid);
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

void  source_if_exist(const char *rc)
{
   if(access(rc,R_OK)!=-1)
   {
      top_exec->FeedCmd("source ");
      top_exec->FeedCmd(rc);
      top_exec->FeedCmd("\n");
   }
}

static void tty_clear()
{
   if(top_exec)
      top_exec->pre_stdout();
}

// look for the option, remove it and return true if found
static bool pick_option(int& argc,char **argv,const char *option)
{
   for(int i=1; i<argc; i++) {
      if(!strcmp(argv[i],option)) {
	 // remove the option, move trailing NULL too.
	 memmove(argv+i,argv+i+1,(argc-i)*sizeof(*argv));
	 argc--;
	 return true;
      }
      if(!strcmp(argv[i],"--"))	 // end of options
	 break;
   }
   return false;
}

char *program_name;

int   main(int argc,char **argv)
{
   program_name=argv[0];

#ifdef SOCKS4
   SOCKSinit(program_name);
#endif

   setlocale (LC_ALL, "");
   setlocale (LC_NUMERIC, "C");
   bindtextdomain (PACKAGE, LOCALEDIR);
   textdomain (PACKAGE);

   CmdExec::RegisterCommand("history",cmd_history,
	 N_("history -w file|-r file|-c|-l [cnt]"),
	 N_(" -w <file> Write history to file.\n"
	 " -r <file> Read history from file; appends to current history.\n"
	 " -c  Clear the history.\n"
	 " -l  List the history (default).\n"
	 "Optional argument cnt specifies the number of history lines to list,\n"
	 "or \"all\" to list all entries.\n"));
   CmdExec::RegisterCommand("attach",cmd_attach,"attach [PID]",
      N_("Attach the terminal to specified backgrounded lftp process.\n"));

   top_exec=new CmdExec(0,0);
   hook_signals();
   top_exec->SetStatusLine(new StatusLine(1));
   Log::global=new Log("debug");
   Log::global->SetCB(tty_clear);

   source_if_exist(SYSCONFDIR"/lftp.conf");

   if(!pick_option(argc,argv,"--norc")) {
      const char *home=getenv("HOME");
      if(home)
	 source_if_exist(dir_file(home,".lftprc"));
      home=get_lftp_config_dir();
      if(home)
	 source_if_exist(dir_file(home,"rc"));
   }

   top_exec->WaitDone();
   top_exec->SetTopLevel();
   top_exec->Fg();

   Ref<ArgV> args(new ArgV(argc,argv));
   args->setarg(0,"lftp");

   lftp_feeder=new ReadlineFeeder(args);

   top_exec->ExecParsed(args.borrow());
   top_exec->WaitDone();
   int exit_code=top_exec->ExitCode();

   top_exec->AtExit();
   top_exec->WaitDone();

   if(Job::NumberOfJobs()>0)
   {
      top_exec->SetInteractive(false);
      move_to_background();
   }
   else
   {
      top_exec->AtExitFg();
      top_exec->AtTerminate();
      top_exec->WaitDone();
   }
   top_exec->KillAll();
   top_exec=0;

   Job::Cleanup();
   ConnectionSlot::Cleanup();
   SessionPool::ClearAll();
   FileAccess::ClassCleanup();
   ProcWait::DeleteAll();
   IdNameCacheCleanup();
   SignalHook::Cleanup();
   Log::Cleanup();
   SMTask::Cleanup();
   return exit_code;
}
