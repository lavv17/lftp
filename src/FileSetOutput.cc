/* lftp and utils
 *
 * Copyright (c) 2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <assert.h>
#include <locale.h>
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
#include "Glob.h"


ResDecl	res_default_cls         ("cmd:cls-default",  "-F", FileSetOutput::ValidateArgv,0),
	res_default_comp_cls    ("cmd:cls-completion-default", "-FB",FileSetOutput::ValidateArgv,0);

/* note: this sorts (add a nosort if necessary) */
void FileSetOutput::print(FileSet &fs, Buffer *o) const
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

void FileSetOutput::config(FDStream *fd)
{
   width = fd_width(fd->getfd());
   assert(width != -1);
   if(!strcasecmp(ResMgr::Query("color:use-color", 0), "auto")) color = isatty(fd->getfd());
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

/* Peer interface: */

#define super FileCopyPeer
FileCopyPeerCLS::FileCopyPeerCLS(FA *_session, ArgV *a, const FileSetOutput &_fso):
   super(GET),
   session(_session),
   fso(_fso), f(0),
   args(a),
   quiet(false),
   num(1), dir(0), mask(0)
{
   if(args->count() == 1)
      args->Add("");
   list_info=0;
   can_seek=false;
   can_seek0=false;
}

FileCopyPeerCLS::~FileCopyPeerCLS()
{
   delete f;
   delete args;
   Delete(list_info);
   SessionPool::Reuse(session);
   xfree(dir);
}

int FileCopyPeerCLS::Do()
{
   if(Done()) return STALL;

   /* one currently processing? */
   if(list_info) {
      if(list_info->Error()) {
	 SetError(list_info->ErrorText());
	 return MOVED;
      }

      if(!list_info->Done())
	 return STALL;

      /* one just finished */
      int oldpos = pos;
      fso.pat = mask;
      FileSet *res = list_info->GetResult();
      Delete(list_info);
      list_info=0;
      if(res)
	 fso.print(*res, this);
      fso.pat = 0;
      delete res;
      pos = oldpos;
   }

   /* next: */
   xfree(dir); dir = 0;
   xfree(mask); mask = 0;

   dir = args->getnext();
   if(!dir) {
      /* done */
      PutEOF();
      return MOVED;
   }
   dir = xstrdup(dir);

   /* If the basename contains wildcards, move the basename into mask. */
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
   if(!list_info) {
      PutEOF();
      return MOVED;
   }
   list_info->UseCache(use_cache);

   return MOVED;
}

const char *FileCopyPeerCLS::GetStatus()
{
   return session->CurrentStatus();
}

void FileCopyPeerCLS::Suspend()
{
   if(session)
      session->Suspend();
   super::Suspend();
}
void FileCopyPeerCLS::Resume()
{
   super::Resume();
   if(session)
      session->Resume();
}
