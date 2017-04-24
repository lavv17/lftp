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
#include <errno.h>
#include "trio.h"
#include "mmvJob.h"
#include "misc.h"
#include "plural.h"
#include "FileGlob.h"

mmvJob::mmvJob(FileAccess *session,const ArgV *args,const char *t,FA::open_mode m1)
 : SessionJob(session), dst_dir(t), m(m1),
   remove_target(false), moved_count(0), error_count(0), done(false)
{
   op.set(args->a0());
   for(int i=args->getindex(); i<args->count(); i++)
      wcd.push(xstrdup(args->getarg(i)));
}

void mmvJob::doOpen() const
{
   if(remove_target && session->OpenMode()!=FA::REMOVE)
      session->Open(curr_dst,FA::REMOVE);
   else
      session->Open2(curr_src,curr_dst,m);
}

int mmvJob::Do()
{
   int m=STALL;

   if(Done())
      return STALL;

   if(glob) {
      // handle ongoing globbing
      if(glob->Error())
      {
	 fprintf(stderr,"%s: %s: %s\n",cmd(),glob->GetPattern(),glob->ErrorText());
	 error_count++;
	 glob=0;
	 return MOVED;
      }
      if(!glob->Done())
	 return m;
      // have globbed file set now
      FileSet *files=glob->GetResult();
      files->rewind();
      for(FileInfo *fi=files->curr(); fi; fi=files->next())
	 src.push(fi->name.borrow());
      glob=0;
   }

   if(!curr_src) {
      // pick next file/wildcard to work on
      if(src.count()) {
	 curr_src.set(src.next());
	 curr_dst.set(dir_file(dst_dir,basename_ptr(curr_src)));
      } else if(wcd.count()) {
	 glob=session->MakeGlob(wcd.next());
	 glob->Roll();
	 return MOVED;
      } else {
	 // no more files to move
	 done=true;
	 return MOVED;
      }
   }

   if(!session->IsOpen())
      doOpen();

   int res=session->Done();
   if(res==FA::IN_PROGRESS || res==FA::DO_AGAIN)
      return m;
   if(res!=FA::OK && !isRemoving()) {
      fprintf(stderr,"%s: %s\n",cmd(),session->StrError(res));
      error_count++;
      session->Close();
      curr_src.unset();
      return MOVED;
   }
   if(isRemoving()) {
      doOpen(); // do the real move now.
      return MOVED;
   }
   session->Close();
   moved_count++;
   curr_src.unset();
   return MOVED;
}

xstring& mmvJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   SessionJob::FormatStatus(s,v,prefix);
   if(Done())
      return s;
   if(glob)
      s.appendf("%sglob %s [%s]\n",prefix,glob->GetPattern(),glob->Status());
   else if(isRemoving())
      s.appendf("%srm %s [%s]\n",prefix,curr_dst.get(),session->CurrentStatus());
   else
      s.appendf("%s%s %s=>%s [%s]\n",prefix,cmd(),curr_src.get(),curr_dst.get(),session->CurrentStatus());
   return s;
}

void  mmvJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(Done())
      return;
   if(glob)
      s->Show("glob %s [%s]",glob->GetPattern(),glob->Status());
   else if(isRemoving())
      s->Show("rm %s [%s]",curr_dst.get(),session->CurrentStatus());
   else
      s->Show("%s %s=>%s [%s]",cmd(),curr_src.get(),curr_dst.get(),session->CurrentStatus());
}

void  mmvJob::SayFinal()
{
   if(error_count>0)
      printf(plural("%s: %d error$|s$ detected\n",error_count),cmd(),error_count);
   if(m==FA::RENAME)
      printf(plural("%s: %d file$|s$ moved\n",moved_count),cmd(),moved_count);
   else
      printf(plural("%s: %d file$|s$ linked\n",moved_count),cmd(),moved_count);
}
