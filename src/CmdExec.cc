/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#define RL_PROMPT_START_IGNORE	'\001'
#define RL_PROMPT_END_IGNORE	'\002'

#define super SessionJob

static ResDecl
   res_default_proto	   ("cmd:default-protocol","ftp",0,0),
   res_long_running	   ("cmd:long-running",	"30",ResMgr::UNumberValidate,0),
   res_remote_completion   ("cmd:remote-completion","on",ResMgr::BoolValidate,0),
   res_prompt		   ("cmd:prompt",	"lftp> ",0,0),
   res_default_ls	   ("cmd:ls-default",	"",0,0),
   res_csh_history	   ("cmd:csh-history",	"off",ResMgr::BoolValidate,ResMgr::NoClosure),
   res_verify_path	   ("cmd:verify-path",	"yes",ResMgr::BoolValidate,0),
   res_verify_host	   ("cmd:verify-host",	"yes",ResMgr::BoolValidate,0),
   res_at_exit		   ("cmd:at-exit",	"",   0,0),
   res_fail_exit	   ("cmd:fail-exit",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_verbose		   ("cmd:verbose",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_interactive	   ("cmd:interactive",	"no", ResMgr::BoolValidate,ResMgr::NoClosure),
   res_move_background	   ("cmd:move-background","yes", ResMgr::BoolValidate,ResMgr::NoClosure);

CmdExec	 *CmdExec::cwd_owner=0;
CmdExec	 *CmdExec::chain=0;

void  CmdExec::SetCWD(const char *c)
{
   xfree(cwd);
   cwd=xstrdup(c);
   if(cwd_owner==this)
      cwd_owner=0;
}

void  CmdExec::SaveCWD()
{
   cwd=xgetcwd();
   if(cwd==0)
   {
      // A bad case, but we can do nothing ( xgettext:c-format )
      eprintf(_("Warning: getcwd() failed: %s\n"),strerror(errno));
      cwd_owner=this;
   }
   else
   {
      if(cwd_owner==0)
	 cwd_owner=this;
   }
}
int  CmdExec::RestoreCWD()
{
   if(cwd_owner==this)
      return 0;
   if(cwd==0)
      goto fail;
   if(chdir(cwd)==0)
   {
      cwd_owner=this;
      return 0;
   }
   else
   {
      eprintf(_("Warning: chdir(%s) failed: %s\n"),cwd,strerror(errno));
   fail:
      // can't run further commands in wrong directory
      eprintf("No directory to execute commands in - terminating\n");
      while(!Done())
	 RemoveFeeder();
      exit_code=1;
      return -1;
   }
}

void CmdExec::FeedCmd(const char *c)
{
   partial_cmd=false;
   time(&start_time);
   if(cmd_buf==0)
   {
      cmd_buf=next_cmd=xstrdup(c);
      return;
   }
   int len=strlen(next_cmd);
   memmove(cmd_buf,next_cmd,len);
   cmd_buf=next_cmd=(char*)xrealloc(cmd_buf,len+strlen(c)+1);
   strcpy(next_cmd+len,c);
};

void CmdExec::PrependCmd(const char *c)
{
   time(&start_time);

   int len=strlen(c);
   int nl=(len>0 && c[len-1]!='\n');

   int next_cmd_len=xstrlen(next_cmd);
   int next_cmd_offs=next_cmd-cmd_buf;
   if(next_cmd_offs<len+1)
      cmd_buf=(char*)xrealloc(cmd_buf,len+nl+next_cmd_len+1);
   if(next_cmd_len>0)
      memmove(cmd_buf+len+nl,cmd_buf+next_cmd_offs,next_cmd_len);
   cmd_buf[len+nl+next_cmd_len]=0;
   memcpy(cmd_buf,c,len);
   if(nl)
      cmd_buf[len]='\n';
   next_cmd=cmd_buf;

   if(alias_field>0)
      alias_field+=len+nl;
}

int CmdExec::find_cmd(const char *cmd_name,const struct cmd_rec **ret)
{
   int part=0;
   const cmd_rec *c;
   for(c=dyn_cmd_table?dyn_cmd_table:static_cmd_table; c->name; c++)
   {
      if(!strcmp(c->name,cmd_name))
      {
	 *ret=c;
	 return 1;
      }
      if(!strncmp(c->name,cmd_name,strlen(cmd_name)))
      {
	 part++;
	 *ret=c;
      }
   }

   if(part!=1)
      *ret=0;
   return part;
}

void  CmdExec::exec_parsed_command()
{
   switch(condition)
   {
   case(COND_ANY):
      if(exit_code!=0 && bool(res_fail_exit.Query(0)))
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

   SignalHook::ResetCount(SIGINT);
   SignalHook::ResetCount(SIGHUP);
   SignalHook::ResetCount(SIGTSTP);

   bool did_default=false;

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
	 return;

      args->setarg(0,c->name); // in case it was abbreviated

      if(cmd==0)
	 cmd=args->Combine();

      Job *new_job=0;

      if(c->creator==0)
      {
	 if(did_default)
	 {
	    eprintf("Module for command `%s' did not register the command.\n",cmd_name);
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
	 if(cmd && new_job->cmdline==0)
	 {
	    new_job->cmdline=cmd;
	    cmd=0;
      	 }
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
   bool old_running=running;
   running=true; // disable reentering

   if(args)
      delete args;
   args=a;
   xfree(cmd);
   cmd=args->Combine();
   if(output)
      delete output;
   output=o;
   background=b;
   condition=COND_ANY;
   exec_parsed_command();

   running=old_running;
}

bool CmdExec::Idle()
{
   return(waiting_num==0 && (next_cmd==0 || *next_cmd==0 || partial_cmd));
}

int CmdExec::Done()
{
   return(feeder==0 && Idle());
}

void CmdExec::RemoveFeeder()
{
   alias_field=0;
   xfree(cmd_buf); next_cmd=cmd_buf=0;
   free_used_aliases();

   if(!feeder)
      return;

   CmdFeeder *tmp=feeder->prev;
   next_cmd=cmd_buf=feeder->saved_buf;
   partial_cmd=false;
   delete feeder;
   feeder=tmp;

   // save old cwd if necessary
   if(interactive && feeder==0)
      cwd_history.Set(session,session->GetCwd());
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
   Reuse(session);
   session=saved_session;
   saved_session=0;
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
	    session->Close();
	    exit_code=0;
	    builtin=BUILTIN_NONE;
	    beep_if_long();
	    return MOVED;
	 }
	 if(res<0)
	 {
	    // error
	    if(status_line)
	       status_line->Clear();
	    eprintf("%s: %s\n",args->getarg(0),session->StrError(res));
	    session->Close();
	    builtin=BUILTIN_NONE;
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
	    glob->SortByName();
	    FileSet &list=*glob->GetResult();
	    for(int i=0; list[i]; i++)
	       args_glob->Append(list[i]->name);
	 }
	 if(glob->Done() || glob->Error())
	 {
	    delete glob;
	    glob=0;
	    const char *pat=args->getnext();
	    if(!pat)
	    {
	       // it was last argument
	       delete args;
	       args=args_glob;
	       args_glob=0;
	       builtin=BUILTIN_NONE;
	       if(status_line)
		  status_line->Clear();
	       exit_code=prev_exit_code;
	       exec_parsed_command();
	       return MOVED;
	    }
	    glob=new GlobURL(session,pat);
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
	       {
		  // accept the path
		  const char *f=session->GetFile();
		  f=alloca_strdup(f);
		  session->Chdir(f,false);
	       }
	       session->Close();
	       exit_code=0;
	       builtin=BUILTIN_NONE;
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
      if(status_line)
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
      if(status_line)
	 ShowRunStatus(status_line);   // this is only for top level CmdExec.
      return m;
   }

   if(!interactive)
   {
      BuryDoneJobs();
      if(FindJob(last_bg)==0)
	 last_bg=-1;
   }

try_get_cmd:
   if(next_cmd==0 || *next_cmd==0 || partial_cmd)
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
	 char *prompt=MakePrompt();
	 feeder_called=true;
	 const char *cmd=feeder->NextCmd(this,prompt);
	 feeder_called=false;
	 if(cmd==0)
	 {
	    if(next_cmd && *next_cmd && partial_cmd)
	    {
	       if(next_cmd[strlen(next_cmd)-1]!='\n')
	       {
		  // missing EOL on last line, add it
		  FeedCmd("\n");
		  goto try_get_cmd;
	       }
	       fprintf(stderr,_("Warning: discarding incomplete command\n"));
	    }
	    if(!feeder->RealEOF() && top_level)
	    {
	       *next_cmd=0;
	       FeedCmd("exit;");
	       return MOVED;
	    }
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
	       if(next_cmd)
		  *next_cmd=0;	 // flush unparsed command
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

void CmdExec::ShowRunStatus(StatusLine *s)
{
   switch(builtin)
   {
   case(BUILTIN_CD):
      if(session->IsOpen())
	 s->Show("cd `%s' [%s]",args->getarg(1),session->CurrentStatus());
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

void CmdExec::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(builtin)
   {
      char *s=args->Combine();
      printf(_("\tExecuting builtin `%s' [%s]\n"),s,session->CurrentStatus());
      xfree(s);
      return;
   }
   if(is_queue)
   {
      if(waiting_num>0)
      {
	 printf("\t%s ",_("Now executing:"));
	 for(int i=0; i<waiting_num; i++)
	 {
	    if(v==0)
	       waiting[i]->ListOneJob(v);
	    else
	       waiting[i]->PrintJobTitle();
	    if(i+1<waiting_num)
	       printf("\t\t-");
	 }
      }
      if(!(next_cmd && next_cmd[0]))
	 return;
      printf(_("\tCommands queued:\n"));
      char *cmd=next_cmd;
      int n=1;
      for(;;)
      {
	 char *cmd_end=strchr(cmd,'\n');
	 if(!cmd_end)
	    cmd_end=cmd+strlen(cmd);
	 printf("\t%2d. %.*s\n",n++,int(cmd_end-cmd),cmd);
	 if(*cmd_end==0)
	    break;
	 cmd=cmd_end+1;
	 if(*cmd==0)
	    break;
	 if(v<2 && n>4)
	 {
	    printf("\t%2d. ...\n",n);
	    break;
	 }
      }
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
   }
   if(next_cmd && next_cmd[0])
   {
      // xgettext:c-format
      printf(_("\tRunning\n"));
   }
   else if(feeder)
   {
      printf(_("\tWaiting for command\n"));
   }
}

CmdExec::CmdExec(FileAccess *f) : SessionJob(f)
{
   // add this to chain
   next=chain;
   chain=this;

   cmd=0;
   args=0;
   output=0;
   background=false;

   interactive=false;
   top_level=false;
   status_line=0;
   feeder=0;
   feeder_called=false;
   used_aliases=0;

   next_cmd=cmd_buf=0;
   partial_cmd=false;
   alias_field=0;
   default_output=0;
   condition=COND_ANY;
   prev_exit_code=0;
   exit_code=0;
   last_bg=-1;

   cwd=0;
   SaveCWD();

   var_ls=xstrdup("");
   remote_completion=false;
   long_running=0;
   csh_history=false;
   verify_host=verify_path=true;

   start_time=0;
   old_cwd=0;
   old_lcwd=0;

   glob=0;
   args_glob=0;

   is_queue=false;
   queue_cwd=0;
   queue_lcwd=0;

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
   xfree(cmd);
   if(args)
      delete args;
   if(output)
      delete output;
   xfree(cmd_buf);
   xfree(cwd);
   if(cwd_owner==this)
      cwd_owner=0;
   xfree(old_cwd);
   xfree(old_lcwd);
   if(glob)
      delete glob;
   if(args_glob)
      delete args_glob;

   xfree(queue_cwd);
   xfree(queue_lcwd);

   Reuse(saved_session);
}

char *CmdExec::MakePrompt()
{
   static char *prompt=0;
   static int prompt_size=256;

   if(prompt==0)
      prompt=(char*)xmalloc(prompt_size);

   if(partial_cmd)
   {
      return strcpy(prompt,"> ");
   }

   char *store=prompt;

   const char *scan=res_prompt.Query(getenv("TERM"));
   char ch;
   char str[3];
   const char *to_add;
   static char *buf=0;

   *store=0;
   for(;;)
   {
      ch=*scan++;
      if(ch==0)
	 break;

      if(ch=='\\' && *scan && *scan!='\\')
      {
	 ch=*scan++;
	 switch(ch)
	 {
	 case'0':case'1':case'2':case'3':case'4':case'5':case'6':case'7':
	 {
	    unsigned len;
	    unsigned code;
	    sscanf(scan,"%3o%n",&code,&len);
	    ch=code;
	    scan+=len;
	    str[0]=ch;
	    str[1]=0;
	    to_add=str;
	    break;
	 }
	 case 'a':
	    to_add="\007";
	    break;
	 case 'e':
	    to_add="\033";
	    break;
	 case 'h':
	    to_add=session->GetHostName();
	    break;
	 case 'n':
 	    to_add="\n";
 	    break;
	 case 's':
 	    to_add="lftp";
 	    break;
	 case 'u':
	    to_add=session->GetUser();
	    break;
	 case '@': // @ if non-default user
	    to_add=session->GetUser()?"@":"";
	    break;
	 case 'U':
	    to_add=session->GetConnectURL();
	    break;
 	 case 'v':
	    to_add=VERSION;
	    break;
	 case 'w': // working directory
	 {
	    to_add=session->GetCwd();
	    const char *home=session->GetHome();
	    if(home && strcmp(home,"/") && !strncmp(to_add,home,strlen(home))
	    && (to_add[strlen(home)]=='/' || to_add[strlen(home)]==0))
	    {
	       buf=(char*)xrealloc(buf,strlen(to_add)-strlen(home)+2);
	       sprintf(buf,"~%s",to_add+strlen(home));
	       to_add=buf;
	    }
	    else
	    {
	       if(to_add[0]==0)
		  to_add="~";
	    }
	    break;
	 }
 	 case 'W': // working directory basename
	 {
	    to_add=session->GetCwd();
            if(to_add[0]==0)
               to_add="~";
 	    const char *p=strrchr(to_add,'/');
	    if(p && p>to_add)
	       to_add=p+1;
 	    break;
 	 }
	 case '[':
	 case ']':
	    str[0]='\001';
	    str[1]=(ch=='[')?RL_PROMPT_START_IGNORE:RL_PROMPT_END_IGNORE;
	    str[2]='\0';
	    to_add=str;
	    break;
	 default:
	    str[0]='\\';
	    str[1]=ch;
	    str[2]=0;
	    to_add=str;
	    break;
	 }
      }
      else
      {
	 if(ch=='\\' && *scan=='\\')
	    scan++;
	 str[0]=ch;
	 str[1]=0;
	 to_add=str;
      }

      if(to_add==0)
	 continue;

      int store_index=store-prompt;
      int need=store_index+strlen(to_add)+1;
      if(prompt_size<need)
      {
	 while(prompt_size<need)
	    prompt_size*=2;
	 prompt=(char*)xrealloc(prompt,prompt_size);
	 store=prompt+store_index;
      }

      strcpy(store,to_add);
      store+=strlen(to_add);
   }
   return(prompt);
}

void CmdExec::beep_if_long()
{
   if(start_time!=0 && long_running!=0
   && time(0)-start_time>long_running
   && interactive && Idle() && isatty(1))
      write(1,"\007",1);
}

void CmdExec::Reconfig(const char *name)
{
   const char *c=0;
   if(session)
      c = session->GetConnectURL(FA::NO_PATH);

   long_running = res_long_running.Query(c);
   remote_completion = res_remote_completion.Query(c);
   csh_history = res_csh_history.Query(0);
   xfree(var_ls);
   var_ls=xstrdup(res_default_ls.Query(c));
   verify_path=res_verify_path.Query(c);
   verify_host=res_verify_host.Query(c);
   verbose=res_verbose.Query(0);
   // only allow explicit setting of cmd:interactive to change interactiveness.
   if(top_level && name && !strcmp(name,"cmd:interactive"))
      SetInteractive(res_interactive.Query(0));
}

void CmdExec::pre_stdout()
{
   if(status_line)
      status_line->Clear();
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
   new_feeder->saved_buf=xstrdup(next_cmd);
   xfree(cmd_buf);
   cmd_buf=next_cmd=0;
   feeder=new_feeder;
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
	 delete glob;
	 glob=0;
	 delete args_glob;
	 args_glob=0;
	 break;
      case(BUILTIN_NONE):
      case(BUILTIN_EXEC_RESTART):
	 abort(); // should not happen
      }
      builtin=BUILTIN_NONE;
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
	    for(int k=0; k<jn; k++)
	       j[k]=r->waiting[k]->jobno;
	    RemoveWaiting(r);
	    Delete(r);
	    i--;
	    limit--;
	    for(int k=0; k<jn; k++)
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

void CmdExec::unquote(char *buf,const char *str)
{
   while(*str)
   {
      if(*str=='"' || *str=='\\')
	 *buf++='\\';
      *buf++=*str++;
   }
   *buf=0;
}

bool CmdExec::needs_quotation(const char *buf)
{
   while(*buf)
   {
      if(isspace(*buf))
	 return true;
      if(*buf=='"' || *buf=='\\' || *buf=='&' || *buf=='|' || *buf=='>' || *buf==';')
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
   char	 *store,*arg;
   int	 len=0;

   for(i=start; i<c; i++)
      len+=strlen(v[i])*2+3;

   if(len==0)
      return(xstrdup(""));

   res=(char*)xmalloc(len);

   store=res;
   for(i=start; i<c; i++)
   {
      arg=v[i];
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


CmdExec::cmd_rec *CmdExec::dyn_cmd_table=0;
int CmdExec::dyn_cmd_table_count=0;
void CmdExec::RegisterCommand(const char *name,cmd_creator_t creator,const char *short_desc,const char *long_desc)
{
   if(dyn_cmd_table==0)
   {
      dyn_cmd_table_count=1;
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
   eprintf("%s: command `%s' is not compiled in.\n",op,op);
   return 0;
#endif
}

void CmdExec::FeedArgV(const ArgV *args,int start)
{
   char *cmd;

   if(start+1==args->count())
      cmd=args->Combine(start);
   else
      cmd=args->CombineQuoted(start);

   FeedCmd(cmd);
   FeedCmd("\n");
   xfree(cmd);
}

CmdExec *CmdExec::FindQueue()
{
   for(CmdExec *scan=chain; scan; scan=scan->next)
   {
      if(scan->is_queue
      && !strcmp(this->session->GetConnectURL(FA::NO_PATH),
	         scan->session->GetConnectURL(FA::NO_PATH)))
	 return scan;
   }
   return 0;
}
