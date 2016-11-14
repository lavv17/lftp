/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILESETOUTPUT_H
#define FILESETOUTPUT_H

#include "FileSet.h"
#include "buffer.h"
#include "keyvalue.h"
#include "FileCopy.h"
#include "GetFileInfo.h"
#include "CopyJob.h"
#include "OutputJob.h"
#include "Job.h"

class StatusLine;

class FileSetOutput {
   const char *FileInfoSuffix(const FileInfo &fi) const;

public:
   bool classify; // add / (dir) @ (link)
   // TODO: extra-optional * for exec? maybe not, some servers stick +x on everything

   int width; // width to output, 0 to force one column
   bool color;

   enum { NONE=0, PERMS = 0x1, SIZE = 0x2, DATE = 0x4, LINKS = 0x8,
	  USER = 0x10, GROUP = 0x20, NLINKS = 0x40,

	  ALL=PERMS|SIZE|DATE|LINKS|USER|GROUP|NLINKS
   };
   int mode;

   xstring_c pat;
   xstring_c time_fmt;

   bool basenames;
   bool showdots;
   bool quiet;
   bool patterns_casefold;
   bool sort_casefold;
   bool sort_reverse;
   bool sort_dirs_first;
   bool size_filesonly;
   bool single_column;
   bool list_directories;
   bool need_exact_time;
   int output_block_size;
   int human_opts;

   FileSet::sort_e sort;
   FileSetOutput(): classify(0), width(0), color(false), mode(NONE),
      basenames(false), showdots(false),
      quiet(false), patterns_casefold(false), sort_casefold(false), sort_reverse(false),
      sort_dirs_first(false), size_filesonly(false), single_column(false),
      list_directories(false), need_exact_time(false), output_block_size(0),
      human_opts(0), sort(FileSet::BYNAME) { }

   void long_list();
   void config(const OutputJob *fd);
   const char *parse_argv(const Ref<ArgV>& a);
   const char *parse_res(const char *res);
   static const char *ValidateArgv(xstring_c *s);
   int Need() const;

   void print(FileSet &fs, const JobRef<OutputJob>& o) const;
};

/* Job interface to FileSetOutput */
class clsJob : public SessionJob
{
   JobRef<OutputJob> output;
   Ref<FileSetOutput> fso;
   Ref<ArgV> args;
   SMTaskRef<ListInfo> list_info;
   xstring_c dir;
   xstring_c mask;
   bool done;
   bool use_cache;
   bool error;

   enum { INIT, START_LISTING, GETTING_LIST_INFO, DONE } state;

public:
   clsJob(FA *s, ArgV *a, FileSetOutput *_opts, OutputJob *output);
   int Done();
   int Do();

   void UseCache(bool y=true) { use_cache=y; }

   void Fg() { session->SetPriority(1); output->Fg(); }
   void Bg() { session->SetPriority(0); output->Bg(); }
   void SuspendInternal();
   void ResumeInternal();
   int ExitCode() { return error||output->Error() ? 1 : 0; }

   void ShowRunStatus(const SMTaskRef<StatusLine>&);
   xstring& FormatStatus(xstring&,int v,const char *);
};

#endif
