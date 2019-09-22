/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stddef.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include "CmdExec.h"
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
#define waiting_num waiting.count()

static ResType lftp_cmd_vars[] = {
   {"cmd:default-protocol",	 "ftp",	  0,0},
   {"cmd:long-running",		 "30",	  ResMgr::UNumberValidate,0},
   {"cmd:remote-completion",	 "on",	  ResMgr::BoolValidate,0},
   {"cmd:prompt",		 "lftp \\S\\? \\u\\@\\h:\\w> ",0,0},
   {"cmd:default-title",	 "lftp \\h:\\w",0,0},
   {"cmd:ls-default",		 "",	  0,0},
   {"cmd:csh-history",		 "off",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:verify-path",		 "yes",	  ResMgr::BoolValidate,0},
   {"cmd:verify-path-cached",	 "no",	  ResMgr::BoolValidate,0},
   {"cmd:verify-host",		 "yes",	  ResMgr::BoolValidate,0},
   {"cmd:at-exit",		 "",	  0,0},
   {"cmd:at-exit-bg",		 "",	  0,0},
   {"cmd:at-exit-fg",		 "",	  0,0},
   {"cmd:at-background",	 "",	  0,0},
   {"cmd:at-terminate",		 "",	  0,0},
   {"cmd:at-finish",		 "",	  0,0},
   {"cmd:at-queue-finish",	 "",	  0,0},
   {"cmd:fail-exit",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:verbose",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:interactive",		 "auto",  ResMgr::TriBoolValidate,ResMgr::NoClosure},
   {"cmd:show-status",		 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:move-background",	 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:move-background-detach","yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:set-term-status",	 "no",	  ResMgr::BoolValidate,0},
   {"cmd:term-status",		 "",	  0, 0},
   {"cmd:trace",		 "no",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {"cmd:parallel",		 "1",	  ResMgr::UNumberValidate,0},
   {"cmd:queue-parallel",	 "1",	  ResMgr::UNumberValidate,0},
   {"cmd:cls-exact-time",	 "yes",	  ResMgr::BoolValidate,ResMgr::NoClosure},
   {0}
};
static ResDecls lftp_cmd_vars_register(lftp_cmd_vars);

CmdExec	 *CmdExec::cwd_owner;
CmdExec	 *CmdExec::chain;
JobRef<CmdExec> CmdExec::top;

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
   const cmd_rec *c=dyn_cmd_table?dyn_cmd_table.get():static_cmd_table;
   const int count=dyn_cmd_table?dyn_cmd_table.count():static_cmd_table_length;
   for(int i=0; i<count; i++,c++)
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
      if(exit_code!=0 && ResMgr::QueryBool("cmd:fail-exit",0))
      {
         failed_exit_code=exit_code;
	 while(feeder)
	    RemoveFeeder();
	 cmd_buf.Empty();
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
   if(!cmd_name)
      return;
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
      RevertToSavedSession();
      if(new_job) {
	 if(!new_job->cmdline)
	    new_job->cmdline.move_here(cmdline);
	 AddNewJob(new_job);
      }
   }
}

void CmdExec::AddNewJob(Job *new_job)
{
   if(new_job->jobno<0)
      new_job->AllocJobno();
   new_job->SetParentFg(this,!background);
   exit_code=0;
   AddWaiting(new_job);
   if(background) {
      Roll(new_job);
      if(!new_job->Done())
	 SuspendJob(new_job);
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
   return(waiting_num==0 && builtin==BUILTIN_NONE && (cmd_buf.Size()==0 || partial_cmd));
}

int CmdExec::Done()
{
   Enter();
   bool done = (feeder==0 && Idle())
      || (auto_terminate_in_bg && NumberOfChildrenJobs()==0 && !in_foreground_pgrp());
   Leave();
   return done;
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
   SetInteractive();
}

void CmdExec::ReuseSavedSession()
{
   saved_session=0;
}
void CmdExec::RevertToSavedSession()
{
   if(saved_session==0)
      return;
   ChangeSession(saved_session.borrow());
}
void CmdExec::ChangeSlot(const char *n)
{
   if(!n || !*n)
   {
      slot.set(0);
      return;
   }
   slot.set(n);
   const FileAccess *s=ConnectionSlot::FindSession(n);
   if(!s)
      ConnectionSlot::Set(n,session);
   else
      ChangeSession(s->Clone());
}

void CmdExec::AtFinish()
{
   if(queue_feeder && queue_feeder->JobCount())
      return;
   if(!fed_at_finish && NumAwaitedJobs()==0 && cmd_buf.Size()==0) {
      FeedCmd(ResMgr::Query(queue_feeder?"cmd:at-queue-finish":"cmd:at-finish",0));
      FeedCmd("\n");
      fed_at_finish=true;
   }
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
		     session->Roll();
		     return MOVED;
		  }
		  session->Close();
		  exit_code=0;
		  builtin=BUILTIN_NONE;
		  PrependCmd(xstring::get_tmp("open ").append_quoted(loc));
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
	       args=args_glob.borrow();
	       builtin=BUILTIN_NONE;
	       redirections=0;
	       if(status_line)
		  status_line->Clear();
	       exit_code=prev_exit_code;
	       RevertToSavedSession();
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
      if(status_line && show_status && status_line->CanShowNow())
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
      if(status_line && show_status && status_line->CanShowNow())
	 ShowRunStatus(status_line);   // this is only for top level CmdExec.
      if(m != STALL || interactive || waiting_num >= max_waiting)
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

	 if(status_line && show_status)
	 {
	    const char *def_title = FormatPrompt(ResMgr::Query("cmd:default-title",getenv("TERM")));
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
	    auto_terminate_in_bg=false;
	    FeedCmd(cmd);
	    Roll();
	    if(!Idle())
	       fed_at_finish=false;
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

   parse_result res = parse_one_cmd();

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
   if(args==0 || args->count()==0) {
      AtFinish();
      return MOVED;  // empty command
   }

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

xstring& CmdExec::FormatStatus(xstring& s,int v,const char *prefix)
{
   SessionJob::FormatStatus(s,v,prefix);
   if(builtin)
   {
      xstring_ca ac(args->Combine());
      return s.appendf(_("\tExecuting builtin `%s' [%s]\n"),ac.get(),session->CurrentStatus());
   }
   if(queue_feeder)
   {
      if(IsSuspended())
	 s.appendf("%s%s\n",prefix,_("Queue is stopped."));
      BuryDoneJobs();
      for(int i=0; i<waiting_num; i++)
      {
	 if(i==0)
	    s.appendf("%s%s ",prefix,_("Now executing:"));
	 if(v==0)
	    waiting[i]->FormatOneJob(s,v);
	 else
	    waiting[i]->FormatJobTitle(s);
	 if(i+1<waiting_num)
	    s.appendf("%s\t-",prefix);
      }
      return queue_feeder->FormatStatus(s,v,prefix);
   }
   if(waiting_num==1)
      return s.appendf(_("\tWaiting for job [%d] to terminate\n"),waiting[0]->jobno);
   else if(waiting_num>1)
   {
      s.appendf(_("\tWaiting for termination of jobs: "));
      for(int i=0; i<waiting_num; i++)
      {
	 s.appendf("[%d]",waiting[i]->jobno);
	 s.append(i+1<waiting_num?' ':'\n');
      }
      return s;
   }
   if(cmd_buf.Size()>0)
      s.append(_("\tRunning\n"));
   else if(feeder)
      s.append(_("\tWaiting for command\n"));
   return s;
}

void CmdExec::init(LocalDirectory *c)
{
   // add this to chain
   next=chain;
   chain=this;

   background=false;

   interactive=false;
   show_status=true;
   top_level=false;
   auto_terminate_in_bg=false;
   feeder=0;
   feeder_called=false;
   used_aliases=0;

   partial_cmd=false;
   alias_field=0;
   default_output=0;
   condition=COND_ANY;
   prev_exit_code=0;
   exit_code=0;
   failed_exit_code=0;
   last_bg=-1;
   fed_at_finish=true;

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
CmdExec::CmdExec(FileAccess *s,LocalDirectory *c)
   : SessionJob(s?s:new DummyProto), parent_exec(0)
{
   init(c);
}
CmdExec::CmdExec(CmdExec *parent)
   : SessionJob(parent->session->Clone()), parent_exec(parent)
{
   init(parent->cwd->Clone());
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
}

const char *CmdExec::FormatPrompt(const char *scan)
{
   const char *cwd=session->GetCwd();
   if(cwd==0 || cwd[0]==0)
      cwd="~";
   {
      const char *home=session->GetHome();
      int home_len=xstrlen(home);
      if(home_len>1 && !strncmp(cwd,home,home_len)
      && (cwd[home_len]=='/' || cwd[home_len]==0))
      {
	 cwd=xstring::format("~%s",cwd+home_len);
      }
   }
   const char *cwdb=session->GetCwd();
   if(cwdb==0 || cwdb[0]==0)
      cwdb="~";
   const char *p=strrchr(cwdb,'/');
   if(p && p>cwdb)
      cwdb=p+1;

   const char *lcwd=this->cwd->GetName();
   {
      const char *home=get_home();
      int home_len=xstrlen(home);
      if(home_len>1 && !strncmp(lcwd,home,home_len)
      && (lcwd[home_len]=='/' || lcwd[home_len]==0))
      {
	 lcwd=xstring::format("~%s",lcwd+home_len);
      }
   }
   const char *lcwdb=this->cwd->GetName();
   p=strrchr(lcwdb,'/');
   if(p && p>lcwdb)
      lcwdb=p+1;

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
      { 'l', lcwd },
      { 'L', lcwdb },
      { '[', StartIgn },
      { ']', EndIgn },
      { 0, "" }
   };
   static xstring prompt;
   SubstTo(prompt, scan, subst);

   return(prompt);
}

const char *CmdExec::MakePrompt()
{
   if(partial_cmd)
      return "> ";
   return FormatPrompt(ResMgr::Query("cmd:prompt",getenv("TERM")));
}

void CmdExec::beep_if_long()
{
   if(start_time!=0 && long_running!=0
   && now.UnixTime()>start_time+long_running
   && interactive && Idle() && isatty(1))
      write(1,"\007",1);
   AtFinish();
}

void CmdExec::Reconfig(const char *name)
{
   const char *c=0;
   if(session)
      c = session->GetConnectURL(FA::NO_PATH);

   long_running=ResMgr::Query("cmd:long-running",c);
   remote_completion=ResMgr::QueryBool("cmd:remote-completion",c);
   csh_history=ResMgr::QueryBool("cmd:csh-history",0);
   verify_path=ResMgr::QueryBool("cmd:verify-path",c);
   verify_path_cached=ResMgr::QueryBool("cmd:verify-path-cached",c);
   verify_host=ResMgr::QueryBool("cmd:verify-host",c);
   verbose=ResMgr::QueryBool("cmd:verbose",0);
   if(top_level || queue_feeder)
      max_waiting=ResMgr::Query(queue_feeder?"cmd:queue-parallel":"cmd:parallel",c);
   if(name && !strcmp(name,"cmd:interactive"))
      SetInteractive();
   show_status=ResMgr::QueryBool("cmd:show-status",0);
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
   SetInteractive();
}

int CmdExec::AcceptSig(int sig)
{
   if(sig!=SIGINT && sig!=SIGTERM)
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
	    RemoveWaiting(r);
	    Delete(r);
	    i--;
	    limit--;
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
void CmdExec::SetInteractive()
{
   if(!top_level)
      return;
   bool def=feeder?feeder->IsInteractive():false;
   SetInteractive(ResMgr::QueryTriBool("cmd:interactive",0,def));
}

xstring& xstring::append_quoted(const char *str,int len)
{
   if(!CmdExec::needs_quotation(str,len))
      return append(str);

   append('"');
   while(len>0)
   {
      if(*str=='"' || *str=='\\')
	 append('\\');
      append(*str++);
      len--;
   }
   return append('"');
}

bool CmdExec::needs_quotation(const char *buf,int len)
{
   while(len>0)
   {
      if(*buf==' ' || *buf=='\t')
	 return true;
      if(strchr("\"'\\&|>;",*buf))
	 return true;
      buf++;
      len--;
   }
   return false;
}

void CmdExec::FeedQuoted(const char *c)
{
   FeedCmd(xstring::get_tmp("").append_quoted(c));
}

// implementation is here because it depends on CmdExec.
xstring& ArgV::CombineQuotedTo(xstring& res,int start) const
{
   res.nset("",0);
   if(start>=Count())
      return res;
   for(;;)
   {
      const char *arg=String(start++);
      res.append_quoted(arg);
      if(start>=Count())
	 return(res);
      res.append(' ');
   }
}
xstring& ArgV::CombineCmdTo(xstring& res,int i) const
{
   return i>=count()-1 ? CombineTo(res,i) : CombineQuotedTo(res,i);
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
   FeedCmd(ResMgr::Query("cmd:at-exit",0));
   FeedCmd("\n");
   /* Clear the title, and ensure we don't write anything else
    * to it in case we're being backgrounded. */
   status_line=0;
}

void CmdExec::AtExitBg()
{
   FeedCmd(ResMgr::Query("cmd:at-exit-bg",0));
   FeedCmd("\n");
}
void CmdExec::AtExitFg()
{
   FeedCmd(ResMgr::Query("cmd:at-exit-fg",0));
   FeedCmd("\n");
}
void CmdExec::AtBackground()
{
   FeedCmd(ResMgr::Query("cmd:at-background",0));
   FeedCmd("\n");
}
void CmdExec::AtTerminate()
{
   FeedCmd(ResMgr::Query("cmd:at-terminate",0));
   FeedCmd("\n");
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

int CmdExec::cmd_rec::cmp(const CmdExec::cmd_rec *a,const CmdExec::cmd_rec *b)
{
   return strcmp(a->name,b->name);
}

xarray<CmdExec::cmd_rec> CmdExec::dyn_cmd_table;
void CmdExec::RegisterCommand(const char *name,cmd_creator_t creator,const char *short_desc,const char *long_desc)
{
   if(dyn_cmd_table==0)
      dyn_cmd_table.nset(static_cmd_table,static_cmd_table_length);
   cmd_rec new_entry={name,creator,short_desc,long_desc};
   int i;
   if(dyn_cmd_table.bsearch(new_entry,cmd_rec::cmp,&i))
   {
      cmd_rec *const c=&dyn_cmd_table[i];
      c->creator=creator;
      if(short_desc)
	 c->short_desc=short_desc;
      if(long_desc || strlen(c->long_desc)<2)
	 c->long_desc=long_desc;
      return;
   }
   dyn_cmd_table.insert(new_entry,i);
}

void CmdExec::ChangeSession(FileAccess *new_session)
{
   session=new_session;
   session->SetPriority(fg?1:0);
   Reconfig(0);
   if(slot)
      ConnectionSlot::Set(slot,session);
}

void CmdExec::RegisterCompatCommand(const char *name,cmd_creator_t creator,const char *short_desc,const char *long_desc)
{
   if(dyn_cmd_table==0)
   {
      int count=0;
      for(const cmd_rec *c=static_cmd_table; c->name; c++)
        count++;
      dyn_cmd_table.nset(static_cmd_table,count);
   }
   for(int i=0; i<dyn_cmd_table.count(); i++)
   {
      cmd_rec *const c=&dyn_cmd_table[i];
      if(!strcmp(c->name,name))
      {
          char *lftp_name=(char*)malloc(5+strlen(name)+1);
          char *short_d = NULL;

          if (c->short_desc) {
              short_d = (char*)malloc(5+strlen(c->short_desc)+1);
              sprintf(short_d, "lftp-%s", c->short_desc);
              c->short_desc = short_d;
          }

          sprintf(lftp_name, "lftp-%s", name);
          c->name = lftp_name;
            break;
       }
   }
   cmd_rec new_entry={name,creator,short_desc,long_desc};
   dyn_cmd_table.append(new_entry);
}

const char *CmdExec::CmdByIndex(int i)
{
   if(dyn_cmd_table)
   {
      if(i<dyn_cmd_table.count())
	 return dyn_cmd_table[i].name;
      return 0;
   }
   return static_cmd_table[i].name;
}

bool CmdExec::load_cmd_module(const char *op)
{
   const char *modname=xstring::cat("cmd-",op,NULL);
   if(module_init_preloaded(modname))
      return true;
#ifdef WITH_MODULES
   if(module_load(modname,0,0)==0)
   {
      eprintf("%s\n",module_error_message());
      return false;
   }
   return true;
#else
   eprintf(_("%s: command `%s' is not compiled in.\n"),op,op);
   return false;
#endif
}

Job *CmdExec::default_cmd()
{
   const char *op=args->a0();
   if(load_cmd_module(op)) {
      builtin=BUILTIN_EXEC_RESTART;
      return this;
   }
   return 0;
}
Job *CmdExec::builtin_local()
{
   if(args->count()<2) {
      eprintf(_("Usage: %s cmd [args...]\n"),args->a0());
      return 0;
   }
   saved_session=session.borrow();
   session=FileAccess::New("file");
   if(!session) {
      eprintf(_("%s: cannot create local session\n"),args->a0());
      RevertToSavedSession();
      return 0;
   }
   session->SetCwd(cwd->GetName());
   args->delarg(0);
   builtin=BUILTIN_EXEC_RESTART;
   return this;
}

void CmdExec::FeedArgV(const ArgV *args,int start)
{
   xstring cmd;
   args->CombineCmdTo(cmd,start);
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
