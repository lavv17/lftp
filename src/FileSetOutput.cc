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
#include "FileSet.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <locale.h>
#include <mbswidth.h>

CDECL_BEGIN
#include <filemode.h>
#include "human.h"
CDECL_END

#include "misc.h"
#include "ResMgr.h"

#include "FileSetOutput.h"
#include "ArgV.h"
#include "ColumnOutput.h"
#include "DirColors.h"
#include "FileGlob.h"
#include "CopyJob.h"


ResDecl	res_default_cls         ("cmd:cls-default",  "-F", FileSetOutput::ValidateArgv,ResMgr::NoClosure),
	res_default_comp_cls    ("cmd:cls-completion-default", "-FBa",FileSetOutput::ValidateArgv,ResMgr::NoClosure);

ResDecl res_time_style	("cmd:time-style", "%b %e  %Y|%b %e %H:%M", 0, ResMgr::NoClosure);

/* note: this sorts (add a nosort if necessary) */
void FileSetOutput::print(FileSet &fs, const JobRef<OutputJob>& o) const
{
   fs.Sort(sort, sort_casefold, sort_reverse);
   if(sort_dirs_first) fs.Sort(FileSet::DIRSFIRST, false, sort_reverse);

   ColumnOutput c;

   DirColors &col=*DirColors::GetInstance();

   const char *suffix_color = "";

   /* Most fields are only printed if at least one file has that
    * information; if no files have perm information, for example,
    * discard the entire field. */
   int have = fs.Have();

   for(int i = 0; fs[i]; i++) {
      const FileInfo *f = fs[i];
      if(!showdots && !list_directories &&
	    (!strcmp(basename_ptr(f->name),".") || !strcmp(basename_ptr(f->name),"..")))
	 continue;

      if(pat && *pat &&
	    fnmatch(pat, f->name, patterns_casefold? FNM_CASEFOLD:0))
	 continue;

      c.append();

      if((mode & PERMS) && (f->defined&FileInfo::MODE)) {
	 char mode[16];
	 memset(mode, 0, sizeof(mode));
	 strmode(f->mode, mode);
	 /* FIXME: f->mode doesn't have type info; it wouldn't
	  * be hard to fix that */
	 if(f->filetype == FileInfo::DIRECTORY) mode[0] = 'd';
	 else if(f->filetype == FileInfo::SYMLINK) mode[0] = 'l';
	 else mode[0] = '-';

	 c.add(mode, "");
      } else if(have & FileInfo::MODE) {
	 c.add("           ", "");
      }

      if((have & FileInfo::NLINKS) && (mode & NLINKS)) {
	 if(f->defined&f->NLINKS)
	    c.addf("%4i ", "", f->nlinks);
	 else
	    c.addf("%4i ", "", "");
      }

      if((have & FileInfo::USER) && (mode & USER)) {
	 c.addf("%-8.8s ", "", (f->defined&f->USER)? f->user: "");
      }

      if((have & FileInfo::GROUP) && (mode & GROUP)) {
	 c.addf("%-8.8s ", "", (f->defined&f->GROUP)? f->group: "");
      }

      if((mode & SIZE) && (have&FileInfo::SIZE)) {
	 char sz[LONGEST_HUMAN_READABLE + 2];
	 if((f->filetype == FileInfo::NORMAL || !size_filesonly)
	 && (f->defined&f->SIZE)) {
	    char buffer[LONGEST_HUMAN_READABLE + 1];
	    snprintf(sz, sizeof(sz), "%8s ",
	       human_readable (f->size, buffer, human_opts, 1,
		  output_block_size? output_block_size:1024));
	 } else {
	    snprintf(sz, sizeof(sz), "%8s ", ""); /* pad */
	 }
	 c.add(sz, "");
      }

      /* We use unprec dates; doing MDTMs for each file in ls is far too
       * slow.  If someone actually wants that (to get dates on servers with
       * unparsable dates, or more accurate dates), it wouldn't be
       * difficult.  If we did this, we could also support --full-time. */
      if((mode & DATE) && (have & f->DATE)) {
	 /* Consider a time to be recent if it is within the past six
	  * months.  A Gregorian year has 365.2425 * 24 * 60 * 60 ==
	  * 31556952 seconds on the average.  Write this value as an
	  * integer constant to avoid floating point hassles.  */
	 const int six_months_ago = SMTask::now.UnixTime() - 31556952 / 2;
	 bool recent = six_months_ago <= f->date;

	 const char *use_fmt=time_fmt;
	 if(!use_fmt)
	    use_fmt=ResMgr::Query("cmd:time-style",0);
	 if(!use_fmt || !*use_fmt)
	    use_fmt="%b %e  %Y\n%b %e %H:%M";

	 xstring_ca dt_mem(xstrftime(use_fmt, localtime (&f->date.ts)));
	 char *dt=strtok(dt_mem.get_non_const(),"\n|");
	 if(recent) {
	    char *dt1=strtok(NULL,"\n|");
	    if(dt1)
	       dt=dt1;
	 }
	 if (!(f->defined&f->DATE)) {
	    /* put an empty field; make sure it's the same width */
	    int wid = mbswidth(dt, 0);
	    dt = string_alloca(wid+1);
	    memset(dt, ' ', wid);
	    dt[wid] = 0;
	 }
	 c.addf("%s ", "", dt);
      }

      const char *nm = f->name;
      if(basenames) nm = basename_ptr(nm);
      c.add(nm, col.GetColor(f));

      if(classify)
	 c.add(FileInfoSuffix(*f), suffix_color);

      if((mode & LINKS) &&
	 f->filetype == FileInfo::SYMLINK &&
	 f->symlink) {
	 c.add(" -> ", "");

	 /* see if we have a file entry for the symlink */
	 FileInfo tmpfi;
	 FileInfo *lfi = fs.FindByName(f->symlink);

	 if(!lfi) {
	    /* create a temporary one */
	    tmpfi.SetName(f->symlink);
	    lfi = &tmpfi;
	 }

	 c.add(lfi->name, col.GetColor(lfi));
	 if(classify)
	    c.add(FileInfoSuffix(*lfi), suffix_color);
      }
   }

   c.print(o, single_column? 0:width, color);
}

const char *FileSetOutput::FileInfoSuffix(const FileInfo &fi) const
{
   if(!(fi.defined&fi.TYPE))
      return "";
   if(fi.filetype == FileInfo::DIRECTORY)
      return "/";
   else if(fi.filetype == FileInfo::SYMLINK)
      return "@";
   return "";
}

void FileSetOutput::config(const OutputJob *o)
{
   width = o->GetWidth();
   if(width == -1)
      width = 80;
   color = ResMgr::QueryTriBool("color:use-color", 0, o->IsTTY());
}

void FileSetOutput::long_list()
{
   single_column = true;
   mode = ALL;
   /* -l's default size is 1; otherwise 1024 */
   if(!output_block_size)
      output_block_size = 1;
}

const char *FileSetOutput::parse_res(const char *res)
{
   Ref<ArgV> arg(new ArgV("",res));
   const char *error=parse_argv(arg);
   if(error)
      return error;

   /* shouldn't be any non-option arguments */
   if(arg->count() > 1)
      return _("non-option arguments found");

   return 0;
}

const char *FileSetOutput::ValidateArgv(xstring_c *s)
{
   if(!*s) return NULL;

   FileSetOutput tmp;

   const char *ret = tmp.parse_res(*s);
   if(ret) return ret;

   return NULL;
}

int FileSetOutput::Need() const
{
   int need=FileInfo::NAME;
   if(mode & PERMS)
      need|=FileInfo::MODE;
//   if(mode & SIZE) /* this must be optional */
//      need|=FileInfo::SIZE;
//   if(mode & DATE) /* this too */
//      need|=FileInfo::DATE;
   if(mode & LINKS)
      need|=FileInfo::SYMLINK_DEF;
   if(mode & USER)
      need|=FileInfo::USER;
   if(mode & GROUP)
      need|=FileInfo::GROUP;
   if(need_exact_time)
      need|=FileInfo::DATE;

   return need;
}

#undef super
#define super SessionJob

clsJob::clsJob(FA *s, ArgV *a, FileSetOutput *_opts, OutputJob *_output):
   SessionJob(s),
   fso(_opts),
   args(a),
   done(0),
   use_cache(true),
   error(false),
   state(INIT)
{
   list_info=0;

   if(args->count() == 1)
      args->Add("");

   output=_output;
   AddWaiting(output);
}

int clsJob::Done()
{
   return done && output->Done();
}

int clsJob::Do()
{
   int m=STALL;

   if(output->Done())
      state=DONE;

   switch(state)
   {
   case INIT:
      state=START_LISTING;
      m=MOVED;

   case START_LISTING:
   {
      list_info=0;

      /* next: */
      mask.set(0);
      dir.set(args->getnext());
      if(!dir) {
	 /* done */
	 state=DONE;
	 return MOVED;
      }

      /* If the basename contains wildcards, set up the mask. */
      const char *bn = basename_ptr(dir);
      if(Glob::HasWildcards(bn)) {
	 /* The mask is the whole argument, not just the basename; this is
	  * because the whole relative paths will end up in the FileSet, and
	  * that's what this pattern will be matched against. */
	 mask.set(dir);
	 // leave the final / on the path, to prevent the dirname of
	 // "file/*" from being treated as a file
	 dir.truncate(bn-dir); // this can result in dir eq ""
      } else {
	 // no need to glob, just unquote metacharacters.
	 Glob::UnquoteWildcards(const_cast<char*>(bn));
      }

      list_info=new GetFileInfo(session, dir, fso->list_directories);
      list_info->UseCache(use_cache);
      list_info->Need(fso->Need());

      state=GETTING_LIST_INFO;
      m=MOVED;
   }
   case GETTING_LIST_INFO:
   {
      if(!list_info->Done())
	 return m;

      if(list_info->Error()) {
	 eprintf("%s\n", list_info->ErrorText());
	 error=true;
	 state=START_LISTING;
	 return MOVED;
      }

      /* one just finished */
      fso->pat.move_here(mask);
      FileSet *res = list_info->GetResult();

      if(res)
	 fso->print(*res, output);

      fso->pat.set(0);
      delete res;

      state=START_LISTING;
      return MOVED;
   }

   case DONE:
      if(!done)
      {
	 output->PutEOF();
	 done=true;
	 m=MOVED;
      }
      break;
   }
   return m;
}

void clsJob::SuspendInternal()
{
   super::SuspendInternal();
   if(list_info)
      list_info->SuspendSlave();
   session->SuspendSlave();
}

void clsJob::ResumeInternal()
{
   if(list_info)
      list_info->ResumeSlave();
   session->ResumeSlave();
   super::ResumeInternal();
}

void clsJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   if(fso->quiet)
      return;

   if(!output->ShowStatusLine(s))
      return;

   if(list_info && !list_info->Done())
   {
      const char *curr = args->getcurr();
      if(!*curr)
	 curr = ".";
      const char *stat = list_info->Status();
      if(*stat)
	 s->Show("`%s' %s %s", curr, stat, output->Status(s));
   }
   else
	 s->Show("%s", output->Status(s));
}

xstring& clsJob::FormatStatus(xstring& s,int v,const char *prefix)
{
   Job::FormatStatus(s,v,prefix);

   if(list_info)
   {
      const char *curr = args->getcurr();
      if(!*curr)
	 curr = ".";
      const char *stat = list_info->Status();
      if(*stat)
	 s.appendf("%s`%s' %s\n", prefix, curr, stat);
   }
   return s;
}
