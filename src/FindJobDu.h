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
#ifndef FINDJOBDU_H
#define FINDJOBDU_H

#include "FindJob.h"

class FinderJob_Du : public FinderJob
{
   SMTaskRef<IOBuffer> buf;

   /* We keep traversing deeper than this, but we never print a total
    * past this. */
   int max_print_depth;
   bool print_totals;
   int output_block_size;
   int human_opts;
   bool all_files;
   bool separate_dirs;
   bool file_count;

   bool success;

   long long tot_size;

   void Init(const char *d);

   struct stack_entry {
      xstring_c dir;
      long long size;
      stack_entry(const char *dir) : dir(dir), size(0) {}
   };
   RefArray<stack_entry> size_stack;

   Ref<ArgV> args;

   void print_size (long long n_blocks, const char *string);
   off_t BlockCeil(off_t size) const;

   // prepends last directory name
   const char *MakeFileName(const char *n);

   void Push (const char *d);
   void Pop();

public:
   FinderJob_Du(FileAccess *s,ArgV *a,FDStream *o);
   ~FinderJob_Du();
   int Done();

   void PrintTotals() { print_totals=true; }
   void SetBlockSize(int n,int ho) { output_block_size = n; human_opts = ho; }
   void PrintDepth(int n) { max_print_depth = n; }
   void AllFiles() { all_files=true; }
   void SeparateDirs() { separate_dirs=true; }
   void FileCount() { file_count=true; }

protected:
   /* virtuals */
   prf_res ProcessFile(const char *d,const FileInfo *fi);
   void ProcessList(FileSet *f);
   void Finish();
   void Enter(const char *d);
   void Exit();
};

#endif // FINDJOBDU_H
