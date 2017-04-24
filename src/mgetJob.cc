/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "mgetJob.h"
#include "misc.h"
#include "ArgV.h"
#include "url.h"

void mgetJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(glob)
   {
      s->Show("%s",glob->Status());
      return;
   }
   GetJob::ShowRunStatus(s);
}
xstring& mgetJob::FormatStatus(xstring& buf,int v,const char *prefix)
{
   if(glob)
   {
      SessionJob::FormatStatus(buf,v,prefix);
      const char *s=glob->Status();
      if(!s || !s[0])
	 return buf;
      return buf.appendf("\t%s\n",s);
   }
   return GetJob::FormatStatus(buf,v,prefix);
}

mgetJob::mgetJob(FileAccess *session,const ArgV *a,bool c,bool md)
   : GetJob(session,new ArgV(a->a0()),c),
   local_session(FileAccess::New("file"))
{
   make_dirs=md;
   for(int i=a->getindex(); i<a->count(); i++)
      wcd.push(xstrdup(a->getarg(i)));
}

int mgetJob::Do()
{
   if(glob) {
      // handle ongoing globbing
      if(glob->Error())
      {
	 fprintf(stderr,"%s: %s: %s\n",op,glob->GetPattern(),glob->ErrorText());
      glob_error:
	 count++;
	 errors++;
	 glob=0;
	 return MOVED;
      }
      if(!glob->Done())
	 return STALL;
      // have globbed file set now
      FileSet *files=glob->GetResult();
      if(files->get_fnum()==0)
      {
	 fprintf(stderr,_("%s: %s: no files found\n"),op,glob->GetPattern());
	 goto glob_error;
      }
      files->rewind();
      for(FileInfo *fi=files->curr(); fi; fi=files->next()) {
	 xstring& src=fi->name;
	 args->Append(src);
	 args->Append(output_file_name(src,0,!reverse,output_dir,make_dirs));
      }
      glob=0;
   }

   if(wcd.count()>0) {
      glob=new GlobURL(reverse?local_session:session,
			wcd.next(),GlobURL::FILES_ONLY);
      return MOVED;
   }

   return GetJob::Do();
}

mgetJob::~mgetJob()
{
}
