/*
 * lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include <assert.h>

#include "FindJobDu.h"
#include "CmdExec.h"
#include "misc.h"

CDECL_BEGIN
#include "human.h"
CDECL_END

FinderJob_Du::FinderJob_Du(FileAccess *s,ArgV *a,FDStream *o):
	FinderJob(s)
{
   args=a;
   op=args->a0();

   buf=new IOBufferFDStream(o,IOBuffer::PUT);

   Need(FileInfo::SIZE);

   /* defaults */
   max_print_depth = -1;
   print_totals = false;
   output_block_size = 1024;
   all_files = false;
   separate_dirs = false;
   file_count = false;

   tot_size=0;
   size_stack=0;
   stack_ptr=-1;
   success=false;

   Init(a->getcurr());
}

FinderJob_Du::~FinderJob_Du()
{
   Delete(buf);
   delete args;
   while(stack_ptr >= 0)
      Pop();
   xfree(size_stack);
}

/* process a new directory */
void FinderJob_Du::Init(const char *d)
{
   NextDir(d);
}

int FinderJob_Du::Done()
{
   return FinderJob::Done() && args->getcurr()==0 && buf->Done();
}

void FinderJob_Du::Finish()
{
   /* if there's anything left, we had an error; clear the stack */
   if(stack_ptr != -1) {
      while(stack_ptr >= 0)
	 Pop();
   } else success = true; /* at least one succeeded */

   /* next? */
   char *d=args->getnext();
   if(d) {
      /* we have another argument */
      Init(d);
      return;
   }

   /* we're done */
   if (print_totals) /* don't print totals on error */
      print_size(tot_size, _("total"));
   buf->PutEOF();
}

FinderJob::prf_res FinderJob_Du::ProcessFile(const char *d,const FileInfo *fi)
{
   if(buf->Broken())
      return PRF_FATAL;
   if(buf->Error())
   {
      eprintf("%s: %s\n",op,buf->ErrorText());
      return PRF_FATAL;
   }
   if(fg_data==0)
      fg_data=buf->GetFgData(fg);
   if(buf->Size()>0x10000)
      return PRF_LATER;

   if(fi->filetype==fi->DIRECTORY)
      return PRF_OK; /* don't care */
   if(!file_count && !(fi->defined&fi->SIZE))
      return PRF_OK; /* can't count this one */

   /* add this file to the current dir */
   int add = fi->size;
   if (file_count)
      add = 1;
   if(stack_ptr != -1)
      size_stack[stack_ptr].size += add;
   tot_size += add;

   if(all_files || stack_ptr == -1) {
      /* this is <, where Pop() is <=, since the file counts in depth */
      if(max_print_depth == -1 || stack_ptr < max_print_depth)
	 print_size(fi->size,
	       dir_file(stack_ptr == -1? "":size_stack[stack_ptr].dir,fi->name));
   }

   return PRF_OK;
}

void FinderJob_Du::ProcessList(FileSet *f)
{
   f->Sort(FileSet::BYNAME, true);
}

/* push a directory onto the stack */
void FinderJob_Du::Push (const char *d)
{
   stack_ptr++;
   size_stack = (stack *) xrealloc(size_stack, (stack_ptr+1) * sizeof(stack));

   const char *combine;
   combine = dir_file(stack_ptr > 0? size_stack[stack_ptr-1].dir:"", d);

   size_stack[stack_ptr].dir = xstrdup(combine);
   size_stack[stack_ptr].size = 0;
}

/* pop a directory off the stack, combining as necessary */
void FinderJob_Du::Pop()
{
   assert(stack_ptr!=-1); /* no underflows */

   /* merge directory's size with its parent */
   if(!separate_dirs && stack_ptr > 0)
      size_stack[stack_ptr-1].size += size_stack[stack_ptr].size;

   xfree(size_stack[stack_ptr].dir);
   stack_ptr--;
   /* no need to actually shrink */
}

void FinderJob_Du::print_size (long long n_blocks, const char *string)
{
   char buffer[LONGEST_HUMAN_READABLE + 1];
   /* We get blocks in bytes, since we don't know the remote system's
    * block size. */
   buf->Format("%s\t%s\n",
	 human_readable_inexact (n_blocks, buffer, 1,
	    output_block_size, human_ceiling),
	 string);
}

/* finished a directory; print it if necessary and pop it off the stack */
void FinderJob_Du::Exit()
{
   /* print the dir */
   if(max_print_depth == -1 || stack_ptr <= max_print_depth)
      print_size(size_stack[stack_ptr].size, size_stack[stack_ptr].dir);

   Pop();
}

void FinderJob_Du::Enter(const char *d)
{
   Push(d);
}
