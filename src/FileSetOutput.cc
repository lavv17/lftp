/* lftp and utils
 *
 * Copyright (c) 2001-2002 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * Portions from GNU fileutils.
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
#include <filemode.h>


#include <config.h>
#include "FileSet.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fnmatch.h>
#include <mbswidth.h>

CDECL_BEGIN
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


ResDecl	res_default_cls         ("cmd:cls-default",  "-F", FileSetOutput::ValidateArgv,0),
	res_default_comp_cls    ("cmd:cls-completion-default", "-FB",FileSetOutput::ValidateArgv,0);

/* note: this sorts (add a nosort if necessary) */
void FileSetOutput::print(FileSet &fs, OutputJob *o) const
{
   fs.Sort(sort, sort_casefold);
   if(sort_dirs_first) fs.Sort(FileSet::DIRSFIRST, false);

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
	 mode_string(f->mode, mode);
	 /* FIXME: f->mode doesn't have type info; it wouldn't
	  * be hard to fix that */
	 if(f->filetype == FileInfo::DIRECTORY) mode[0] = 'd';
	 else if(f->filetype == FileInfo::SYMLINK) mode[0] = 'l';
	 else mode[0] = '-';
	 strcat(mode, " ");

	 c.add(mode, "");
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
	 char sz[128];
	 if((f->filetype == FileInfo::NORMAL || !size_filesonly)
	 && (f->defined&f->SIZE)) {
	    char buffer[128];
	    sprintf(sz, "%8s ",
	       human_readable_inexact (f->size, buffer, 1,
		  output_block_size? output_block_size:1024, human_ceiling));
	 } else {
	    sprintf(sz, "%8s ", ""); /* pad */
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
	 const int six_months_ago = SMTask::now - 31556952 / 2;
	 bool recent = six_months_ago <= f->date;

	 /* We assume all time outputs are equal-width. */
	 static const char *long_time_format[] = {
	    dcgettext (NULL, "%b %e  %Y", LC_TIME),
	    dcgettext (NULL, "%b %e %H:%M", LC_TIME)
	 };

	 const char *fmt = long_time_format[recent];
	 struct tm *when_local;
	 char *dt;
	 if ((f->defined&f->DATE)
	 && (when_local = localtime (&f->date))) {
	    dt = xstrftime(fmt, when_local);
	 } else {
	    /* put an empty field; make sure it's the same width */
	    dt = xstrftime(long_time_format[0], NULL);
	    int wid = mbswidth(dt, MBSW_ACCEPT_INVALID|MBSW_ACCEPT_UNPRINTABLE);
	    xfree(dt);

	    dt = (char *) xmalloc(wid+1);
	    memset(dt, ' ', wid);
	    dt[wid] = 0;
	 }
	 c.addf("%s ", "", dt);
	 xfree(dt);
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

FileSetOutput::FileSetOutput(const FileSetOutput &cp)
{
   memset(this, 0, sizeof(*this));
   *this = cp;
}

const FileSetOutput &FileSetOutput::operator = (const FileSetOutput &cp)
{
   if(this == &cp) return *this;

   memcpy(this, &cp, sizeof(*this));
   pat = xstrdup(cp.pat);
   return *this;
}

void FileSetOutput::config(const OutputJob *o)
{
   width = o->GetWidth();
   if(width == -1)
      width = 80;

   if(!strcasecmp(ResMgr::Query("color:use-color", 0), "auto")) color = o->IsTTY();
   else color = ResMgr::QueryBool("color:use-color", 0);
}

void FileSetOutput::long_list()
{
   single_column = true;
   mode = ALL;
   /* -l's default size is 1; otherwise 1024 */
   if(!output_block_size)
      output_block_size = 1;
}

const char *FileSetOutput::ValidateArgv(char **s)
{
   if(!*s) return NULL;

   ArgV arg("", *s);
   FileSetOutput tmp;

   const char *ret = tmp.parse_argv(&arg);
   if(ret) return ret;

   /* shouldn't be any non-option arguments */
   if(arg.count() > 1) return _("non-option arguments found");

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

   return need;
}

#undef super
#define super SessionJob

clsJob::clsJob(FA *s, ArgV *a, const FileSetOutput &_opts, OutputJob *_output):
   SessionJob(s),
   fso(_opts),
   args(a),
   num(1)
{
   use_cache=true;
   done=0;
   dir=0;
   mask=0;
   state=INIT;
   list_info=0;

   if(args->count() == 1)
      args->Add("");

   output=_output;
   output->SetParentFg(this);
}

clsJob::~clsJob()
{
   delete args;
   xfree(dir);
   Delete(list_info);
   Delete(output);
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
      Delete(list_info);
      list_info=0;

      /* next: */
      xfree(dir); dir = 0;
      xfree(mask); mask = 0;

      dir = args->getnext();
      if(!dir) {
	 /* done */
	 state=DONE;
	 return MOVED;
      }
      dir = xstrdup(dir);

      /* If the basename contains wildcards, set up the mask. */
      mask = strrchr(dir, '/');
      if(!mask) mask=dir;
      if(Glob::HasWildcards(mask)) {
	 if(mask == dir)
	    dir = xstrdup("");
	 else {
	    /* The mask is the whole argument, not just the basename; this is
	     * because the whole relative paths will end up in the FileSet, and
	     * that's what this pattern will be matched against. */
	    char *newmask = xstrdup(dir);

	    // leave the final / on the path, to prevent the dirname of
	    // "file/*" from being treated as a file
	    mask[1] = 0;
	    mask = newmask;
	 }
      } else mask=0;

      list_info=new GetFileInfo(session, dir, fso.list_directories);
      list_info->UseCache(use_cache);
      list_info->Need(fso.Need());

      state=GETTING_LIST_INFO;
      m=MOVED;
   }
   case GETTING_LIST_INFO:
   {
      if(!list_info->Done())
	 return m;

      if(list_info->Error()) {
	 eprintf("%s\n", list_info->ErrorText());
	 state=START_LISTING;
	 return MOVED;
      }

      /* one just finished */
      fso.pat = mask;
      FileSet *res = list_info->GetResult();

      if(res)
	 fso.print(*res, output);

      fso.pat = 0;
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

void clsJob::Suspend()
{
   if(list_info)
      list_info->Suspend();
   session->Suspend();
   super::Suspend();
}

void clsJob::Resume()
{
   if(list_info)
      list_info->Resume();
   session->Resume();
   super::Resume();
}

void clsJob::ShowRunStatus(StatusLine *s)
{
   if(fso.quiet)
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

void clsJob::PrintStatus(int v)
{
   Job::PrintStatus(v);

   if(list_info)
   {
      const char *curr = args->getcurr();
      if(!*curr)
	 curr = ".";
      const char *stat = list_info->Status();
      if(*stat)
	 printf("\t`%s' %s\n", curr, stat);
   }
}
