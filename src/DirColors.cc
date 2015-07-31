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
#include "DirColors.h"
#include "ResMgr.h"
#include "FileSet.h"
#include "buffer.h"

DirColors *DirColors::instance;
const char DirColors::resource[]="color:dir-colors";

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

void DirColors::Parse(const char *p)
{
   Empty();
   Add(".lc", "\033[");
   Add(".rc", "m");
   Add(".no", "");
   Add(".fi", "");
   Add(".di", "");
   Add(".ln", "");

   if(!p)
      return;

   char *buf;			/* color_buf buffer pointer */
   int state;			/* State of parser */
   char label[4];		/* Indicator label */
   const char *ext;

   label[0] = '.';
   label[3] = 0;

   ext = NULL;

   buf = alloca_strdup (p);

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
		  break;
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
	    const char *b = buf;
	    state = get_funky_string (&buf, &p, 0) < 0 ? -1 : 1;
	    Add(label, b);
	 }
	 break;

      case 4:		/* Equal sign after *.ext */
	 if (*(p++) == '=')
	 {
	    const char *b = buf;
	    state = get_funky_string (&buf, &p, 0) < 0 ? -1 : 1;
	    Add(ext, b);
	 }
	 else
	    state = -1;
	 break;
      }
   }
   // if (color_indicator[C_LINK].len == 6
   //     && !strncmp (color_indicator[C_LINK].string, "target", 6))
   //   color_symlink_as_referent = 1;

   if(!Lookup(".ec"))
   {
      const char *no=Lookup(".no");
      const char *lc=Lookup(".lc");
      const char *rc=Lookup(".rc");
      Add(".ec",xstring::cat(lc,no,rc,NULL));
   }
}

DirColors::DirColors()
{
   Reconfig(resource);
}

const char *DirColors::GetColor(const char *name,int type)
{
   const char *ret=0;
   if(type==FileInfo::DIRECTORY)
   {
      ret=Lookup(".di");
      if(ret)
	 return ret;
   }
   else if(type==FileInfo::SYMLINK)
   {
      ret=Lookup(".ln");
      if(ret)
	 return ret;
   }
   else if(type==FileInfo::NORMAL)
      ret=Lookup(".fi");

   const char *ext = strrchr(name, '.');
   if(ext && *++ext)
   {
      const char *l=Lookup(ext);
      if(l)
	 return l;
   }

   return ret?ret:"";
}

const char *DirColors::GetColor(const FileInfo *fi)
{
   return GetColor(fi->name,fi->defined&fi->TYPE?fi->filetype:-1);
}

void DirColors::PutColored(const Ref<Buffer>& buf,const char *name,int type)
{
   const char *color=GetColor(name,type);
   const char *lc=Lookup(".lc");
   const char *rc=Lookup(".rc");
   if(!color || !*color || !lc || !rc)
   {
      buf->Put(name);
      return;
   }
   buf->Put(lc);
   buf->Put(color);
   buf->Put(rc);
   buf->Put(name);
   PutReset(buf);
}
void DirColors::PutReset(const Ref<Buffer>& buf)
{
   const char *reset=Lookup(".ec");
   buf->Put(reset);
}

void DirColors::Reconfig(const char *name)
{
   if(!xstrcmp(name,resource))
      Parse(ResMgr::Query(resource,0));
}
