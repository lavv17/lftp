/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <ctype.h>
#include "SleepJob.h"
#include "CmdExec.h"
#include "misc.h"

SleepJob::SleepJob(const TimeInterval &when,FileAccess *s,char *what)
   : SessionJob(s), next_time(when)
{
   start_time=now;
   cmd=what;
   exit_code=0;
   done=false;
   saved_cwd=xgetcwd();
   repeat=false;
   repeat_count=0;
   exec=0;
}
SleepJob::~SleepJob()
{
   Delete(exec);
   xfree(cmd);
   xfree(saved_cwd);
}

int SleepJob::Do()
{
   if(Done())
      return STALL;

   if(waiting_num>0)
   {
      Job *j=FindDoneAwaitedJob();
      if(!j)
	 return STALL;
      if(!repeat)
      {
	 exit_code=j->ExitCode();
	 RemoveWaiting(j);
	 Delete(j);
	 exec=0;
	 done=true;
	 return MOVED;
      }
      repeat_count++;
      start_time=now;
      exec=(CmdExec*)j; // we are sure it is CmdExec.
      RemoveWaiting(j);
   }

   if(next_time.IsInfty())
   {
      TimeoutS(HOUR);  // to avoid deadlock message
      return STALL;
   }

   if(now-start_time>=next_time.Seconds())
   {
      if(cmd)
      {
	 if(!exec)
	 {
	    exec=new CmdExec(session);
	    session=0;
	    exec->SetParentFg(this);
	    exec->SetCWD(saved_cwd);
	    exec->AllocJobno();
	    exec->cmdline=(char*)xmalloc(3+strlen(cmd));
	    sprintf(exec->cmdline,"(%s)",cmd);
	 }
	 exec->FeedCmd(cmd);
	 exec->FeedCmd("\n");
	 AddWaiting(exec);
	 exec=0;
	 return MOVED;
      }
      done=true;
      return MOVED;
   }
   time_t diff=next_time.Seconds()-(now-start_time);
   if(diff>1024)
      diff=1024;  // prevent overflow
   TimeoutS(diff);
   return STALL;
}

void SleepJob::PrintStatus(int)
{
   if(repeat)
   {
      printf("\tRepeat count: %d\n",repeat_count);
      return;
   }
}

#define args (parent->args)
#define eprintf parent->eprintf
#define Clone() parent->session->Clone()
Job *cmd_sleep(CmdExec *parent)
{
   char *op=args->a0();
   if(args->count()!=2)
   {
      eprintf(_("%s: argument required. "),op);
   err:
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }
   const char *t=args->getarg(1);
   TimeInterval delay(t);
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
   const char *t=args->getarg(1);
   TimeInterval delay(1);
   if(t && isdigit((unsigned char)t[0]))
   {
      delay=TimeInterval(t);
      if(delay.Error())
      {
	 eprintf("%s: %s: %s.\n",op,t,delay.ErrorText());
	 return 0;
      }
      cmd_start=2;
   }

   char *cmd = (args->count()==cmd_start+1
	        ? args->Combine(cmd_start) : args->CombineQuoted(cmd_start));
   SleepJob *s=new SleepJob(delay,Clone(),cmd);
   s->Repeat();
   return s;
}

extern "C" {
#include "getdate.h"
}
Job *cmd_at(CmdExec *parent)
{
   int count=1;
   int cmd_start=0;
   int date_len=0;
   for(;;)
   {
      char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!strcmp(arg,"--"))
      {
	 cmd_start=count+1;
	 break;
      }
      date_len+=strlen(arg)+1;
      count++;
   }

#if 0
   char **av=(char**)xmemdup(args->GetV(),(count+1)*sizeof(char**));
   av[count]=0;
   time_t when=parsetime(count-1,av+1);
   xfree(av);
#endif
   char *date=args->Combine(1);
   date[date_len]=0;
   time_t now=time(0);
   time_t when=get_date(date,&now);
   xfree(date);

   if(when==0 || when==(time_t)-1)
      return 0;

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

   FileAccess *s = cmd ? Clone() : 0;
   return new SleepJob(when-now, s, cmd);
}
#undef args

#ifdef MODULE
CDECL void module_init()
{
   CmdExec::RegisterCommand("sleep",cmd_sleep);
   CmdExec::RegisterCommand("at",cmd_at);
}
#endif
