/*
 * lftp and utils
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include "CmdExec.h"
#include "xmalloc.h"
#include "xstring.h"
#include "SignalHook.h"
#include "alias.h"
#include "misc.h"
#include "ResMgr.h"
#include "module.h"
#include "url.h"
#include "QueueFeeder.h"
#include "LocalDir.h"
#include "ConnectionSlot.h"
#include "DummyProto.h"

#define RL_PROMPT_START_IGNORE	'\001'
#define RL_PROMPT_END_IGNORE	'\002'

#define super SessionJob

static ResDecl
   res_default_proto	   ("cmd:default-protocol","ftp",0,0),
   res_long_running	   ("cmd:long-running",	"30",ResMgr::UNumberValidate,0),
   res_remote_completion   ("cmd:remote-completion","on",ResMgr::BoolValidate,0),
   res_prompt		   ("cmd:prompt",	"lftp \\S\\? \\u\\@\\h:\\w> ",0,0),
   res_default_title	   ("cmd:default-title","lftp \\h:\\w",0,0),
   res_default_ls	   ("cmd:ls-default",	"",0,0),
   res_csh_history	   ("cmd:csh-history",	"off",ResMgr::BoolValidate,ResMgr::NoClosure),
   res_verify_path	   ("cmd:verify-path",	"yes",ResMgr::BoolValidate,0),
   res_verify_path_cached  ("cmd:verify-path-cached","no",ResMgr::BoolValidate,0),
   res_verify_host	   ("cmd:verify-host",	"yes",ResMgr::BoolValidate,0),
   res_at_exit		   ("cmd:at-exit",	"",   0,0),
   res_fail_exit	   ("cmd:fail-exit",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_verbose		   ("cmd:verbose",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_interactive	   ("cmd:interactive",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_move_background	   ("cmd:move-background","yes", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_set_term_status     ("cmd:set-term-status","no", ResMgr::BoolValidate,0),
   res_term_status         ("cmd:term-status",  "", 0, 0),
   res_trace		   ("cmd:trace",  "no",	ResMgr::BoolValidate,ResMgr::NoClosure),
   res_parallel		   ("cmd:parallel",	   "1",  ResMgr::UNumberValidate,0),
   res_queue_parallel	   ("cmd:queue-parallel",  "1",	 ResMgr::UNumberValidate,0);

CmdExec	 *CmdExec::cwd_owner;
CmdExec	 *CmdExec::chain;
CmdExec  *CmdExec::top;

void  CmdExec::SaveCWD()
{
   if(!cwd)
      cwd=new LocalDirectory;
   cwd->SetFromCWD();
   if(cwd_owner==0)
      cwd_owner=this;
}
int  CmdExec::RestoreCWD()
{
   if(cwd_owner==this)
      return 0;
   if(cwd==0)
      return -1;
   const char *err=cwd->Chdir();
   if(!err)
   {
      cwd_owner=this;
      return 0;
   }

   const char *name=cwd->GetName();
   eprintf(_("Warning: chdir(%s) failed: %s\n"),name?name:"?",err);
   return -1;
}

void CmdExec::FeedCmd(const char *c)
{
   partial_cmd=false;
   start_time=now;
   cmd_buf.Put(c);
};

void CmdExec::PrependCmd(const char *c)
{
   start_time=now;

   int len=strlen(c);
   int nl=(len>0 && c[len-1]!='\n');

   if(nl)
      cmd_buf.Prepend("\n",1);
   cmd_buf.Prepend(c,len);

   if(alias_field>0)
      alias_field+=len+nl;
}

int CmdExec::find_cmd(const char *cmd_name,const struct cmd_rec **ret)
{
   int part=0;
   const cmd_rec *c;
   for(c=dyn_cmd_table?dyn_cmd_table:static_cmd_table; c->name; c++)
   {
      if(!strcasecmp(c->name,cmd_name))
      {
	 *ret=c;
	 return 1;
      }
      if(!strncasecmp(c->name,cmd_name,strlen(cmd_name)))
      {
	 part++;
	 *ret=c;
      }
   }

   if(part!=1)
      *ret=0;
   return part;
}

CMD(lcd);

void  CmdExec::exec_parsed_command()
{
   switch(condition)
   {
   case(COND_ANY):
      if(exit_code!=0 && res_fail_exit.QueryBool(0))
      {
	 while(!Done())
	    RemoveFeeder();
	 return;
      }
      break;
   case(COND_AND):
      if(exit_code!=0)
	 return;
      break;
   case(COND_OR):
      if(exit_code==0)
	 return;
      break;
   }

   prev_exit_code=exit_code;
   exit_code=1;

   if(interactive)
   {
      SignalHook::ResetCount(SIGINT);
      SignalHook::ResetCount(SIGHUP);
      SignalHook::ResetCount(SIGTSTP);
   }

   bool did_default=false;

   if(ResMgr::QueryBool("cmd:trace",0))
   {
      xstring_ca c(args->CombineQuoted());
      printf("+ %s\n",c.get());
   }

restart:

   const struct cmd_rec *c;
   const char *cmd_name=args->getarg(0);
   int part=find_cmd(cmd_name,&c);
   if(part<=0)
      eprintf(_("Unknown command `%s'.\n"),cmd_name);
   else if(part>1)
      eprintf(_("Ambiguous command `%s'.\n"),cmd_name);
   else
   {
      if(RestoreCWD()==-1)
      {
	 if(c->creator!=cmd_lcd)
	    return;
      }

      args->setarg(0,c->name); // in case it was abbreviated
      args->rewind();

      xstring_ca cmdline(args->Combine());   // save the cmdline
      Job *new_job=0;

      if(c->creator==0)
      {
	 if(did_default)
	 {
	    eprintf(_("Module for command `%s' did not register the command.\n"),cmd_name);
	    exit_code=1;
	    return;
	 }
	 new_job=default_cmd();
	 did_default=true;
      }
      else
      {
	 new_job=c->creator(this);
      }
      if(new_job==this || builtin)
      {
	 if(builtin==BUILTIN_EXEC_RESTART)
	 {
	    builtin=BUILTIN_NONE;
	    goto restart;
	 }
	 return;
      }
      if(new_job)
      {
	 if(new_job->jobno<0)
	    new_job->AllocJobno();
	 if(!new_job->cmdline)
	    new_job->cmdline.set_allocated(cmdline.borrow());
	 new_job->SetParentFg(this,!background);
      }
      AddWaiting(new_job);
      if(background)
      {
	 if(new_job)
	 {
	    Roll(new_job);
	    if(!new_job->Done())
	       SuspendJob(new_job);
	 }
      } // background
   }
}

void CmdExec::SuspendJob(Job *j)
{
   j->Bg();
   if(interactive)
      j->ListOneJob(0,0,"&");
   last_bg=j->jobno;
   exit_code=0;
   RemoveWaiting(j);
}

void CmdExec::ExecParsed(ArgV *a,FDStream *o,bool b)
{
   Enter();

   args=a;
   output=o;
   background=b;
   condition=COND_ANY;
   exec_parsed_command();

   Leave();
}

bool CmdExec::Idle()
{
   return(waiting_num==0 && (cmd_buf.Size()==0 || partial_cmd));
}

int CmdExec::Done()
{
   return(feeder==0 && Idle());
}

void CmdExec::RemoveFeeder()
{
   free_used_aliases();

   if(!feeder)
      return;

   // save old cwd if necessary
   if(interactive && feeder->prev==0)
      cwd_history.Set(session);

   cmd_buf.Empty();
   cmd_buf.Put(feeder->saved_buf);
   partial_cmd=false;
   if(feeder==queue_feeder)
      queue_feeder=0;
   delete replace_value(feeder,feeder->prev);
   Reconfig(0);
}

void CmdExec::ReuseSavedSession()
{
   Reuse(saved_session);
   saved_session=0;
}
void CmdExec::RevertToSavedSession()
{
   if(saved_session==0)
      return;
   ChangeSession(saved_session);
   saved_session=0;
}
void CmdExec::ChangeSlot(const char *n)
{
   if(!n || !*n)
   {
      slot.set(0);
      return;
   }
   FileAccess *s=ConnectionSlot::FindSession(n);
   if(!s)
      ConnectionSlot::Set(n,session);
   else
      ChangeSession(s->Clone());
   slot.set(n);
}

int CmdExec::Do()
{
   int m=STALL;

   if(builtin!=BUILTIN_NONE)
   {
      int res;
      switch(builtin)
      {
      case(BUILTIN_CD):
	 res=session->Done();
	 if(res==FA::OK)
	 {
	    // done
	    if(status_line)
	       status_line->Clear();
	    if(interactive || verbose)
	    {
	       const char *cwd=session->GetCwd();
	       eprintf(_("cd ok, cwd=%s\n"),cwd?cwd:"~");
	       cwd_history.Set(session,old_cwd);
	    }
	    if(slot)
	       ConnectionSlot::SetCwd(slot,session->GetCwd());
	    session->Close();
	    exit_code=0;
	    builtin=BUILTIN_NONE;
	    redirections=0;
	    beep_if_long();
	    return MOVED;
	 }
	 if(res<0)
	 {
	    if(res==FA::FILE_MOVED)
	    {
	       // cd to another url.
	       const char *loc_c=session->GetNewLocation();
	       int max_redirections=ResMgr::Query("xfer:max-redirections",0);
	       if(loc_c && max_redirections>0)
	       {
		  eprintf(_("%s: received redirection to `%s'\n"),"cd",loc_c);
		  if(++redirections>max_redirections)
		  {
		     eprintf("cd: %s\n",_("Too many redirections"));
		     goto cd_err_done;
		  }

		  char *loc=alloca_strdup(loc_c);
		  ParsedURL u(loc,true);
		  if(!u.proto)
		  {
		     bool is_file=(last_char(loc)!='/');
		     FileAccess::Path new_cwd(session->GetNewCwd());
		     new_cwd.Change(0,is_file,loc);
		     session->PathVerify(new_cwd);
		     Roll(session);
		     return MOVED;
		  }
		  session->Close();
		  exit_code=0;
		  builtin=BUILTIN_NONE;
		  char *cmd=string_alloca(6+3+2*strlen(loc));
		  strcpy(cmd,"open \"");
		  unquote(cmd+strlen(cmd),loc);
		  strcat(cmd,"\";");
		  PrependCmd(cmd);
	       	  return MOVED;
	       }
	    }
	    // error
	    if(status_line)
	       status_line->Clear();
	    eprintf("%s: %s\n",args->getarg(0),session->StrError(res));
	 cd_err_done:
	    session->Close();
	    builtin=BUILTIN_NONE;
	    redirections=0;
	    beep_if_long();
	    exit_code=1;
	    return MOVED;
	 }
	 break;

      case(BUILTIN_OPEN):
	 res=session->Done();
	 if(res==FA::OK)
	 {
	    if(status_line)
	       status_line->Clear();
	    session->Close();
	    ReuseSavedSession();
	    builtin=BUILTIN_NONE;
	    redirections=0;
	    beep_if_long();
	    exit_code=0;

	    return MOVED;
	 }
	 if(res<0)
	 {
	    if(status_line)
	       status_line->Clear();
	    eprintf("%s: %s\n",args->getarg(0),session->StrError(res));
	    session->Close();
	    RevertToSavedSession();
	    builtin=BUILTIN_NONE;
	    redirections=0;
	    beep_if_long();
	    exit_code=1;
	    return MOVED;
	 }
	 break;
      case(BUILTIN_GLOB):
	 if(glob->Error())
	 {
	    if(status_line)
	       status_line->Clear();
	    eprintf("%s: %s\n",args->getarg(0),glob->ErrorText());
	 }
	 else if(glob->Done())
	 {
	    FileSet &list=*glob->GetResult();
	    for(int i=0; list[i]; i++)
	       args_glob->Append(list[i]->name);
	 }
	 if(glob->Done() || glob->Error())
	 {
	    const char *pat=args->getnext();
	    if(!pat)
	    {
	       glob=0;
	       // it was last argument
	       args=args_glob;
	       builtin=BUILTIN_NONE;
	       redirections=0;
	       if(status_line)
		  status_line->Clear();
	       exit_code=prev_exit_code;
	       exec_parsed_command();
	       return MOVED;
	    }
	    glob->NewGlob(pat);
	    m=MOVED;
	 }
	 break;

      case(BUILTIN_NONE):
      case(BUILTIN_EXEC_RESTART):
	 abort(); // can't happen
      }
      if(interactive)
      {
	 if(SignalHook::GetCount(SIGINT))
	 {
	    if(status_line)
	       status_line->WriteLine(_("Interrupt"));
	    return AcceptSig(SIGINT);
	 }
	 if(SignalHook::GetCount(SIGTSTP))
	 {
	    if(builtin==BUILTIN_CD || builtin==BUILTIN_OPEN)
	    {
	       if(status_line)
		  status_line->Clear();
	       if(builtin==BUILTIN_CD)
		  session->ChdirAccept();
	       session->Close();
	       exit_code=0;
	       builtin=BUILTIN_NONE;
	       redirections=0;
	       return MOVED;
	    }
	    else
	    {
	       SignalHook::ResetCount(SIGTSTP);
	    }
	 }
	 if(SignalHook::GetCount(SIGHUP))
	 {
	    SetInteractive(false);
	    return MOVED;
	 }
      }
      if(status_line && status_line->CanShowNow())
	 ShowRunStatus(status_line);   // this is only for top level CmdExec.
      return m;
   }

   if(waiting_num>0)
   {
      Job *j;
      while((j=FindDoneAwaitedJob())!=0)
      {
	 j->Bg();
	 if(status_line)
	    status_line->Clear();
 	 if(interactive || verbose)
	    j->SayFinal(); // final phrase like 'rm succeed'
	 exit_code=j->ExitCode();
	 RemoveWaiting(j);
	 Delete(j);
	 beep_if_long();
      	 return MOVED;
      }
      if(interactive)
      {
	 if(SignalHook::GetCount(SIGINT))
	 {
	    for(int i=0; i<waiting_num; i++)
	       waiting[i]->Bg();
	    SignalHook::ResetCount(SIGINT);
	    if(status_line)
	       status_line->WriteLine(_("Interrupt"));
	    return AcceptSig(SIGINT);
	 }
	 if(SignalHook::GetCount(SIGTSTP))
	 {
	    while(waiting_num>0)
	       SuspendJob(waiting[0]);
	    return MOVED;
	 }
	 if(SignalHook::GetCount(SIGHUP))
	 {
	    SetInteractive(false);
	    return MOVED;
	 }
      }
      if(status_line && status_line->CanShowNow())
	 ShowRunStatus(status_line);   // this is only for top level CmdExec.
      if(m != STALL || interactive || !feeder || waiting_num >= max_waiting)
	 return m;
   }

   if(!interactive)
   {
      BuryDoneJobs();
      if(FindJob(last_bg)==0)
	 last_bg=-1;
   }

try_get_cmd:
   if(cmd_buf.Size()==0 || partial_cmd)
   {
      if(feeder)
      {
	 if(interactive && !partial_cmd)
	 {
	    ListDoneJobs();
	    BuryDoneJobs();
	    if(FindJob(last_bg)==0)
	       last_bg=-1;
	 }

	 if(status_line)
	 {
	    const char *def_title = FormatPrompt(res_default_title.Query(getenv("TERM")));
	    status_line->DefaultTitle(def_title);
	    status_line->Clear();
	 }

	 const char *prompt=MakePrompt();
	 feeder_called=true;
	 if(fg)
	    feeder->Fg();
	 const char *cmd=feeder->NextCmd(this,prompt);
	 feeder_called=false;
	 if(!cmd)
	 {
	    if(cmd_buf.Size()>0 && partial_cmd)
	    {
	       const char *next_cmd=cmd_buf.Get();
	       if(last_char(next_cmd)!='\n')
	       {
		  // missing EOL on last line, add it
		  FeedCmd("\n");
		  goto try_get_cmd;
	       }
	       fprintf(stderr,_("Warning: discarding incomplete command\n"));
	    }
	    if(!feeder->RealEOF() && top_level)
	    {
	       cmd_buf.Empty();
	       FeedCmd("exit;");
	       return MOVED;
	    }
	    if(waiting_num > 0)
	      return m;
	    RemoveFeeder();
	    m=MOVED;
	    goto try_get_cmd;
	 }
	 if(cmd[0])
	 {
	    FeedCmd(cmd);
	    return MOVED;
	 }
	 else
	 {
	    if(SignalHook::GetCount(SIGINT)>0)
	    {
	       SignalHook::ResetCount(SIGINT);
	       cmd_buf.Empty();	 // flush unparsed command
	       return MOVED;
	    }
	 }
      }
      return m;
   }

   parse_result
      res = parse_one_cmd();

   if(alias_field<=0)
      free_used_aliases();

   switch(res)
   {
   case(PARSE_ERR):
      return MOVED;
   case(PARSE_AGAIN):
      partial_cmd=true;
      goto try_get_cmd;
   case(PARSE_OK):
      if(feeder)
	 feeder->Bg();
      break;
   }
   if(args==0 || args->count()==0)
      return MOVED;  // empty command

   if(interactive)
      session->DontSleep(); // We don't want to get a delay just after user
			    // entered a command.

   exec_parsed_command();
   return MOVED;
}

void CmdExec::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   switch(builtin)
   {
   case(BUILTIN_CD):
      if(session->IsOpen())
	 s->Show("cd `%s' [%s]",squeeze_file_name(args->getarg(1),s->GetWidthDelayed()-40),session->CurrentStatus());
      break;
   case(BUILTIN_OPEN):
      if(session->IsOpen())
	 s->Show("open `%s' [%s]",session->GetHostName(),session->CurrentStatus());
      break;
   case(BUILTIN_GLOB):
      s->Show("%s",glob->Status());
      break;
   case(BUILTIN_EXEC_RESTART):
      abort(); // can't happen
   case(BUILTIN_NONE):
      if(waiting_num>0)
	 Job::ShowRunStatus(s);
      else
	 s->Clear();
      break;
   }
}

void CmdExec::PrintStatus(int v,const char *prefix)
{
   SessionJob::PrintStatus(v,prefix);
   if(builtin)
   {
      xstring_ca s(args->Combine());
      printf(_("\tExecuting builtin `%s' [%s]\n"),s.get(),session->CurrentStatus());
      return;
   }
   if(queue_feeder)
   {
      if(IsSuspended())
	 printf("%s%s\n",prefix,_("Queue is stopped."));
      BuryDoneJobs();
      for(int i=0; i<waiting_num; i++)
      {
	 if(i==0)
	    printf("%s%s ",prefix,_("Now executing:"));
	 if(v==0)
	    waiting[i]->ListOneJob(v);
	 else
	    waiting[i]->PrintJobTitle();
	 if(i+1<waiting_num)
	    printf("%s\t-",prefix);
      }
      queue_feeder->PrintStatus(v,prefix);
      return;
   }
   if(waiting_num==1)
   {
      printf(_("\tWaiting for job [%d] to terminate\n"),waiting[0]->jobno);
      return;
   }
   else if(waiting_num>1)
   {
      printf(_("\tWaiting for termination of jobs: "));
      for(int i=0; i<waiting_num; i++)
      {
	 printf("[%d]",waiting[i]->jobno);
	 printf("%c",i+1<waiting_num?' ':'\n');
      }
      return;
   }
   if(cmd_buf.Size()>0)
   {
      // xgettext:c-format
      printf(_("\tRunning\n"));
   }
   else if(feeder)
   {
      printf(_("\tWaiting for command\n"));
   }
}

CmdExec::CmdExec(FileAccess *f,LocalDirectory *c) : SessionJob(f?f:new DummyProto)
{
   // add this to chain
   next=chain;
   chain=this;

   background=false;

   interactive=false;
   top_level=false;
   feeder=0;
   feeder_called=false;
   used_aliases=0;

   partial_cmd=false;
   alias_field=0;
   default_output=0;
   condition=COND_ANY;
   prev_exit_code=0;
   exit_code=0;
   last_bg=-1;

   cwd=c;
   if(!cwd)
      SaveCWD();

   remote_completion=false;
   long_running=0;
   csh_history=false;
   verify_host=verify_path=true;
   verify_path_cached=false;

   start_time=0;

   redirections=0;

   queue_feeder=0;
   max_waiting=1;

   saved_session=0;

   builtin=BUILTIN_NONE;

   Reconfig();
}

CmdExec::~CmdExec()
{
   // remove this from chain.
   for(CmdExec **scan=&chain; *scan; scan=&(*scan)->next)
   {
      if(this==*scan)
      {
	 *scan=(*scan)->next;
	 break;
      }
   }

   free_used_aliases();
   if(cwd_owner==this)
      cwd_owner=0;

   Reuse(saved_session);
}

const char *CmdExec::FormatPrompt(const char *scan)
{
   const char *cwd=session->GetCwd();
   if(cwd==0 || cwd[0]==0)
      cwd="~";
   {
      const char *home=session->GetHome();
      if(home && strcmp(home,"/") && !strncmp(cwd,home,strlen(home))
	    && (cwd[strlen(home)]=='/' || cwd[strlen(home)]==0))
      {
	 static char *cwdbuf=0;
	 cwdbuf=(char*)xrealloc(cwdbuf,strlen(cwd)-strlen(home)+2);
	 sprintf(cwdbuf,"~%s",cwd+strlen(home));
	 cwd=cwdbuf;
      }
   }
   const char *cwdb=session->GetCwd();
   cwdb=session->GetCwd();
   if(cwdb==0 || cwdb[0]==0)
      cwdb="~";
   const char *p=strrchr(cwdb,'/');
   if(p && p>cwdb)
      cwdb=p+1;

   static const char StartIgn[]={RL_PROMPT_START_IGNORE,0};
   static const char EndIgn[]={RL_PROMPT_END_IGNORE,0};

   subst_t subst[] = {
      { 'a', "\007" },
      { 'e', "\033" },
      { 'n', "\n" },
      { 's', "lftp" },
      { 'v', VERSION },

      { 'h', session->GetHostName() },
      { 'u', session->GetUser() },
 // @ if non-default user
      { '@', session->GetUser()?"@":"" },
      { 'U', session->GetConnectURL() },
      { 'S', slot?slot.get():"" },
      { 'w', cwd },
      { 'W', cwdb },
      { '[', StartIgn },
      { ']', EndIgn },
      { 0, "" }
   };
   static xstring_c prompt;
   prompt.set_allocated(Subst(scan, subst));

   return(prompt);
}

const char *CmdExec::MakePrompt()
{
   if(partial_cmd)
   {
      static char partial_prompt[] = "> ";
      return partial_prompt;
   }

   return FormatPrompt(res_prompt.Query(getenv("TERM")));
}

void CmdExec::beep_if_long()
{
   if(start_time!=0 && long_running!=0
   && now.UnixTime()>start_time+long_running
   && interactive && Idle() && isatty(1))
      write(1,"\007",1);
}

void CmdExec::Reconfig(const char *name)
{
   const char *c=0;
   if(session)
      c = session->GetConnectURL(FA::NO_PATH);

   long_running = res_long_running.Query(c);
   remote_completion = res_remote_completion.QueryBool(c);
   csh_history = res_csh_history.QueryBool(0);
   verify_path=res_verify_path.QueryBool(c);
   verify_path_cached=res_verify_path_cached.QueryBool(c);
   verify_host=res_verify_host.QueryBool(c);
   verbose=res_verbose.QueryBool(0);
   // only allow explicit setting of cmd:interactive to change interactiveness.
   if(top_level && name && !strcmp(name,"cmd:interactive"))
      SetInteractive(res_interactive.QueryBool(0));
   ResType *r=queue_feeder?&res_queue_parallel:&res_parallel;
   max_waiting=r->Query(c);
}

void CmdExec::pre_stdout()
{
   if(status_line)
      status_line->Clear(false);
   if(feeder_called)
      feeder->clear();
   current->TimeoutS(1);
}

void CmdExec::top_vfprintf(FILE *file,const char *f,va_list v)
{
   pre_stdout();
   ::vfprintf(file,f,v);
}

void CmdExec::SetCmdFeeder(CmdFeeder *new_feeder)
{
   new_feeder->prev=feeder;
   new_feeder->saved_buf.set(cmd_buf.Get());
   feeder=new_feeder;
   cmd_buf.Empty();
}

int CmdExec::AcceptSig(int sig)
{
   if(sig!=SIGINT)
      return STALL;
   if(builtin)
   {
      switch(builtin)
      {
      case(BUILTIN_CD):
	 session->Close();
	 break;
      case(BUILTIN_OPEN):
	 session->Close();
	 RevertToSavedSession();
	 break;
      case(BUILTIN_GLOB):
	 glob=0;
	 args_glob=0;
	 break;
      case(BUILTIN_NONE):
      case(BUILTIN_EXEC_RESTART):
	 abort(); // should not happen
      }
      builtin=BUILTIN_NONE;
      redirections=0;
      exit_code=1;
      return MOVED;
   }
   if(waiting_num>0)
   {
      int limit=waiting_num;
      for(int i=0; i<limit; i++)
      {
	 Job *r=waiting[i];
	 int res=r->AcceptSig(sig);
	 if(res==WANTDIE)
	 {
	    exit_code=1;
	    int jn=r->waiting_num;
	    int *j=(int *)alloca(jn*sizeof(int));
	    int k;
	    for(k=0; k<jn; k++)
	       j[k]=r->waiting[k]->jobno;
	    RemoveWaiting(r);
	    Delete(r);
	    i--;
	    limit--;
	    for(k=0; k<jn; k++)
	    {
	       if(j[k]>=0)
		  AddWaiting(FindJob(j[k])); // in case some jobs survived
	    }
	 }
      }
      if(waiting_num==0 && parent!=0)
	 return WANTDIE;
      return MOVED;
   }
   if(parent!=0)
      return WANTDIE;
   return STALL;
}

void CmdExec::SetInteractive(bool i)
{
   if(interactive==i)
      return;
   if(i)
   {
      SignalHook::DoCount(SIGINT);
      SignalHook::DoCount(SIGTSTP);
   }
   else
   {
      SignalHook::Restore(SIGINT);
      SignalHook::Restore(SIGTSTP);
   }
   interactive=i;
}

int CmdExec::unquote(char *buf,const char *str)
{
   char *buf0=buf;
   while(*str)
   {
      if(*str=='"' || *str=='\\')
	 *buf++='\\';
      *buf++=*str++;
   }
   *buf=0;
   return buf-buf0;
}

const char *CmdExec::unquote(const char *str)
{
   static xstring ret;
   ret.get_space(strlen(str)*2);
   ret.set_length(unquote(ret.get_non_const(),str));
   return ret;
}

bool CmdExec::needs_quotation(const char *buf)
{
   while(*buf)
   {
      if(isspace(*buf))
	 return true;
      if(strchr("\"'\\&|>;",*buf))
	 return true;
      buf++;
   }
   return false;
}

void CmdExec::FeedQuoted(const char *c)
{
   char *buf=(char*)alloca(strlen(c)*2+2+1);
   buf[0]='"';
   unquote(buf+1,c);
   strcat(buf,"\"");
   FeedCmd(buf);
}

// implementation is here because it depends on CmdExec.
char *ArgV::CombineQuoted(int start) const
{
   int	 i;
   char  *res;
   char	 *store;
   const char *arg;
   int	 len=0;

   for(i=start; i<Count(); i++)
      len+=strlen(String(i))*2+3;

   if(len==0)
      return(xstrdup(""));

   res=(char*)xmalloc(len);

   store=res;
   for(i=start; i<Count(); i++)
   {
      arg=String(i);
      if(CmdExec::needs_quotation(arg))
      {
	 *store++='"';
	 CmdExec::unquote(store,arg);
	 store+=strlen(store);
	 *store++='"';
      }
      else
      {
	 strcpy(store,arg);
	 store+=strlen(store);
      }
      *store++=' ';
   }
   store[-1]=0;

   return(res);
}

const char *CmdExec::GetFullCommandName(const char *cmd)
{
   const CmdExec::cmd_rec *c;
   int part=CmdExec::find_cmd(cmd,&c);
   if(part==1)
      return c->name;
   return cmd;
}

void CmdExec::AtExit()
{
   FeedCmd(res_at_exit.Query(0));
   FeedCmd("\n");
   /* Clear the title, and ensure we don't write anything else
    * to it in case we're being backgrounded. */
   status_line=0;
}

void CmdExec::EmptyCmds()
{
   cmd_buf.Empty();
}

bool CmdExec::WriteCmds(int fd) const
{
   const char *buf;
   int len;
   cmd_buf.Get(&buf,&len);
   for(;;)
   {
      if(len==0)
	 return true;
      int res=write(fd,buf,len);
      if(res<=0)
	 return false;
      buf+=res;
      len-=res;
   }
}

bool CmdExec::ReadCmds(int fd)
{
   for(;;)
   {
      int size=0x1000;
      size=read(fd,cmd_buf.GetSpace(size),size);
      if(size==-1)
	 return false;
      if(size==0)
	 return true;
      cmd_buf.SpaceAdd(size);
   }
}

void CmdExec::free_used_aliases()
{
   if(used_aliases)
   {
      TouchedAlias::FreeChain(used_aliases);
      used_aliases=0;
   }
   alias_field=0;
}

void CmdExec::skip_cmd(int len)
{
   cmd_buf.Skip(len);
   alias_field-=len;
   if(alias_field<=0)
      free_used_aliases();
}

CmdExec::cmd_rec *CmdExec::dyn_cmd_table=0;
int CmdExec::dyn_cmd_table_count=0;
void CmdExec::RegisterCommand(const char *name,cmd_creator_t creator,const char *short_desc,const char *long_desc)
{
   if(dyn_cmd_table==0)
   {
      dyn_cmd_table_count=2;
      for(const cmd_rec *c=static_cmd_table; c->name; c++)
	 dyn_cmd_table_count++;
      dyn_cmd_table=(cmd_rec*)xmemdup(static_cmd_table,dyn_cmd_table_count*sizeof(cmd_rec));
   }
   else
   {
      dyn_cmd_table_count++;
      dyn_cmd_table=(cmd_rec*)xrealloc(dyn_cmd_table,dyn_cmd_table_count*sizeof(cmd_rec));
   }
   for(cmd_rec *c=dyn_cmd_table; c->name; c++)
   {
      if(!strcmp(c->name,name))
      {
	 c->creator=creator;
	 if(short_desc)
	    c->short_desc=short_desc;
	 if(long_desc)
	    c->long_desc=long_desc;
	 dyn_cmd_table_count--;
	 return;
      }
   }
   dyn_cmd_table[dyn_cmd_table_count-2].name=name;
   dyn_cmd_table[dyn_cmd_table_count-2].creator=creator;
   dyn_cmd_table[dyn_cmd_table_count-2].short_desc=short_desc;
   dyn_cmd_table[dyn_cmd_table_count-2].long_desc=long_desc;
   memset(&dyn_cmd_table[dyn_cmd_table_count-1],0,sizeof(cmd_rec));
}

void CmdExec::ChangeSession(FileAccess *new_session)
{
   Reuse(session);
   session=new_session;
   session->SetPriority(fg?1:0);
   Reconfig(0);
   if(slot)
      ConnectionSlot::Set(slot,session);
}

const CmdExec::cmd_rec *CmdExec::CmdByIndex(int i)
{
   return (dyn_cmd_table?dyn_cmd_table:static_cmd_table)+i;
}

Job *CmdExec::default_cmd()
{
   const char *op=args->a0();
#ifdef WITH_MODULES
   char *modname=(char*)alloca(4+strlen(op)+1);
   sprintf(modname,"cmd-%s",op);
   if(module_load(modname,0,0)==0)
   {
      eprintf("%s\n",module_error_message());
      return 0;
   }
   builtin=BUILTIN_EXEC_RESTART;
   return this;
#else
   eprintf(_("%s: command `%s' is not compiled in.\n"),op,op);
   return 0;
#endif
}

void CmdExec::FeedArgV(const ArgV *args,int start)
{
   xstring_c cmd;

   if(start+1==args->count())
      cmd.set_allocated(args->Combine(start));
   else
      cmd.set_allocated(args->CombineQuoted(start));

   FeedCmd(cmd);
   FeedCmd("\n");
}

bool CmdExec::SameQueueParameters(CmdExec *scan,const char *this_url)
{
   return !strcmp(this_url,scan->session->GetConnectURL(FA::NO_PATH))
      && this->slot.eq(scan->slot);
}

/* return the CmdExec containing a queue feeder; create if necessary */
CmdExec  *CmdExec::GetQueue(bool create)
{
   const char *this_url=session->GetConnectURL(FA::NO_PATH);
   // future GetConnectURL overwrite the static buffer, save it.
   this_url=alloca_strdup(this_url);
   for(CmdExec *scan=chain; scan; scan=scan->next)
   {
      if(scan->queue_feeder && SameQueueParameters(scan,this_url))
	 return scan;
   }
   if(!create)
      return NULL;

   CmdExec *queue=new CmdExec(session->Clone(),cwd->Clone());
   queue->slot.set(slot);

   queue->SetParentFg(this,false);
   queue->AllocJobno();
   const char *url=session->GetConnectURL(FA::NO_PATH);
   queue->cmdline.vset("queue (",url,slot?"; ":"",slot?slot.get():"",")",NULL);
   queue->queue_feeder=new QueueFeeder(session->GetCwd(), cwd->GetName());
   queue->SetCmdFeeder(queue->queue_feeder);
   queue->Reconfig(0);

   return queue;
}
