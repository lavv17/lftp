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
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include "CmdExec.h"
#include "xmalloc.h"
#include "xstring.h"
#include "SignalHook.h"
#include "alias.h"
extern "C" {
#include <readline/readline.h>
}

#define super SessionJob

static ResDecl
   res_long_running	   ("cmd:long-running","30",ResMgr::UNumberValidate,0),
   res_remote_completion   ("cmd:remote-completion","on",ResMgr::BoolValidate,0),
   res_completion_use_ls   ("cmd:ls-in-completion","on",ResMgr::BoolValidate,0),
   res_prompt		   ("cmd:prompt","lftp> ",0,0),
   res_default_ls	   ("cmd:ls-default","",0,0),
   res_csh_history	   ("cmd:csh-history","off",ResMgr::BoolValidate,0),
   res_save_passwords	   ("bmk:save-passwords","no",ResMgr::BoolValidate,0);

CmdExec	 *CmdExec::cwd_owner=0;

void  CmdExec::SaveCWD()
{
   if(cwd==0)
      cwd=(char*)xmalloc(1024);
   if(getcwd(cwd,1024)==0)
   {
      // A bad case, but we can do nothing ( xgettext:c-format )
      eprintf(_("Warning: getcwd() failed\n"));
      free(cwd);
      cwd=0;
   }
   else
   {
      if(cwd_owner==0)
	 cwd_owner=this;
   }
}
void  CmdExec::RestoreCWD()
{
   if(cwd==0 || cwd_owner==this)
      return;
   if(chdir(cwd)==0)
      cwd_owner=this;
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
//    printf("after feed: next_cmd=`%s'\n",next_cmd);
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

   for(c=cmd_table; c->name; c++)
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

   const struct cmd_rec *c;
   const char *cmd_name=args->getarg(0);
   int part=find_cmd(cmd_name,&c);
   if(part<=0)
      eprintf(_("Unknown command `%s'.\n"),cmd_name);
   else if(part>1)
      eprintf(_("Ambiguous command `%s'.\n"),cmd_name);
   else
   {
      RestoreCWD();

      args->setarg(0,c->name); // in case it was abbreviated

      if(cmd==0)
	 cmd=args->Combine();

      Job *(CmdExec::*func)()=c->func;
      waiting=(this->*func)();
      if(waiting==this) // builtin
	 return;
      if(waiting)
      {
	 waiting->parent=this;
	 if(waiting->jobno<0)
	    waiting->AllocJobno();
	 if(cmd && waiting->cmdline==0)
	 {
	    waiting->cmdline=cmd;
	    cmd=0;
      	 }
	 if(fg && !background)
	    waiting->Fg();
      }
      if(background)
      {
	 exit_code=0;
	 if(waiting)
	 {
	    while(waiting->Do()==MOVED)
	       ;
	    if(!waiting->Done())
	    {
	       waiting->Bg();
	       if(interactive)
	       {
		  printf("[%d] %s &\n",waiting->jobno,
		     waiting->cmdline?waiting->cmdline:"?");
		  waiting->PrintStatus(1);
	       }
	       waiting=0;
	    }
	 }
      } // background
   }
}

void CmdExec::ExecParsed(ArgV *a,FDStream *o,bool b)
{
   bool old_running=running;
   running=true; // disable reentering

   if(args)
      delete args;
   args=a;
   if(cmd)
      free(cmd);
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
   return(waiting==0 && (next_cmd==0 || *next_cmd==0 || partial_cmd));
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

int CmdExec::Do()
{
   int m=STALL;

   if(waiting==this)
   {
      int res;
      switch(builtin)
      {
      case(BUILTIN_CD):
	 res=session->Done();
	 if(res==Ftp::OK)
	 {
	    // done
	    if(status_line)
	       status_line->Show("");
	    if(interactive)
	    {
	       const char *cwd=session->GetCwd();
	       eprintf(_("cd ok, cwd=%s\n"),cwd?cwd:"~");
	       cwd_history.Set(session,old_cwd);
	    }
	    session->Close();
	    exit_code=0;
	    waiting=0;
	    beep_if_long();
	    return MOVED;
	 }
	 if(res<0)
	 {
	    // error
	    if(status_line)
	       status_line->Show("");
	    eprintf("%s: %s\n",args->getarg(0),session->StrError(res));
	    session->Close();
	    waiting=0;
	    beep_if_long();
	    exit_code=1;
	    return MOVED;
	 }
	 break;

      case(BUILTIN_OPEN):
	 res=session->Done();
	 if(res==Ftp::OK)
	 {
	    if(status_line)
	       status_line->Show("");
	    session->Close();
	    waiting=0;
	    beep_if_long();
	    exit_code=0;

	    return MOVED;
	 }
	 if(res<0)
	 {
	    if(status_line)
	       status_line->Show("");
	    eprintf("%s: %s\n",args->getarg(0),session->StrError(res));
	    session->Close();
	    waiting=0;
	    beep_if_long();
	    exit_code=1;
	    return MOVED;
	 }
	 break;
      }
      if(interactive)
      {
	 if(SignalHook::GetCount(SIGINT))
	 {
	    if(status_line)
	       status_line->WriteLine(_("Interrupt"));
	    return AcceptSig(SIGINT);
	 }
	 if(SignalHook::GetCount(SIGHUP))
	 {
	    interactive=0;
	    return MOVED;
	 }
      }
      if(status_line)
	 waiting->ShowRunStatus(status_line);
      return STALL;
   }

   if(waiting)
   {
      if(waiting->Done())
      {
	 waiting->Bg();
	 if(status_line)
	    status_line->Show("");
 	 if(interactive)
	    waiting->SayFinal(); // final phrase like 'rm succeed'
	 exit_code=waiting->ExitCode();
	 delete waiting;
	 waiting=0;
	 beep_if_long();
      	 return MOVED;
      }
      if(interactive)
      {
	 if(SignalHook::GetCount(SIGINT))
	 {
	    waiting->Bg();
	    SignalHook::ResetCount(SIGINT);
	    if(status_line)
	       status_line->WriteLine(_("Interrupt"));
	    return AcceptSig(SIGINT);
	 }
	 if(SignalHook::GetCount(SIGTSTP))
	 {
	    waiting->Bg();
	    if(status_line)
	       status_line->WriteLine("[%d] %s &",waiting->jobno,waiting->cmdline);
	    waiting->PrintStatus(1);
	    exit_code=0;
	    waiting=0;
	    return MOVED;
	 }
	 if(SignalHook::GetCount(SIGHUP))
	 {
	    interactive=0;
	    return MOVED;
	 }
      }
      if(status_line)
	 waiting->ShowRunStatus(status_line);
      return STALL;
   }

   if(!interactive)
      BuryDoneJobs();

try_get_cmd:
   if(next_cmd==0 || *next_cmd==0 || partial_cmd)
   {
      if(feeder)
      {
	 if(interactive && !partial_cmd)
	 {
	    ListDoneJobs();
	    BuryDoneJobs();
	 }
	 char *prompt=MakePrompt();
	 char *cmd=feeder->NextCmd(this,prompt);
	 if(cmd==0)
	 {
	    if(next_cmd && *next_cmd && partial_cmd)
	       fprintf(stderr,_("Warning: discarding incomplete command\n"));
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
   if(waiting && waiting!=this)
      waiting->ShowRunStatus(s);
   else if(waiting==this)
   {
      switch(builtin)
      {
      case(BUILTIN_CD):
	 s->Show("cd `%s' [%s]",args->getarg(1),session->CurrentStatus());
	 break;
      case(BUILTIN_OPEN):
	 s->Show("open `%s' [%s]",session->GetHostName(),session->CurrentStatus());
      	 break;
      }
   }
   else if(Done())
      s->Show("");
}

void CmdExec::PrintStatus(int v)
{
   SessionJob::PrintStatus(v);
   if(v<1)
      return;
   if(waiting==this)
   {
      char *s=args->Combine();
      printf(_("\tExecuting builtin `%s' [%s]\n"),s,session->CurrentStatus());
      free(s);
      return;
   }
   if(waiting)
   {
      printf(_("\tWaiting for job [%d] to terminate\n"),waiting->jobno);
      return;
   }
   // xgettext:c-format
   printf(_("\tRunning\n"));
}

CmdExec::CmdExec(FileAccess *f) : SessionJob(f)
{
   cmd=0;
   args=0;
   waiting=0;
   output=0;
   background=false;

   interactive=false;
   status_line=0;
   feeder=0;
   used_aliases=0;

   next_cmd=cmd_buf=0;
   partial_cmd=false;
   alias_field=0;
   default_output=0;
   condition=COND_ANY;
   prev_exit_code=0;
   exit_code=0;

   cwd=0;
   SaveCWD();

   var_ls=xstrdup("");
   var_prompt=xstrdup("lftp> ");
   remote_completion=false;
   completion_use_ls=true;
   long_running=0;
   csh_history=false;
   save_passwords=false;

   Reconfig();

   start_time=0;
   old_cwd=0;
   old_lcwd=0;

   debug_file=0;
}

CmdExec::~CmdExec()
{
   CloseDebug();
   if(debug_shell==this)
      debug_shell=0; // unfortunately, we lose debug with this shell
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

   char *scan=var_prompt;
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

      while(prompt_size<=store-prompt+(int)strlen(to_add))
	 prompt=(char*)xrealloc(prompt,prompt_size*=2);

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

void CmdExec::Reconfig()
{
   long_running = res_long_running.Query(0);
   remote_completion = res_remote_completion.Query(0);
   completion_use_ls = res_completion_use_ls.Query(0);
   csh_history = res_csh_history.Query(0);
   xfree(var_ls);
   var_ls=xstrdup(res_default_ls.Query(session->GetHostName()));
   xfree(var_prompt);
   var_prompt=xstrdup(res_prompt.Query(0));
   save_passwords=res_save_passwords.Query(0);
}

void CmdExec::vfprintf(FILE *file,const char *f,va_list v)
{
   if(parent)
      parent->vfprintf(file,f,v);
   else
   {
      if(status_line)
	 status_line->Show("");
      ::vfprintf(file,f,v);
   }
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
   if(waiting==this) // builtin
   {
      session->Close();
      waiting=0;
      exit_code=1;
      return MOVED;
   }
   if(waiting)
   {
      int res=waiting->AcceptSig(sig);
      if(res==WANTDIE)
      {
	 exit_code=1;
	 delete waiting;
	 waiting=0;
      }
      return MOVED;
   }
   return WANTDIE;
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

void CmdExec::CloseDebug()
{
   if(debug_file)
   {
      fclose(debug_file);
      debug_file=0;
   }
}

int CmdExec::OpenDebug(const char *file)
{
   CloseDebug();
   debug_file=fopen(optarg,"a");
   if(!debug_file)
   {
      perror(optarg);
      return -1;
   }
   fcntl(fileno(debug_file),F_SETFD,FD_CLOEXEC);
   return 0;
}

void CmdExec::Fg()
{
   super::Fg();
   if(waiting)
      waiting->Fg();
}
void CmdExec::Bg()
{
   if(waiting)
      waiting->Bg();
   super::Bg();
}
