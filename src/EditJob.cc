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
#include <sys/types.h>
#include <sys/stat.h>

#include "EditJob.h"
#include "GetJob.h"
#include "SysCmdJob.h"

void EditJob::Finish(int e)
{
   exit_code=e;
   done=true;
   if(!keep)
      unlink(temp_file);
}

int EditJob::HandleJob(JobRef<Job>& j,bool fail)
{
   if(!j->Done())
      return STALL;
   if(j->ExitCode() && fail)
      Finish(1);
   RemoveWaiting(j);
   return MOVED;
}

int EditJob::Do()
{
   int m=STALL;
   if(Done())
      return m;
   if(put) {
      if(!HandleJob(put))
	 return m;
      if(done)
	 return MOVED;
      Finish(0);
      return MOVED;
   }
   if(editor) {
      if(!HandleJob(editor))
	 return m;
      if(done)
	 return MOVED;
      struct stat st;
      int res=stat(temp_file,&st);
      if(res<0) {
	 perror(temp_file);
	 Finish(1);
	 return MOVED;
      }
      if(st.st_mtime!=mtime) {
	 ArgV *args=new ArgV("put");
	 args->Append(temp_file);
	 args->Append(file);
	 GetJob *j=new GetJob(session->Clone(),args,false);
	 j->Reverse();
	 put=j;
	 AddWaiting(put);
	 return MOVED;
      }
      Finish(0);
      return MOVED;
   }
   if(get) {
      if(!HandleJob(get,false))
	 return m;
      if(done)
	 return MOVED;
      struct stat st;
      int res=stat(temp_file,&st);
      mtime=(res>=0?st.st_mtime:NO_DATE);
      const char *bin=getenv("EDITOR");
      if (bin==NULL)
         bin="vi";
      xstring cmd(bin);
      cmd.append(" ");
      cmd.append(shell_encode(temp_file));
      editor=new SysCmdJob(cmd);
      AddWaiting(editor);
      return MOVED;
   }
   else {
      ArgV *args=new ArgV("get");
      args->Append(file);
      args->Append(temp_file);
      get=new GetJob(session->Clone(),args,false);
      AddWaiting(get);
      return MOVED;
   }
   return m;
}
