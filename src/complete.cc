/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id$ */

#include <config.h>

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include "xalloca.h"
#include "xmalloc.h"
#include "FileAccess.h"
#include "CmdExec.h"
#include "alias.h"
#include "SignalHook.h"
#include "CharReader.h"
#include "LsCache.h"
#include "complete.h"
#include "lftp_rl.h"
#include "url.h"

CDECL_BEGIN
#include "readline/readline.h"
CDECL_END

static char *bash_dequote_filename (char *text, int quote_char);
static int lftp_char_is_quoted(char *string,int eindex);

static int len;    // lenght of the word to complete
static int cindex; // index in completion array
static const char *const *array;
static FileSet *glob_res=NULL;

static bool shell_cmd;
static bool quote_glob;

char *command_generator(char *text,int state)
{
   const char *name;
   static const Alias *alias;

   /* If this is a new word to complete, initialize now.  This includes
      saving the length of TEXT for efficiency, and initializing the cindex
      variable to 0. */
   if(!state)
   {
      cindex=0;
      alias=Alias::base;
   }

   /* Return the next name which partially matches from the command list. */
   while ((name=CmdExec::CmdByIndex(cindex)->name)!=0)
   {
      cindex++;
      if(strncmp(name,text,len)==0)
	 return(xstrdup(name));
   }

   while(alias)
   {
      const Alias *tmp=alias;
      alias=alias->next;
      if(strncmp(tmp->alias,text,len)==0)
         return(xstrdup(tmp->alias));
   }

   /* If no names matched, then return NULL. */
   return(NULL);
}

static char *remote_generator(char *text,int state)
{
   const char *name;

   /* If this is a new word to complete, initialize now.  This includes
      saving the length of TEXT for efficiency, and initializing the cindex
      variable to 0. */
   if(!state)
      cindex=0;

   if(glob_res==NULL)
      return NULL;

   while((*glob_res)[cindex])
   {
      name=(*glob_res)[cindex++]->name;
      if(!name[0])
	 continue;
      if(strncmp(name,text,len)==0)
	 return(xstrdup(name));
   }

   glob_res=NULL;
   return NULL;
}

static char *bookmark_generator(char *text,int s)
{
   static int state;
   const char *t;
   if(!s)
   {
      state=0;
      lftp_bookmarks.Rewind();
   }
   for(;;)
   {
      switch(state)
      {
      case 0:
	 t=lftp_bookmarks.CurrentKey();
	 if(!t)
	 {
	    state=1;
	    break;
	 }
	 if(!lftp_bookmarks.Next())
	    state=1;
	 if(strncmp(t,text,len)==0)
	    return xstrdup(t);
	 break;
      case 1:
	 return 0;
      }
   }
}

static char *array_generator(char *text,int state)
{
   const char *name;

   /* If this is a new word to complete, initialize now. */
   if(!state)
      cindex=0;

   if(array==NULL)
      return NULL;

   while((name=array[cindex++])!=NULL)
   {
      if(!name[0])
	 continue;
      if(strncmp(name,text,len)==0)
	 return(xstrdup(name));
   }

   array=NULL;
   return NULL;
}

static bool not_dir(char *f)
{
   struct stat st;
   f=tilde_expand(f);
   bool res=(stat(f,&st)==-1 || !S_ISDIR(st.st_mode));
   free(f);
   return res;
}

void ignore_non_dirs(char **matches)
{
   // filter out non-dirs.
   int out=1;
   for(int i=1; matches[i]; i++)
   {
      if(!not_dir(matches[i]))
	 matches[out++]=matches[i];
      else
	 free(matches[i]);
   }
   matches[out]=0;
   if(out==1)
   {
      // we have only the LCD prefix. Handle it carefully.
      char *f=matches[0];
      int len=strlen(f);
      if((len>2 && f[len-1]=='/') // all files, no dirs.
      || not_dir(f))		 // or single non dir.
      {
	 // all files, no dirs.
	 free(f);
	 matches[0]=0;
      }
   }
}

static const char *find_word(const char *p)
{
   while(*p && isspace(*p))
      p++;
   return p;
}
// returns false when buffer overflows
static bool copy_word(char *buf,const char *p,int n)
{
   while(n>0 && *p && !isspace(*p))
   {
      *buf++=*p++;
      n--;
   }
   if(n>0)
      *buf=0;
   return n>0;
}


enum completion_type
{
   LOCAL, LOCAL_DIR, REMOTE_FILE, REMOTE_DIR, BOOKMARK, COMMAND,
   STRING_ARRAY, NO_COMPLETION
};

static completion_type cmd_completion_type(const char *cmd,int start)
{
   const char *w=find_word(cmd);

   if(w-cmd == start) // first word is command
      return COMMAND;

   // try to guess whether the completion word is remote

   char buf[20];  // no commands longer
   TouchedAlias *used_aliases=0;

   for(;;)
   {
      w=find_word(cmd);
      if(w[0]=='!')
	 shell_cmd=true;
      if(w[0]=='#')
	 return NO_COMPLETION;
      if(w[0]=='!')
      {
	 shell_cmd=quote_glob=true;
	 return LOCAL;
      }
      if(w[0]=='?')  // help
	 return COMMAND;
      if(w[0]=='(')
      {
	 cmd=w+1;
	 continue;
      }
      if(!copy_word(buf,w,sizeof(buf))
      || buf[0]==0)
      {
	 TouchedAlias::FreeChain(used_aliases);
	 return LOCAL;
      }
      const char *alias=Alias::Find(buf);
      if(alias && !TouchedAlias::IsTouched(alias,used_aliases))
      {
	 used_aliases=new TouchedAlias(alias,used_aliases);
	 cmd=alias;
	 continue;
      }
      const char *full=CmdExec::GetFullCommandName(buf);
      if(full!=buf)
	 strcpy(buf,full);
      TouchedAlias::FreeChain(used_aliases);
      break;
   }

   for(char *p=rl_line_buffer+start; p>rl_line_buffer; )
   {
      p--;
      if((*p=='>' || *p=='|')
      && !lftp_char_is_quoted(rl_line_buffer,p-rl_line_buffer))
	 return LOCAL;
      if(!isspace((unsigned char)*p))
	 break;
   }

   if(!strcmp(buf,"shell"))
      shell_cmd=quote_glob=true;
   if(!strcmp(buf,"glob")
   || !strcmp(buf,"mget")
   || !strcmp(buf,"mput")
   || !strcmp(buf,"mrm"))
      quote_glob=true;

   if(!strcmp(buf,"cd")
   || !strcmp(buf,"mkdir"))
      return REMOTE_DIR; /* append slash automatically */

   if(!strcmp(buf,"cat")
   || !strcmp(buf,"ls")
   || !strcmp(buf,"more")
   || !strcmp(buf,"mrm")
   || !strcmp(buf,"mv")
   || !strcmp(buf,"nlist")
   || !strcmp(buf,"rm")
   || !strcmp(buf,"rmdir")
   || !strcmp(buf,"zcat")
   || !strcmp(buf,"zmore"))
      return REMOTE_FILE;

   if(!strcmp(buf,"open")
   || !strcmp(buf,"lftp"))
      return BOOKMARK;

   if(!strcmp(buf,"help"))
      return COMMAND;

   bool was_o=false;
   bool was_N=false;
   bool was_O=false;
   bool second=false;
   int second_start=-1;
   for(int i=start; i>4; i--)
   {
      if(!isspace(rl_line_buffer[i-1]))
	 break;
      if(!strncmp(rl_line_buffer+i-3,"-o",2) && isspace(rl_line_buffer[i-4]))
      {
	 was_o=true;
	 break;
      }
      if(!strncmp(rl_line_buffer+i-3,"-N",2) && isspace(rl_line_buffer[i-4]))
      {
	 was_N=true;
	 break;
      }
      if(!strncmp(rl_line_buffer+i-3,"-O",2) && isspace(rl_line_buffer[i-4]))
      {
	 was_O=true;
	 break;
      }
   }
   w=find_word(cmd);
   while(*w && !isspace(*w))
      w++;  // FIXME: handle quotations.
   if(*w)
   {
      w=find_word(w);
      second_start=w-cmd;
      if(w-cmd==start)	// we complete second word
	 second=true;
   }

   if(!strcmp(buf,"get")
   || !strcmp(buf,"pget")
   || !strcmp(buf,"get1"))
   {
      if(was_O)
	 return LOCAL_DIR;
      if(!was_o)
	 return REMOTE_FILE;
   }
   if(!strcmp(buf,"mget"))
      if(!was_O)
	 return REMOTE_FILE;
   if(!strcmp(buf,"put"))
      if(was_o)
	 return REMOTE_FILE;
   if(!strcmp(buf,"put")
   || !strcmp(buf,"mput"))
      if(was_O)
	 return REMOTE_DIR;
   if(!strcmp(buf,"mirror"))
   {
      // FIXME: guess -R and take arg number into account.
      if(!was_N)
	 return REMOTE_DIR;
   }
   if(!strcmp(buf,"bookmark"))
   {
      if(second)
      {
	 array=bookmark_subcmd;
	 return STRING_ARRAY;
      }
      else
	 return BOOKMARK;
   }
   if(!strcmp(buf,"chmod"))
   {
      if(second)
	 return NO_COMPLETION;
      else
	 return REMOTE_FILE;
   }
   if(!strcmp(buf,"glob")
   || !strcmp(buf,"command")
   || !strcmp(buf,"queue"))
   {
      if(second)
	 return COMMAND;
      else
      {
	 // FIXME: infinite alias expansion is possible.
	 if(second_start>0 && start>second_start && (int)strlen(cmd)>second_start)
	    return cmd_completion_type(cmd+second_start,start-second_start);
	 return REMOTE_FILE;
      }
   }
   if(!strcmp(buf,"cache"))
   {
      if(second)
      {
	 array=cache_subcmd;
	 return STRING_ARRAY;
      }
      else
	 return NO_COMPLETION;
   }

   if(!strcmp(buf,"lcd"))
      return LOCAL_DIR;

   return LOCAL;
}

static void glob_quote(char *out,const char *in,int len)
{
   while(len>0)
   {
      switch(*in)
      {
      case '*': case '?': case '[': case ']':
	 if(!quote_glob)
	    *out++='\\';
	 break;
      case '\\':
	 switch(in[1])
	 {
	 case '*': case '?': case '[': case ']': case '\\':
	    *out++=*in++;  // copy the backslash.
	    break;
	 default:
	    in++; // skip it.
	    break;
	 }
	 break;
      }
      *out++=*in;
      in++;
      len--;
   }
   *out=0;
}

CmdExec *completion_shell;
int remote_completion=0;

static bool force_remote=false;

/* Attempt to complete on the contents of TEXT.  START and END show the
   region of TEXT that contains the word to complete.  We can use the
   entire line in case we want to do some simple parsing.  Return the
   array of matches, or NULL if there aren't any. */
static char **lftp_completion (char *text,int start,int end)
{
   completion_shell->RestoreCWD();

   if(start>end)  // workaround for a bug in readline
      start=end;

   GlobURL *rg=0;

   rl_completion_append_character=' ';
   rl_ignore_some_completions_function=0;
   shell_cmd=false;
   quote_glob=false;

   completion_type type=cmd_completion_type(rl_line_buffer,start);

   len=end-start;

   char *(*generator)(char *text,int state) = 0;

   switch(type)
   {
   case NO_COMPLETION:
      rl_attempted_completion_over = 1;
      return 0;
   case COMMAND:
      generator = command_generator;
      break;
   case BOOKMARK:
      generator = bookmark_generator;
      break;
   case STRING_ARRAY:
      generator = array_generator;
      break;
   case LOCAL:
   case LOCAL_DIR:
      if(force_remote || (url::is_url(text) && remote_completion))
      {
	 if(type==LOCAL_DIR)
	    type=REMOTE_DIR;
	 goto really_remote;
      }
      if(type==LOCAL_DIR)
	 rl_ignore_some_completions_function = (Function*)ignore_non_dirs;
      break;
   case REMOTE_FILE:
   case REMOTE_DIR:
      if(!remote_completion && !force_remote)
	 break; // local
   really_remote:
      char *pat=(char*)alloca(len*2+10);
      glob_quote(pat,text,len);

      strcat(pat,"*");

      completion_shell->session->DontSleep();

      SignalHook::ResetCount(SIGINT);
      glob_res=NULL;
      rg=new GlobURL(completion_shell->session,pat);
      if(rg)
      {
	 rg->glob->NoMatchPeriod();
	 if(type==REMOTE_DIR)
	    rg->glob->DirectoriesOnly();
	 for(;;)
	 {
	    SMTask::Schedule();
	    if(rg->Done())
	       break;
	    if(SignalHook::GetCount(SIGINT))
	    {
	       SignalHook::ResetCount(SIGINT);
	       rl_attempted_completion_over = 1;
	       delete rg;
	       return 0;
	    }
	    SMTask::Block();
	 }
	 glob_res=rg->GetResult();
	 glob_res->rewind();
      }
      if(glob_res->get_fnum()==1)
      {
	 FileInfo *info=glob_res->curr();
	 rl_completion_append_character=' ';
	 if(info->defined&info->TYPE && info->filetype==info->DIRECTORY)
	    rl_completion_append_character='/';
      }
      rl_filename_completion_desired=1;
      generator = remote_generator;
      break;

   } /* end switch */

   if(generator==0)
      return 0; // default to local

   char quoted=((lftp_char_is_quoted(rl_line_buffer,start) &&
		 strchr(rl_completer_quote_characters,rl_line_buffer[start-1]))
		? rl_line_buffer[start-1] : 0);
   text=bash_dequote_filename(text, quoted);
   len=strlen(text);

   char **matches=completion_matches(text,(CPFunction*)generator);

   if(rg)
      delete rg;

   if(!matches)
   {
      rl_attempted_completion_over = 1;
      goto leave;
   }

   if(type==REMOTE_DIR)
      rl_completion_append_character='/';

leave:
   xfree(text);
   return matches;
}


enum { COMPLETE_DQUOTE,COMPLETE_SQUOTE,COMPLETE_BSQUOTE };
#define completion_quoting_style COMPLETE_BSQUOTE

/* **************************************************************** */
/*								    */
/*	 Functions for quoting strings to be re-read as input	    */
/*								    */
/* **************************************************************** */

/* Return a new string which is the single-quoted version of STRING.
   Used by alias and trap, among others. */
static char *
single_quote (char *string)
{
  register int c;
  char *result, *r, *s;

  result = (char *)xmalloc (3 + (4 * strlen (string)));
  r = result;
  *r++ = '\'';

  for (s = string; s && (c = *s); s++)
    {
      *r++ = c;

      if (c == '\'')
	{
	  *r++ = '\\';	/* insert escaped single quote */
	  *r++ = '\'';
	  *r++ = '\'';	/* start new quoted string */
	}
    }

  *r++ = '\'';
  *r = '\0';

  return (result);
}

/* Quote STRING using double quotes.  Return a new string. */
static char *
double_quote (char *string)
{
  register int c;
  char *result, *r, *s;

  result = (char *)xmalloc (3 + (2 * strlen (string)));
  r = result;
  *r++ = '"';

  for (s = string; s && (c = *s); s++)
    {
      switch (c)
        {
	case '$':
	case '`':
	  if(!shell_cmd)
	     goto def;
	case '"':
	case '\\':
	  *r++ = '\\';
	default: def:
	  *r++ = c;
	  break;
        }
    }

  *r++ = '"';
  *r = '\0';

  return (result);
}

/* Quote special characters in STRING using backslashes.  Return a new
   string. */
static char *
backslash_quote (char *string)
{
  int c;
  char *result, *r, *s;

  result = (char*)xmalloc (2 * strlen (string) + 1);

  for (r = result, s = string; s && (c = *s); s++)
    {
      switch (c)
	{
 	case '\'':
 	case '(': case ')':
 	case '!': case '{': case '}':		/* reserved words */
 	case '^':
 	case '$': case '`':			/* expansion chars */
	  if(!shell_cmd)
	    goto def;
 	case '*': case '[': case '?': case ']':	/* globbing chars */
	  if(!shell_cmd && !quote_glob)
	    goto def;
	case ' ': case '\t': case '\n':		/* IFS white space */
	case '"': case '\\':		/* quoting chars */
	case '|': case '&': case ';':		/* shell metacharacters */
	case '<': case '>':
	  *r++ = '\\';
	  *r++ = c;
	  break;
	case '~':				/* tilde expansion */
	case '#':				/* comment char */
	  if(!shell_cmd)
	    goto def;
	  if (s == string)
	    *r++ = '\\';
	  /* FALLTHROUGH */
	default: def:
	  *r++ = c;
	  break;
	}
    }

  *r = '\0';
  return (result);
}

#if 0 // no need yet
static int
contains_shell_metas (char *string)
{
  char *s;

  for (s = string; s && *s; s++)
    {
      switch (*s)
	{
	case ' ': case '\t': case '\n':		/* IFS white space */
	case '\'': case '"': case '\\':		/* quoting chars */
	case '|': case '&': case ';':		/* shell metacharacters */
	case '(': case ')': case '<': case '>':
	case '!': case '{': case '}':		/* reserved words */
	case '*': case '[': case '?': case ']':	/* globbing chars */
	case '^':
	case '$': case '`':			/* expansion chars */
	  return (1);
	case '#':
	  if (s == string)			/* comment char */
	    return (1);
	  /* FALLTHROUGH */
	default:
	  break;
	}
    }

  return (0);
}
#endif //0

/* Filename quoting for completion. */
/* A function to strip quotes that are not protected by backquotes.  It
   allows single quotes to appear within double quotes, and vice versa.
   It should be smarter. */
static char *
bash_dequote_filename (char *text, int quote_char)
{
  char *ret, *p, *r;
  int l, quoted;

  l = strlen (text);
  ret = (char*)xmalloc (l + 1);
  for (quoted = quote_char, p = text, r = ret; p && *p; p++)
    {
      /* Allow backslash-quoted characters to pass through unscathed. */
      if (*p == '\\')
	{
	  *r++ = *++p;
	  if (*p == '\0')
	    break;
	  continue;
	}
      /* Close quote. */
      if (quoted && *p == quoted)
        {
          quoted = 0;
          continue;
        }
      /* Open quote. */
      if (quoted == 0 && (*p == '\'' || *p == '"'))
        {
          quoted = *p;
          continue;
        }
      *r++ = *p;
    }
  *r = '\0';
  return ret;
}

/* Quote characters that the readline completion code would treat as
   word break characters with backslashes.  Pass backslash-quoted
   characters through without examination. */
static char *
quote_word_break_chars (char *text)
{
  char *ret, *r, *s;
  int l;

  l = strlen (text);
  ret = (char*)xmalloc ((2 * l) + 1);
  for (s = text, r = ret; *s; s++)
    {
      /* Pass backslash-quoted characters through, including the backslash. */
      if (*s == '\\')
	{
	  *r++ = '\\';
	  *r++ = *++s;
	  if (*s == '\0')
	    break;
	  continue;
	}
      /* OK, we have an unquoted character.  Check its presence in
	 rl_completer_word_break_characters. */
      if (strchr (rl_completer_word_break_characters, *s))
        *r++ = '\\';
      *r++ = *s;
    }
  *r = '\0';
  return ret;
}

/* Quote a filename using double quotes, single quotes, or backslashes
   depending on the value of completion_quoting_style.  If we're
   completing using backslashes, we need to quote some additional
   characters (those that readline treats as word breaks), so we call
   quote_word_break_chars on the result. */
static char *
bash_quote_filename (char *s, int rtype, char *qcp)
{
  char *rtext, *mtext, *ret;
  int rlen, cs;

  rtext = (char *)NULL;

  /* If RTYPE == MULT_MATCH, it means that there is
     more than one match.  In this case, we do not add
     the closing quote or attempt to perform tilde
     expansion.  If RTYPE == SINGLE_MATCH, we try
     to perform tilde expansion, because single and double
     quotes inhibit tilde expansion by the shell. */

  mtext = s;
#if 0
  if (mtext[0] == '~' && rtype == SINGLE_MATCH)
    mtext = bash_tilde_expand (s);
#endif

  cs = completion_quoting_style;
  /* Might need to modify the default completion style based on *qcp,
     since it's set to any user-provided opening quote. */
  if (*qcp == '"')
    cs = COMPLETE_DQUOTE;
  else if (*qcp == '\'')
    cs = COMPLETE_SQUOTE;
#if defined (BANG_HISTORY)
  else if (*qcp == '\0' && history_expansion && cs == COMPLETE_DQUOTE &&
	   history_expansion_inhibited == 0 && strchr (mtext, '!'))
    cs = COMPLETE_BSQUOTE;

  if (*qcp == '"' && history_expansion && cs == COMPLETE_DQUOTE &&
        history_expansion_inhibited == 0 && strchr (mtext, '!'))
    {
      cs = COMPLETE_BSQUOTE;
      *qcp = '\0';
    }
#endif

  switch (cs)
    {
    case COMPLETE_DQUOTE:
      rtext = double_quote (mtext);
      break;
    case COMPLETE_SQUOTE:
      rtext = single_quote (mtext);
      break;
    case COMPLETE_BSQUOTE:
      rtext = backslash_quote (mtext);
      break;
    }

  if (mtext != s)
    free (mtext);

  /* We may need to quote additional characters: those that readline treats
     as word breaks that are not quoted by backslash_quote. */
  if (rtext && cs == COMPLETE_BSQUOTE)
    {
      mtext = quote_word_break_chars (rtext);
      free (rtext);
      rtext = mtext;
    }

  /* Leave the opening quote intact.  The readline completion code takes
     care of avoiding doubled opening quotes. */
  rlen = strlen (rtext);
  ret = (char*)xmalloc (rlen + 1);
  strcpy (ret, rtext);

  /* If there are multiple matches, cut off the closing quote. */
  if (rtype == MULT_MATCH && cs != COMPLETE_BSQUOTE)
    ret[rlen - 1] = '\0';
  free (rtext);
  return ret;
}

static int skip_double_quoted(char *s, int i)
{
   while(s[i] && s[i]!='"')
   {
      if(s[i]=='\\' && s[i+1])
	 i++;
      i++;
   }
   if(s[i])
      i++;
   return i;
}

int lftp_char_is_quoted(char *string,int eindex)
{
  int i, pass_next;

  for (i = pass_next = 0; i <= eindex; i++)
    {
      if (pass_next)
        {
          pass_next = 0;
          if (i >= eindex)
            return 1;
          continue;
        }
      else if (string[i] == '"')
        {
          i = skip_double_quoted (string, ++i);
          if (i > eindex)
            return 1;
          i--;  /* the skip functions increment past the closing quote. */
        }
      else if (string[i] == '\\')
        {
          pass_next = 1;
          continue;
        }
    }
  return (0);
}

int   lftp_complete_remote(int count,int key)
{
   extern Function *rl_last_func;

   if(rl_last_func == (Function*)lftp_complete_remote)
      rl_last_func = (Function*)rl_complete;

   force_remote = true;
   int ret=rl_complete(count,key);
   force_remote = false;
   return ret;
}

int   lftp_rl_getc(FILE *file)
{
   int res;
   CharReader r(fileno(file));

   for(;;)
   {
      SMTask::Schedule();
      res=r.GetChar();
      if(res==r.EOFCHAR)
	 return EOF;
      if(res!=r.NOCHAR)
	 return res;
      lftp_rl_redisplay_maybe();
      SMTask::Block();
      if(SignalHook::GetCount(SIGINT)>0)
      {
	 rl_kill_text(0,rl_end);
	 rl_line_buffer[0]=0;
	 return '\n';
      }
   }
}

/* Tell the GNU Readline library how to complete.  We want to try to complete
   on command names if this is the first word in the line, or on filenames
   if not. */
void lftp_readline_init ()
{
   /* Allow conditional parsing of the ~/.inputrc file. */
   rl_readline_name = (char*)"lftp";

   /* Tell the completer that we want a crack first. */
   rl_attempted_completion_function = (CPPFunction *)lftp_completion;

   rl_getc_function = (Function*)lftp_rl_getc;

   rl_completer_quote_characters = (char*)"\"";
   rl_completer_word_break_characters = (char*)" \t\n\"";
   rl_filename_quote_characters = (char*)" \t\n\\\">;|&()*?[]";
   rl_filename_quoting_function = (CPFunction*)bash_quote_filename;
   rl_filename_dequoting_function = (CPFunction*)bash_dequote_filename;
   rl_char_is_quoted_p = (Function*)lftp_char_is_quoted;

   rl_add_defun((char*)"complete-remote",(Function*)lftp_complete_remote,-1);
   static char line[]="Meta-Tab: complete-remote";
   rl_parse_and_bind(line); /* this function writes to the string */
}
