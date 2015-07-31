/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <ctype.h>
#include <stddef.h>
#include "SleepJob.h"
#include "CmdExec.h"
#include "misc.h"
#include "LocalDir.h"

SleepJob::SleepJob(const TimeInterval &when,FileAccess *s,LocalDirectory *cwd,char *what)
   : SessionJob(s), Timer(when), saved_cwd(cwd)
{
   cmd.set_allocated(what);
   exit_code=0;
   done=false;
   repeat=false;
   weak=false;
   repeat_count=0;
   max_repeat_count=0;
   continue_code=-1;
   break_code=-1;
}
SleepJob::~SleepJob()
{
}

int SleepJob::Do()
{
   int m=STALL;
   if(Done())
      return m;

   if(waiting.count()>0)
   {
      Job *j=FindDoneAwaitedJob();
      if(!j)
	 return m;
      exit_code=j->ExitCode();
      if(!repeat || (++repeat_count>=max_repeat_count && max_repeat_count)
      || exit_code==break_code || (continue_code!=-1 && exit_code!=continue_code))
      {
	 RemoveWaiting(j);
	 Delete(j);
	 exec=0;
	 done=true;
	 return MOVED;
      }
      Reset();
      exec=(CmdExec*)j; // we are sure it is CmdExec.
      RemoveWaiting(j);
      m=MOVED;
   }

   if(Stopped())
   {
      if(cmd)
      {
	 if(!exec)
	 {
	    exec=new CmdExec(session.borrow(),saved_cwd.borrow());
	    exec->AllocJobno();
	    exec->cmdline.vset("(",cmd.get(),")",NULL);
	 }
	 exec->FeedCmd(cmd);
	 exec->FeedCmd("\n");
	 AddWaiting(exec.borrow());
	 return MOVED;
      }
      done=true;
      return MOVED;
   }
   return m;
}

const char *SleepJob::Status()
{
   if(Stopped() || TimeLeft().Seconds()<=1)
      return "";

   if(IsInfty())
      return(_("Sleeping forever"));
   return xstring::cat(_("Sleep time left: "),
      TimeLeft().toString(TimeInterval::TO_STR_TRANSLATE),
      NULL);
}

xstring& SleepJob::FormatStatus(xstring& buf,int,const char *prefix)
{
   if(repeat)
      buf.appendf(_("\tRepeat count: %d\n"),repeat_count);
   const char *s=Status();
   if(s[0])
      buf.appendf("\t%s\n",s);
   return buf;
}
void SleepJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Stopped())
      Job::ShowRunStatus(s);
   else
   {
      s->Show("%s",Status());
      current->TimeoutS(1);
   }
}

void SleepJob::lftpMovesToBackground()
{
   if(weak || IsInfty() || (repeat && cmd[0]==0))
   {
      // terminate
      done=true;
   }
}

#define args (parent->args)
#define eprintf parent->eprintf
#define session (parent->session)
Job *cmd_sleep(CmdExec *parent)
{
   const char *op=args->a0();
   if(args->count()!=2)
   {
      eprintf(_("%s: argument required. "),op);
   err:
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }
   const char *t=args->getarg(1);
   TimeIntervalR delay(t);
   if(delay.Error())
   {
      eprintf("%s: %s: %s. ",op,t,delay.ErrorText());
      goto err;
   }
   return new SleepJob(delay);
}

Job *cmd_repeat(CmdExec *parent)
{
   const char *op=args->a0();
   int cmd_start=1;
   TimeIntervalR delay(1);
   int max_count=0;
   const char *delay_str=0;
   bool while_ok=false;
   bool until_ok=false;
   bool weak=false;
   int opt;

   static struct option repeat_opts[]=
   {
      {"delay",required_argument,0,'d'},
      {"count",required_argument,0,'c'},
      {"while-ok",no_argument,0,'o'},
      {"until-ok",no_argument,0,'O'},
      {"weak",no_argument,0,'w'},
      {0},
   };

   args->rewind();
   while((opt=args->getopt_long("+c:d:",repeat_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 max_count=atoi(optarg);
	 break;
      case('d'):
	 delay_str=optarg;
	 break;
      case('o'):
	 while_ok=true;
	 break;
      case('O'):
	 until_ok=true;
	 break;
      case('w'):
	 weak=true;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }
   if(!delay_str)
   {
      const char *t=args->getcurr();
      if(t && isdigit((unsigned char)t[0]))
      {
	 args->getnext();
	 delay_str=t;
      }
   }
   cmd_start=args->getindex();

   if(delay_str)
   {
      delay.Set(delay_str);
      if(delay.Error())
      {
	 eprintf("%s: %s: %s.\n",op,delay_str,delay.ErrorText());
	 return 0;
      }
   }

   char *cmd = (args->count()==cmd_start+1
	        ? args->Combine(cmd_start) : args->CombineQuoted(cmd_start));
   SleepJob *s=new SleepJob(delay,session->Clone(),parent->cwd->Clone(),cmd);
   s->Repeat(max_count);
   s->SetWeak(weak);
   if(while_ok)
      s->ContinueCode(0);
   if(until_ok)
      s->BreakCode(0);
   return s;
}

extern "C" {
#include "parse-datetime.h"
}
Job *cmd_at(CmdExec *parent)
{
   int count=1;
   int cmd_start=0;
   xstring date;
   for(;;)
   {
      const char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!strcmp(arg,"--"))
      {
	 cmd_start=count+1;
	 break;
      }
      if(date)
	 date.append(' ');
      date.append(arg);
      count++;
   }

   if(!date) {
      eprintf(_("%s: date-time specification missed\n"),args->a0());
      return 0;
   }

   struct timespec ts;
   if(!parse_datetime(&ts,date,0))
   {
      eprintf(_("%s: date-time parse error\n"),args->a0());
      return 0;
   }
   time_t when=ts.tv_sec;
   if(when<SMTask::now)
      when+=86400;

   char *cmd=0;
   if(cmd_start)
   {
      // two cases:
      //  1. at time -- "cmd; cmd..." (one argument)
      //  2. at time -- shell "cmd; cmd..." (several args)
      if(cmd_start==args->count()-1)
	 cmd=args->Combine(cmd_start);
      else
	 cmd=args->CombineQuoted(cmd_start);
   }

   if(!cmd)
      return new SleepJob(Time(when,0)-SMTask::now);

   return new SleepJob(Time(when,0)-SMTask::now,
			session->Clone(), parent->cwd->Clone(), cmd);
}
#undef args

#include "modconfig.h"
#ifndef MODULE_CMD_SLEEP
# define module_init cmd_sleep_module_init
#endif
CDECL void module_init()
{
   CmdExec::RegisterCommand("sleep",cmd_sleep,0,
	 N_("Usage: sleep <time>[unit]\n"
	 "Sleep for given amount of time. The time argument can be optionally\n"
	 "followed by unit specifier: d - days, h - hours, m - minutes, s - seconds.\n"
	 "By default time is assumed to be seconds.\n")
   );
   CmdExec::RegisterCommand("at",cmd_at);
   CmdExec::RegisterCommand("repeat",cmd_repeat,0,
	 N_("Repeat specified command with a delay between iterations.\n"
	 "Default delay is one second, default command is empty.\n"
	 " -c <count>  maximum number of iterations\n"
	 " -d <delay>  delay between iterations\n"
	 " --while-ok  stop when command exits with non-zero code\n"
	 " --until-ok  stop when command exits with zero code\n"
	 " --weak      stop when lftp moves to background.\n")
   );
}
