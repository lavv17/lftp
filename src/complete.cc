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
#include <time.h>
#include "xalloca.h"
#include "xmalloc.h"
#include "ftpclass.h"
#include "lftp.h"
#include "rglob.h"
#include "CmdExec.h"
#include "alias.h"
// #include "GetPass.h"
#include "SignalHook.h"
#include "CharReader.h"
#include "LsCache.h"

extern "C" {
#include "readline/readline.h"
}

static char *bash_dequote_filename (char *text, int quote_char);
static int lftp_char_is_quoted(char *string,int eindex);

static int len;    // lenght of the word to complete
static int cindex; // index in completion array
static char **glob_res=NULL;

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
   while ((name=CmdExec::cmd_table[cindex].name)!=0)
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

char *remote_generator(char *text,int state)
{
   char *name;

   /* If this is a new word to complete, initialize now.  This includes
      saving the length of TEXT for efficiency, and initializing the cindex
      variable to 0. */
   if(!state)
      cindex=0;

   if(glob_res==NULL)
      return NULL;

   while((name=glob_res[cindex++])!=NULL)
   {
      if(!strchr(name,'/') && memchr(text,'/',len))
      {
	 // workaround for servers returning only names without dir
	 char *slash=text+len;
	 while(slash[-1]!='/')
	    slash--;
	 if(strncmp(name,slash,len-(slash-text)))
	    continue;

	 char *combined=(char*)xmalloc(strlen(name)+len+1);
	 strncpy(combined,text,slash-text);
	 strcpy(combined+(slash-text),name);
	 return combined;
      }
      if(strncmp(name,text,len)==0)
	 return(xstrdup(name));
   }

   glob_res=NULL;
   return NULL;
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


// returns:
//    2 - remote dir
//    1 - remote file
//    0 - local
int   remote_cmd(int start)
{
   // try to guess whether the completion word is remote

   char buf[20];  // no commands longer
   const char *cmd=rl_line_buffer;
   TouchedAlias *used_aliases=0;

   for(;;)
   {
      const char *w=find_word(cmd);
      if(w[0]=='!' || w[0]=='#')
	 return 0;
      if(w[0]=='(')
      {
	 cmd=w+1;
	 continue;
      }
      if(!copy_word(buf,w,sizeof(buf))
      || buf[0]==0)
      {
	 TouchedAlias::FreeChain(used_aliases);
	 return 0;
      }
      const char *alias=Alias::Find(buf);
      if(alias && !TouchedAlias::IsTouched(alias,used_aliases))
      {
	 used_aliases=new TouchedAlias(alias,used_aliases);
	 cmd=alias;
	 continue;
      }
      const CmdExec::cmd_rec *c;
      int part=CmdExec::find_cmd(buf,&c);
      if(part==1)
	 strcpy(buf,c->name);
      TouchedAlias::FreeChain(used_aliases);
      break;
   }

   if(!strcmp(buf,"cd")
   || !strcmp(buf,"mkdir"))
      return 2;	/* append slash automatically */

   if(!strcmp(buf,"ls")
   || !strcmp(buf,"mget")
   || !strcmp(buf,"rm")
   || !strcmp(buf,"mrm")
   || !strcmp(buf,"rmdir")
   || !strcmp(buf,"more")
   || !strcmp(buf,"cat")
   || !strcmp(buf,"zcat")
   || !strcmp(buf,"zmore"))
      return 1;

   bool was_o=false;
   for(int i=start; i>4; i--)
   {
      if(!isspace(rl_line_buffer[i-1]))
	 break;
      if(!strncmp(rl_line_buffer+i-3,"-o",2) && isspace(rl_line_buffer[i-4]))
      {
	 was_o=true;
	 break;
      }
   }

   if(!strcmp(buf,"get")
   || !strcmp(buf,"pget"))
      if(!was_o)
	 return 1;
   if(!strcmp(buf,"put"))
      if(was_o)
	 return 1;

   return 0;
}

CmdExec *completion_shell;

static bool force_remote=false;

/* Attempt to complete on the contents of TEXT.  START and END show the
   region of TEXT that contains the word to complete.  We can use the
   entire line in case we want to do some simple parsing.  Return the
   array of matches, or NULL if there aren't any. */
char **lftp_completion (char *text,int start,int end)
{
   rl_completion_append_character=' ';

   if(start==0)
   {
      len=end;
      return completion_matches(text,command_generator);
   }
   int remote_type=remote_cmd(start);
   if((remote_completion && remote_type) || force_remote)
   {
      len=end-start;
      char *pat=(char*)alloca(len+5);
      strncpy(pat,text,len);
      pat[len]=0;
      if(strchr(pat,'*') || strchr(pat,'?'))
	 return(NULL);

      // get directory from the pattern (strip file name)
      char *sl=pat+strlen(pat);
      while(sl>pat && sl[-1]!='/')
	 sl--;
      while(sl>pat && sl[-1]=='/')
	 sl--;
      if(sl>pat)  // 'dir///file'
	 sl[0]=0;
      else if(pat[0]=='/')
	 pat[1]=0;	// '///'
      else
	 pat[0]=0;	// no slashes

//       strcat(pat,"/*")

      FileAccess::open_mode mode=Ftp::LIST;

      // try to find in cache
      if(!LsCache::Find(completion_shell->session,pat,mode,0,0)
      &&  LsCache::Find(completion_shell->session,pat,Ftp::LONG_LIST,0,0))
      {
	 // this is a bad hack, but often requested by people
	 mode=Ftp::LONG_LIST;
      }

      completion_shell->session->DontSleep();

      SignalHook::ResetCount(SIGINT);
      RemoteGlob g(completion_shell->session,pat,mode);
      g.SetSlashFilter(1);
      for(;;)
      {
	 SMTask::Schedule();
	 if(g.Done())
	    break;
	 if(SignalHook::GetCount(SIGINT))
	 {
	    rl_attempted_completion_over = 1;
	    return 0;
	 }
	 SMTask::Block();
      }
      glob_res=g.GetResult();

      if(mode==Ftp::LONG_LIST)
      {
	 // bad hack
	 // try to extract file names
	 for(int i=0; glob_res[i]; i++)
	 {
	    char *s=glob_res[i];
	    char *space=strrchr(s,' ');
	    if(space && s[0]=='l' && space>s+5
	    && !strncmp(space-3," ->",3))
	    {
	       space[-3]=0;
	       space=strrchr(space-3,' ');
	    }
	    if(space)
	       memmove(s,space+1,strlen(space));
	    int len=strlen(s);
	    if(len>1 && (s[len-1]=='@' || s[len-1]=='*' || s[len-1]=='/'))
	    {
	       if(s[len-1]=='/')
		  rl_completion_append_character='/';
	       s[len-1]=0;
	    }
	 }
      }

      rl_filename_completion_desired=1;

      char quoted=((lftp_char_is_quoted(rl_line_buffer,start) &&
		    strchr(rl_completer_quote_characters,rl_line_buffer[start-1]))
		   ? rl_line_buffer[start-1] : 0);
      text=bash_dequote_filename(text, quoted);
      len=strlen(text);
      char **matches=completion_matches(text,remote_generator);
      free(text);
      if(!matches)
      {
	 rl_attempted_completion_over = 1;
	 return 0;
      }
      if(remote_type==2 /* cd/mkdir */)
	 rl_completion_append_character='/';
      return matches;
   }
   return(NULL);
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
char *
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
char *
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
	case '"':
// 	case '$':
// 	case '`':
	case '\\':
	  *r++ = '\\';
	default:
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
char *
backslash_quote (char *string)
{
  int c;
  char *result, *r, *s;

  result = (char*)xmalloc (2 * strlen (string) + 1);

  for (r = result, s = string; s && (c = *s); s++)
    {
      switch (c)
	{
	case ' ': case '\t': case '\n':		/* IFS white space */
// 	case '\'':
	case '"': case '\\':		/* quoting chars */
	case '|': case '&': case ';':		/* shell metacharacters */
// 	case '(': case ')':
	case '<': case '>':
// 	case '!': case '{': case '}':		/* reserved words */
// 	case '*': case '[': case '?': case ']':	/* globbing chars */
// 	case '^':
// 	case '$': case '`':			/* expansion chars */
	  *r++ = '\\';
	  *r++ = c;
	  break;
	case '#':				/* comment char */
#if 0
	case '~':				/* tilde expansion */
#endif
	  if (s == string)
	    *r++ = '\\';
	  /* FALLTHROUGH */
	default:
	  *r++ = c;
	  break;
	}
    }

  *r = '\0';
  return (result);
}

int
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

int skip_double_quoted(char *s, int i)
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
  int i, pass_next, quoted;

  for (i = pass_next = quoted = 0; i <= eindex; i++)
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

extern "C" {
int   lftp_complete_remote(int count,int key)
{
   extern Function *rl_last_func;

   if(rl_last_func == (Function*)lftp_complete_remote)
      rl_last_func = rl_complete;

   force_remote = true;
   int ret=rl_complete(count,key);
   force_remote = false;
   return ret;
}
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
void initialize_readline ()
{
   /* Allow conditional parsing of the ~/.inputrc file. */
   rl_readline_name = "lftp";

   /* Tell the completer that we want a crack first. */
   rl_attempted_completion_function = (CPPFunction *)lftp_completion;

   rl_getc_function = (int (*)(...))lftp_rl_getc;

   rl_completer_quote_characters = "\"";
   rl_completer_word_break_characters = " \t\n\"";
   rl_filename_quote_characters = " \t\n\\\">;|&()";
   rl_filename_quoting_function = (char* (*)(...))bash_quote_filename;
   rl_filename_dequoting_function = (char* (*)(...))bash_dequote_filename;
   rl_char_is_quoted_p = (int (*)(...))lftp_char_is_quoted;

   rl_add_defun("complete-remote",(Function*)lftp_complete_remote,-1);
   static char line[]="Meta-Tab: complete-remote";
   rl_parse_and_bind(line);
}
