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

#include <mbswidth.h>

#include "misc.h"
#include "ResMgr.h"

/* Our autoconf test will switch to lib/fnmatch.c if the local fnmatch
 * isn't GNU, so this should be OK. */
#define _GNU_SOURCE
#include <fnmatch.h>

#include "FileSetOutput.h"
#include "ArgV.h"
#include "ColumnOutput.h"


ResDecl res_dir_colors   ("cmd:dir-colors",    "",  0,0),
	res_default_cls         ("cmd:cls-default",  "-F", FileSetOutput::ValidateArgv,0),
	res_default_comp_cls    ("cmd:cls-completion-default", "-FB",FileSetOutput::ValidateArgv,0);

/* Parse a string as part of the LS_COLORS variable; this may involve
   decoding all kinds of escape characters.  If equals_end is set an
   unescaped equal sign ends the string, otherwise only a : or \0
   does.  Returns the number of characters output, or -1 on failure.

   The resulting string is *not* null-terminated, but may contain
   embedded nulls.

   Note that both dest and src are char **; on return they point to
   the first free byte after the array and the character that ended
   the input string, respectively.  */

static int
get_funky_string (char **dest, const char **src, int equals_end)
{
   int num;			/* For numerical codes */
   int count;			/* Something to count with */
   enum {
      ST_GND, ST_BACKSLASH, ST_OCTAL, ST_HEX, ST_CARET, ST_END, ST_ERROR
   } state;
   const char *p;
   char *q;

   p = *src;			/* We don't want to double-indirect */
   q = *dest;			/* the whole darn time.  */

   count = 0;			/* No characters counted in yet.  */
   num = 0;

   state = ST_GND;		/* Start in ground state.  */
   while (state < ST_END) {
      switch (state) {
      case ST_GND:		/* Ground state (no escapes) */
	 switch (*p) {
	 case ':':
	 case '\0':
	    state = ST_END;	/* End of string */
	    break;
	 case '\\':
	    state = ST_BACKSLASH; /* Backslash scape sequence */
	    ++p;
	    break;
	 case '^':
	    state = ST_CARET; /* Caret escape */
	    ++p;
	    break;
	 case '=':
	    if (equals_end)
	    {
	       state = ST_END; /* End */
	       break;
	    }
	    /* else fall through */
	 default:
	    *(q++) = *(p++);
	    ++count;
	    break;
	 }
	 break;

      case ST_BACKSLASH:	/* Backslash escaped character */
	 switch (*p) {
	 case '0':
	 case '1':
	 case '2':
	 case '3':
	 case '4':
	 case '5':
	 case '6':
	 case '7':
	    state = ST_OCTAL;	/* Octal sequence */
	    num = *p - '0';
	    break;
	 case 'x':
	 case 'X':
	    state = ST_HEX;	/* Hex sequence */
	    num = 0;
	    break;
	 case 'a':		/* Bell */
	    num = 7;		/* Not all C compilers know what \a means */
	    break;
	 case 'b':		/* Backspace */
	    num = '\b';
	    break;
	 case 'e':		/* Escape */
	    num = 27;
	    break;
	 case 'f':		/* Form feed */
	    num = '\f';
	    break;
	 case 'n':		/* Newline */
	    num = '\n';
	    break;
	 case 'r':		/* Carriage return */
	    num = '\r';
	    break;
	 case 't':		/* Tab */
	    num = '\t';
	    break;
	 case 'v':		/* Vtab */
	    num = '\v';
	    break;
	 case '?':		/* Delete */
	    num = 127;
	    break;
	 case '_':		/* Space */
	    num = ' ';
	    break;
	 case '\0':		/* End of string */
	    state = ST_ERROR;	/* Error! */
	    break;
	 default:		/* Escaped character like \ ^ : = */
	    num = *p;
	    break;
	 }
	 if (state == ST_BACKSLASH) {
	    *(q++) = num;
	    ++count;
	    state = ST_GND;
	 }
	 ++p;
	 break;

      case ST_OCTAL:		/* Octal sequence */
	 if (*p < '0' || *p > '7') {
	    *(q++) = num;
	    ++count;
	    state = ST_GND;
	 }
	 else
	    num = (num << 3) + (*(p++) - '0');
	 break;

      case ST_HEX:		/* Hex sequence */
	 switch (*p) {
	 case '0':
	 case '1':
	 case '2':
	 case '3':
	 case '4':
	 case '5':
	 case '6':
	 case '7':
	 case '8':
	 case '9':
	    num = (num << 4) + (*(p++) - '0');
	    break;
	 case 'a':
	 case 'b':
	 case 'c':
	 case 'd':
	 case 'e':
	 case 'f':
	    num = (num << 4) + (*(p++) - 'a') + 10;
	    break;
	 case 'A':
	 case 'B':
	 case 'C':
	 case 'D':
	 case 'E':
	 case 'F':
	    num = (num << 4) + (*(p++) - 'A') + 10;
	    break;
	 default:
	    *(q++) = num;
	    ++count;
	    state = ST_GND;
	    break;
	 }
	 break;

      case ST_CARET:		/* Caret escape */
	 state = ST_GND;	/* Should be the next state... */
	 if (*p >= '@' && *p <= '~') {
	    *(q++) = *(p++) & 037;
	    ++count;
	 } else if (*p == '?') {
	    *(q++) = 127;
	    ++count;
	 }
	 else
	    state = ST_ERROR;
	 break;

      default:
	 abort ();
      }
   }

   *(q++) = 0;

   *dest = q;
   *src = p;

   if(state == ST_ERROR) return -1;

   return count;
}

static void
parse_ls_color (const char *p, KeyValueDB &out)
{
   if(!p)
      return;

   char *buf;			/* color_buf buffer pointer */
   int state;			/* State of parser */
   char label[4];		/* Indicator label */
   const char *ext;
   char *color_buf;

   label[0] = '.';
   label[3] = 0;

   ext = NULL;

   /* This is an overly conservative estimate, but any possible
      LS_COLORS string will *not* generate a color_buf longer than
      itself, so it is a safe way of allocating a buffer in
      advance.  */
   buf = color_buf = xstrdup (p);

   state = 1;
   while (state > 0) {
      switch (state) {
      case 1:		/* First label character */
	 switch (*p) {
	    case ':':
	       ++p;
	       break;

	    case '*':
	       /* Allocate new extension block and add to head of
		  linked list (this way a later definition will
		  override an earlier one, which can be useful for
		  having terminal-specific defs override global).  */

	       ++p;
	       /* next should be . */
	       if(*p++ != '.') {
		  state = -1;
	       }

	       ext = buf;
	       state = get_funky_string (&buf, &p, 1) < 0 ? -1 : 4;
	       break;

	    case '\0':
	       state = 0;	/* Done! */
	       break;

	    default:	/* Assume it is file type label */
	       label[1] = *(p++);
	       state = 2;
	       break;
	 }
	 break;

      case 2:		/* Second label character */
	 if (*p)
	 {
	    label[2] = *(p++);
	    state = 3;
	 }
	 else
	    state = -1;	/* Error */
	 break;

      case 3:		/* Equal sign after indicator label */
	 state = -1;	/* Assume failure... */
	 if (*(p++) == '=')/* It *should* be... */
	 {
	    char *b = buf;
	    state = get_funky_string (&buf, &p, 0) < 0 ? -1 : 1;
	    out.Add(label, b);
	    if (state == -1)
	       fprintf(stderr, _("unrecognized prefix: \"%s\""), label);
	 }
	 break;

      case 4:		/* Equal sign after *.ext */
	 if (*(p++) == '=')
	 {
	    char *b = buf;
	    state = get_funky_string (&buf, &p, 0) < 0 ? -1 : 1;
	    out.Add(ext, b);
	 }
	 else
	    state = -1;
	 break;
      }
   }

   if (state < 0) {
      fprintf(stderr, _("unparsable color value")); // FIXME
      out.Empty();
   }

   xfree (color_buf);
   // if (color_indicator[C_LINK].len == 6
   //     && !strncmp (color_indicator[C_LINK].string, "target", 6))
   //   color_symlink_as_referent = 1;
}

/* note: this sorts (add a nosort if necessary) */
void FileSetOutput::print(FileSet &fs, Buffer *o) const
{
   fs.Sort(sort, sort_casefold);
   if(sort_dirs_first) fs.Sort(FileSet::DIRSFIRST, false);

   ColumnOutput c;
   const char *colorsp = res_dir_colors.Query(0);
   if(!colorsp || !*colorsp) colorsp = getenv("LS_COLORS");
   if(!colorsp || !*colorsp) colorsp = getenv("ZLS_COLORS"); /* zsh */

   char *colors = xstrdup(colorsp);

   KeyValueDB col;

   /* Defaults: */
   col.Add(".lc", "\033[");
   col.Add(".rc", "m");
   col.Add(".no", "");
   col.Add(".fi", "");
   col.Add(".di", "");
   col.Add(".ln", "");

   parse_ls_color(colors, col);
   xfree(colors);

   const char *suffix_color = "";

   for(int i = 0; fs[i]; i++) {
      const FileInfo *f = fs[i];
      if(!showdots && (!strcmp(basename_ptr(f->name),".") || !strcmp(basename_ptr(f->name),"..")))
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

      if((mode & SIZE) && (f->defined&FileInfo::SIZE)) {
	 char sz[128];
	 if(f->filetype == FileInfo::NORMAL || !size_filesonly) {
	    sprintf(sz, "%10lu ", (unsigned long) f->size);
	 }
	 else {
	    sprintf(sz, "%10s ", ""); /* pad */
	 }
	 c.add(sz, "");
      }

      /* We use unprec dates; doing MDTMs for each file in ls is far too
       * slow.  If someone actually wants that (to get dates on servers with
       * unparsable dates, or more accurate dates), it wouldn't be
       * difficult.  If we did this, we could also support --full-time. */
      if(mode & DATE) {
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
	 if ((f->defined&FileInfo::DATE_UNPREC) && (when_local = localtime (&f->date))) {
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
      c.add(nm, FileInfoColor(*f, col));

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

	 c.add(lfi->name, FileInfoColor(*lfi, col));
	 if(classify)
	    c.add(FileInfoSuffix(*lfi), suffix_color);
      }
   }

   c.print(o, width, color);
}

const char *FileSetOutput::FileInfoSuffix(const FileInfo &fi) const
{
   if(fi.filetype == FileInfo::DIRECTORY)
      return "/";
   else if(fi.filetype == FileInfo::SYMLINK)
      return "@";
   return "";
}

/* can't const 'col' due to non-const-safe implementation; no clean fix */
const char *FileSetOutput::FileInfoColor(const FileInfo &fi, KeyValueDB &col) const
{
   if(fi.filetype == FileInfo::DIRECTORY) {
      const char *ret = col.Lookup(".di");
      if(ret) return ret;
   }
   if(fi.filetype == FileInfo::SYMLINK) {
      const char *ret = col.Lookup(".ln");
      if(ret) return ret;
   }

   const char *ret = col.Lookup(".fi");
   if(!ret) ret = "";

   const char *ext = strrchr(fi.name, '.');
   if(ext && *++ext) {
      const char *l = col.Lookup(ext);
      if(l) return l;
   }

   return ret;
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
   else color = ResMgr::Query("color:use-color", 0);
}

void FileSetOutput::long_list()
{
   mode = ALL;
   width = 0; /* one column */
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
   num(1), dir(0), mask(0),
   state(INIT)
{
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

   int res;
   switch(state) {
   case INIT:
   case GETTING_LIST:
      /* one currently processing and incomplete? */
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
	 fso.print(*res, this);
	 fso.pat = 0;
	 delete res;
	 pos = oldpos;
      }

      /* next: */
      xfree(dir); dir = 0;
      xfree(mask); mask = 0;
      if(args->count() == 1 && num == 1) {
	 dir = xstrdup("./");
	 num++;
      } else {
	 dir = args->getarg(num++);
	 if(!dir) {
	    /* done */
	    PutEOF();
	    return MOVED;
	 }
	 dir = xstrdup(dir);
      }

      /* Problem: is "bar" in "foo/bar" (the last portion) a directory or a
       * file?
       * Neither is intuitive--"cls /pub" is common; so is
       * "cls dir/filename".
       *
       * Let's assume it's a file and force people to use a trailing slash.
       * (I don't particularly like that, but it's done in other places
       * anyway ...) */

      mask = strrchr(dir, '/');
      if(mask) {
	 *mask++ = 0;
	 mask = xstrdup(mask);
      } else {
	 mask = dir;
	 dir = 0;
      }

      if(dir) session->Chdir(dir);
      state = CHANGING_DIR;
      return MOVED;

   case CHANGING_DIR:
      if(dir) {
	 res=session->Done();
	 if(res==FA::IN_PROGRESS)
	    return STALL;
	 if(res<0)
	 {
	    /* if(res==FA::FILE_MOVED)
	       {
	       }
	       Not going to copy+paste 25 lines of code from MirrorJob
	     */
	    printf("%s: %s\n", args->a0(), session->StrError(res));
	    state = GETTING_LIST;
	    return MOVED;
	 }
      }
      list_info=session->MakeListInfo();
      if(!list_info) {
	 PutEOF();
	 return MOVED;
      }
      list_info->UseCache(use_cache);
      state = GETTING_LIST;
      return MOVED;
   }

   return MOVED;
}

const char *FileCopyPeerCLS::GetStatus()
{
   return session->CurrentStatus();
}
