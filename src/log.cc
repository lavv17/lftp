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

#include <stdarg.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "xstring.h"
#include "log.h"
#include "SMTask.h"

Ref<Log> Log::global;

static ResType log_vars[] = {
   {"log:enabled",   "no", ResMgr::BoolValidate},
   {"log:level",     "9",  ResMgr::UNumberValidate},
   {"log:show-time", "no", ResMgr::BoolValidate},
   {"log:show-pid",  "no", ResMgr::BoolValidate},
   {"log:show-ctx",  "no", ResMgr::BoolValidate},
   {"log:file",	     "",   ResMgr::FileCreatable},
   {"log:max-size",  "1M", ResMgr::UNumberValidate},
   {"log:prefix-recv","<--- "},
   {"log:prefix-send","---> "},
   {"log:prefix-note","---- "},
   {"log:prefix-error","**** "},
   {0}
};
static ResDecls log_vars_register(log_vars);

Log::Log(const char *name)
   : name(name)
{
   output=-1;
   need_close_output=false;
   tty_cb=0;
   enabled=false;
   level=0;
   tty=false;
   show_pid=true;
   show_time=true;
   show_context=true;
   at_line_start=true;
   Reconfig(0);
}

bool Log::WillOutput(int l)
{
   if(!enabled || l>level || output==-1)
      return false;
   if(tty)
   {
      pid_t pg=tcgetpgrp(output);
      if(pg!=(pid_t)-1 && pg!=getpgrp())
	 return false;
   }
   return true;
}

void Log::Write(int l,const char *s,int len)
{
   if(!WillOutput(l))
      return;
   DoWrite(s,len);
}
void Log::DoWrite(const char *s,int len)
{
   if(len==0)
      return;
   if(buf.length()==0 || buf.last_char()=='\n') {
      if(show_pid)
	 buf.appendf("[%ld] ",(long)getpid());
      if(show_time)
	 buf.append(SMTask::now.IsoDateTime()).append(' ');
      if(show_context)
      {
	 const char *ctx=SMTask::GetCurrentLogContext();
	 if(ctx)
	    buf.append(ctx).append(' ');
      }
   }
   buf.append(s,len);
   if(buf.last_char()!='\n')
      return;

   if(tty_cb && tty)
      tty_cb();
   int res=write(output,buf,buf.length());
   if(res==-1) {
      if(E_RETRY(errno))
	 return;
      ResMgr::Set("log:enabled",name,"no");
      return;
   }
   if(res==(int)buf.length())
      buf.truncate();
   else
      buf.set_substr(0,res,"",0);
}

void Log::Format(int l,const char *f,...)
{
   if(!WillOutput(l))
      return;

   va_list v;
   va_start(v,f);
   DoWrite(xstring::vformat(f,v));
   va_end(v);
}

void Log::vFormat(int l,const char *f,va_list v)
{
   if(!WillOutput(l))
      return;

   DoWrite(xstring::vformat(f,v));
}

void Log::Cleanup()
{
   global=0;
}
Log::~Log()
{
   CloseOutput();
}

void Log::SetOutput(int o,bool need_close)
{
   CloseOutput();
   output=o;
   need_close_output=need_close;
   if(output!=-1)
      tty=isatty(output);
}

void Log::Reconfig(const char *n) {
   enabled=QueryBool("log:enabled");
   level=Query("log:level");
   show_time=QueryBool("log:show-time");
   show_pid=QueryBool("log:show-pid");
   show_context=QueryBool("log:show-ctx");

   if(!n || !strcmp(n,"log:file")) {
      const char *file=Query("log:file");
      int fd=2;
      bool need_close_fd=false;
      if(file && *file) {
	 struct stat st;
	 if(stat(file,&st)!=-1) {
	    if(st.st_size > long(Query("log:max-size"))) {
	       debug((9,"rotating log %s\n",file));
	       if(rename(file,xstring::cat(file,".old",NULL))==-1)
		  debug((1,"rename(%s): %s\n",file,strerror(errno)));
	    }
	 }
	 fd=open(file,O_WRONLY|O_CREAT|O_APPEND|O_NONBLOCK,0600);
	 if(fd==-1) {
	    perror(file);
	    fd=2;
	 } else {
	    need_close_fd=true;
	    fcntl(fd,F_SETFD,FD_CLOEXEC);
	 }
      }
      if(fd!=output)
	 SetOutput(fd,need_close_fd);
   }
}
