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

SleepJob::SleepJob(time_t when,FileAccess *s,char *what)
   : SessionJob(s)
{
   the_time=when;
   cmd=what;
   exit_code=0;
   done=false;
   saved_cwd=xgetcwd();
   repeat=false;
   repeat_delay=1;
   repeat_count=0;
   exec=0;
}
SleepJob::~SleepJob()
{
   if(exec && exec!=waiting)
      delete exec;
   xfree(cmd);
   xfree(saved_cwd);
}

int SleepJob::Do()
{
   if(Done())
      return STALL;

   if(waiting)
   {
      if(!waiting->Done())
	 return STALL;
      if(!repeat)
      {
	 exit_code=waiting->ExitCode();
	 delete waiting;
	 waiting=0;
	 exec=0;
	 done=true;
	 return MOVED;
      }
      repeat_count++;
      the_time=now+repeat_delay;
      waiting=0;
   }

   if(now>=the_time)
   {
      if(cmd)
      {
	 if(!exec)
	 {
	    exec=new CmdExec(session);
	    session=0;
	    exec->parent=this;
	    exec->SetCWD(saved_cwd);
	    exec->AllocJobno();
	    exec->cmdline=(char*)xmalloc(2+strlen(cmd));
	    sprintf(exec->cmdline,"(%s)",cmd);
	 }
	 waiting=exec;
	 exec->FeedCmd(cmd);
	 exec->FeedCmd("\n");
	 return MOVED;
      }
      done=true;
      return MOVED;
   }
   time_t diff=the_time-now;
   if(diff>1024)
      diff=1024;  // prevent overflow
   block+=TimeOut(diff*1000);
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
   time_t delay=decode_delay(t);
   if(delay==(time_t)-1)
   {
      eprintf(_("%s: invalid delay. "),op);
      goto err;
   }
   return new SleepJob(time(0)+delay);
}

Job *cmd_repeat(CmdExec *parent)
{
   const char *op=args->a0();
   int cmd_start=1;
   const char *t=args->getarg(1);
   time_t delay=1;
   if(t && isdigit((unsigned char)t[0]))
   {
      delay=decode_delay(t);
      if(delay==(time_t)-1)
      {
	 eprintf(_("%s: invalid delay. "),op);
	 eprintf("\n");
	 return 0;
      }
      cmd_start=2;
   }

   char *cmd = (args->count()==cmd_start+1
	        ? args->Combine(cmd_start) : args->CombineQuoted(cmd_start));
   SleepJob *s=new SleepJob(time(0),Clone(),cmd);
   s->Repeat(delay);
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
   return new SleepJob(when, s, cmd);
}
#undef args

#ifdef MODULE
CDECL void module_init()
{
   CmdExec::RegisterCommand("sleep",cmd_sleep);
   CmdExec::RegisterCommand("at",cmd_at);
}
#endif
