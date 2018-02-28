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

#include "trio.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <assert.h>
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
#include "ResMgr.h"
#include "ColumnOutput.h"
#include "FileSetOutput.h"
#include "OutputJob.h"
#include "misc.h"

CDECL_BEGIN
#include <glob.h>

#define USE_VARARGS 1
#define PREFER_STDARG 1
#include <readline/readline.h>
CDECL_END

#ifndef GLOB_PERIOD
# define GLOB_PERIOD 0
#endif

static char *bash_dequote_filename (const char *text, int quote_char);
static int lftp_char_is_quoted(const char *string,int eindex);

static int len;    // lenght of the word to complete
static int cindex; // index in completion array
static const char *const *array;
static char **vars=NULL;
static FileSet *glob_res=NULL;
static bool inhibit_tilde;

static bool shell_cmd;
static bool quote_glob;
static bool quote_glob_basename;

char *command_generator(const char *text,int state)
{
   const char *name;
   static const Alias *alias;

   /* If this is a new word to complete, initialize now. */
   if(!state)
   {
      cindex=0;
      alias=Alias::base;
   }

   /* Return the next name which partially matches from the command list. */
   while ((name=CmdExec::CmdByIndex(cindex))!=0)
   {
      cindex++;
      if(name[0]=='.' && len==0)
	 continue;   // skip hidden commands
      if(strncasecmp(name,text,len)==0)
	 return(xstrdup(name));
   }

   while(alias)
   {
      const Alias *tmp=alias;
      alias=alias->next;
      if(strncasecmp(tmp->alias,text,len)==0)
         return(xstrdup(tmp->alias));
   }

   /* If no names matched, then return NULL. */
   return(NULL);
}

static char *file_generator(const char *text,int state)
{
   /* If this is a new word to complete, initialize now. */
   if(!state)
      cindex=0;

   if(glob_res==NULL)
      return NULL;

   while((*glob_res)[cindex])
   {
      const char *name=(*glob_res)[cindex++]->name;

      if(!name[0])
	 continue;
      return(xstrdup(name));
   }

   return NULL;
}

static bool bookmark_prepend_bm;
static char *bookmark_generator(const char *text,int s)
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
	 if(bookmark_prepend_bm)
	 {
	    xstring& e=xstring::get_tmp("bm:");
	    t=e.append_url_encoded(t,URL_HOST_UNSAFE);
	 }
	 if(strncmp(t,text,len)==0)
	    return xstrdup(t);
	 break;
      case 1:
	 return 0;
      }
   }
}

static char *array_generator(const char *text,int state)
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

static char *vars_generator(const char *text,int state)
{
   const char *name;

   /* If this is a new word to complete, initialize now. */
   if(!state)
      cindex=0;

   if(vars==NULL)
      return NULL;

   while((name=vars[cindex++])!=NULL)
   {
      if(!name[0])
	 continue;
      char *text0=string_alloca(len+2);
      strncpy(text0,text,len);
      text0[len]=0;
      if(ResMgr::VarNameCmp(name,text0)!=ResMgr::DIFFERENT)
	 return(xstrdup(name));
      if(strchr(text0,':')==0)
      {
	 strcat(text0,":");
	 if(ResMgr::VarNameCmp(name,text0)!=ResMgr::DIFFERENT)
	    return(xstrdup(name));
      }
   }

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

int ignore_non_dirs(char **matches)
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
   return 0;
}

static const char *find_word(const char *p)
{
   while(CmdExec::is_space(*p))
      p++;
   return p;
}
static bool copy_word_skip(const char **p_in,char *buf,int n)
{
   const char *&p=*p_in;
   char in_quotes=0;
   for(;;)
   {
      if(!*p)
	 break;
      if(in_quotes)
      {
	 if(*p==in_quotes)
	 {
	    in_quotes=0,p++;
	    continue;
	 }
	 else if(*p=='\\' && CmdExec::quotable(p[1],in_quotes))
	    p++;
	 if(buf && n>0)
	    *buf++=*p,n--;
	 p++;
	 continue;
      }
      if(CmdExec::is_space(*p))
	 break;
      if(*p=='\\' && CmdExec::quotable(p[1],in_quotes))
	 p++;
      else if(CmdExec::is_quote(*p))
      {
	 in_quotes=*p++;
	 continue;
      }
      if(buf && n>0)
	 *buf++=*p,n--;
      p++;
   }
   if(n>0 && buf)
      *buf=0;
   return n>0;
}
// returns false when buffer overflows
static bool copy_word(char *buf,const char *p,int n)
{
   return copy_word_skip(&p,buf,n);
}
static const char *skip_word(const char *p)
{
   copy_word_skip(&p,0,0);
   return p;
}

enum completion_type
{
   LOCAL, LOCAL_DIR, REMOTE_FILE, REMOTE_DIR, BOOKMARK, COMMAND,
   STRING_ARRAY, VARIABLE, NO_COMPLETION
};

// cmd: ptr to command line being completed
// start: location of the word being completed
static completion_type cmd_completion_type(const char *cmd,int start)
{
   const char *w=0;
   char buf[20];  // no commands longer
   TouchedAlias *used_aliases=0;

   // try to guess whether the completion word is remote
   for(;;)
   {
      w=find_word(cmd);
      if(w-cmd == start) // first word is command
	 return COMMAND;
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
	 start-=(w+1-cmd);
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
	 int buf_len=strlen(buf);
	 const char *cmd1=xstring::cat(alias,w+buf_len,NULL);
	 cmd=alloca_strdup(cmd1);
	 start=start-buf_len+strlen(alias);
	 continue;
      }
      const char *full=CmdExec::GetFullCommandName(buf);
      if(full!=buf)
	 strcpy(buf,full);
      TouchedAlias::FreeChain(used_aliases);
      break;
   }

   for(const char *p=cmd+start; p>cmd; )
   {
      p--;
      if((*p=='>' || *p=='|')
      && !lftp_char_is_quoted(cmd,p-cmd))
	 return LOCAL;
      if(!CmdExec::is_space(*p))
	 break;
   }

   if(!strcmp(buf,"shell"))
      shell_cmd=quote_glob=true;
   if(!strcmp(buf,"glob")
   || !strcmp(buf,"mget")
   || !strcmp(buf,"mput")
   || !strcmp(buf,"mrm"))
      quote_glob=true;

   if(!strcmp(buf,"cls"))
      quote_glob_basename=true;

   if(!strcmp(buf,"cd")
   || !strcmp(buf,"mkdir"))
      return REMOTE_DIR; /* append slash automatically */

   if(!strcmp(buf,"cat")
   || !strcmp(buf,"ls")
   || !strcmp(buf,"cls")
   || !strcmp(buf,"du")
   || !strcmp(buf,"edit")
   || !strcmp(buf,"find")
   || !strcmp(buf,"recls")
   || !strcmp(buf,"rels")
   || !strcmp(buf,"more")
   || !strcmp(buf,"mrm")
   || !strcmp(buf,"mv")
   || !strcmp(buf,"mmv")
   || !strcmp(buf,"nlist")
   || !strcmp(buf,"rm")
   || !strcmp(buf,"rmdir")
   || !strcmp(buf,"bzcat")
   || !strcmp(buf,"bzmore")
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
   bool have_R=false;
   bool have_N=false;
   bool second=false;
   int second_start=-1;
   for(int i=start; i>4; i--)
   {
      if(!CmdExec::is_space(rl_line_buffer[i-1]))
	 break;
      if(!strncmp(rl_line_buffer+i-3,"-o",2) && CmdExec::is_space(rl_line_buffer[i-4]))
      {
	 was_o=true;
	 break;
      }
      if(!strncmp(rl_line_buffer+i-3,"-N",2) && CmdExec::is_space(rl_line_buffer[i-4]))
      {
	 was_N=true;
	 break;
      }
      if(i-14 >= 0 && !strncmp(rl_line_buffer+i-13, "--newer-than",12) && CmdExec::is_space(rl_line_buffer[i-14]))
      {
	 was_N=true;
	 break;
      }
      if(!strncmp(rl_line_buffer+i-3,"-O",2) && CmdExec::is_space(rl_line_buffer[i-4]))
      {
	 was_O=true;
	 break;
      }
   }
   w=skip_word(find_word(cmd));
   if(*w)
   {
      w=find_word(w);
      second_start=w-cmd;
      if(w-cmd==start)	// we complete second word
	 second=true;
   }

   if(re_match(cmd," -[a-zA-Z]*R[a-zA-Z]* ",0))
      have_R=true;

   int arg=0;
   bool opt_stop=false;
   for(w=cmd; *w; )
   {
      w=find_word(w);
      if(w-cmd==start || w-cmd+CmdExec::is_quote(*w)==start)
	 break;
      if(w[0]!='-' || opt_stop)
	 arg++;
      else if(re_match(w,"^-[a-zA-Z]*N ",0))
	 have_N=true;
      if(!strncmp(w,"-- ",3))
	 opt_stop=true;
      w=skip_word(w);
   }

   if (!strcmp(buf, "edit"))
	   return REMOTE_FILE;

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
      if(was_N)
	 return LOCAL;
      if(have_N)
	 arg--;
      if(have_R ^ (arg!=1))
	 return LOCAL_DIR;
      if(arg>2)
	 return NO_COMPLETION;
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

   if(!strcmp(buf,"set"))
   {
      if(second)
      {
         if(!vars)
            vars=ResMgr::Generator();
         return VARIABLE;
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
static char **lftp_completion (const char *text,int start,int end)
{
   FileSetOutput fso;

   completion_shell->RestoreCWD();

   if(start>end)  // workaround for a bug in readline
      start=end;

   GlobURL *rg=0;

   rl_completion_append_character=' ';
   rl_ignore_some_completions_function=0;
   shell_cmd=false;
   quote_glob=false;
   quote_glob_basename=false;
   inhibit_tilde=false;
   delete glob_res;
   glob_res=0;

   completion_type type=cmd_completion_type(rl_line_buffer,start);

   len=end-start;

   char *(*generator)(const char *text,int state) = 0;

   switch(type)
   {
   case NO_COMPLETION:
      rl_attempted_completion_over = 1;
      return 0;
   case COMMAND:
      generator = command_generator;
      break;
   case BOOKMARK:
      bookmark_prepend_bm=false;
      generator = bookmark_generator;
      break;
   case STRING_ARRAY:
      generator = array_generator;
      break;
   case VARIABLE:
      generator = vars_generator;
      break;

      char *pat;
   case LOCAL:
   case LOCAL_DIR: {
      if(force_remote || (url::is_url(text) && remote_completion))
      {
	 if(type==LOCAL_DIR)
	    type=REMOTE_DIR;
	 goto really_remote;
      }
   really_local:
      fso.parse_res(ResMgr::Query("cmd:cls-completion-default", 0));

      bool tilde_expanded=false;
      const char *home=getenv("HOME");
      int home_len=xstrlen(home);
      pat=string_alloca((len+home_len)*2+10);
      if(len>0 && home_len>0 && text[0]=='~' && (len==1 || text[1]=='/'))
      {
	 glob_quote(pat,home,home_len);
	 glob_quote(pat+strlen(pat),text+1,len-1);
	 if(len==1)
	    strcat(pat,"/");
	 tilde_expanded=true;
	 inhibit_tilde=false;
      }
      else
      {
	 glob_quote(pat,text,len);
	 inhibit_tilde=true;
      }

      /* if we want case-insensitive matching, we need to match everything
       * in the dir and weed it ourselves (let the generator do it), since
       * glob() has no casefold option */
      if(fso.patterns_casefold) {
	 rl_variable_bind("completion-ignore-case", "1");

	 /* strip back to the last / */
	 char *sl = strrchr(pat, '/');
	 if(sl) *++sl = 0;
	 else pat[0] = 0;
      } else {
	 rl_variable_bind("completion-ignore-case", "0");
      }

      strcat(pat,"*");

      glob_t pglob;
      glob(pat,GLOB_PERIOD,NULL,&pglob);
      glob_res=new FileSet;
      for(int i=0; i<(int)pglob.gl_pathc; i++)
      {
	 char *src=pglob.gl_pathv[i];
	 if(tilde_expanded && home_len>0)
	 {
	    src+=home_len-1;
	    *src='~';
	 }
	 if(!strcmp(basename_ptr(src), ".")) continue;
	 if(!strcmp(basename_ptr(src), "..")) continue;
	 if(type==LOCAL_DIR && not_dir(src)) continue;

	 FileInfo *f = new FileInfo;

	 f->LocalFile(src, false);
	 glob_res->Add(f);
      }
      globfree(&pglob);

      rl_filename_completion_desired=1;
      generator = file_generator;
      break;
   }
   case REMOTE_FILE:
   case REMOTE_DIR: {
      if(!remote_completion && !force_remote)
      {
	 if(type==REMOTE_DIR)
	    type=LOCAL_DIR;
	 goto really_local;
      }
   really_remote:
      if(!strncmp(text,"bm:",3) && !strchr(text,'/'))
      {
	 bookmark_prepend_bm=true;
	 generator=bookmark_generator;
	 rl_completion_append_character='/';
	 break;
      }

      pat=string_alloca(len*2+10);
      glob_quote(pat,text,len);

      if(pat[0]=='~' && pat[1]==0)
	 strcat(pat,"/");

      if(pat[0]=='~' && pat[1]=='/')
	 inhibit_tilde=false;
      else
	 inhibit_tilde=true;
      strcat(pat,"*");

      completion_shell->session->DontSleep();

      SignalHook::ResetCount(SIGINT);
      glob_res=NULL;
      rg=new GlobURL(completion_shell->session,pat,
		     type==REMOTE_DIR?GlobURL::DIRS_ONLY:GlobURL::ALL);

      rl_save_prompt();

      fso.parse_res(ResMgr::Query("cmd:cls-completion-default", 0));

      if(rg)
      {
	 rg->NoInhibitTilde();
	 if(fso.patterns_casefold) {
	    rl_variable_bind("completion-ignore-case", "1");
	    rg->CaseFold();
	 } else
	    rl_variable_bind("completion-ignore-case", "0");

	 Timer status_timer;
	 status_timer.SetMilliSeconds(20);

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

	       rl_restore_prompt();
	       rl_clear_message();

	       return 0;
	    }

	    if(!fso.quiet)
	    {
	       /* don't set blank status; if we're operating from cache,
		* that's all we'll get and it'll look ugly: */
	       const char *ret = rg->Status();
	       if(*ret)
	       {
		  if(status_timer.Stopped())
		  {
		     rl_message ("%s> ", ret);
		     status_timer.SetResource("cmd:status-interval",0);
		  }
	       }
	    }

	    SMTask::Block();
	 }
	 glob_res=new FileSet(rg->GetResult());
	 glob_res->rewind();
      }
      rl_restore_prompt();
      rl_clear_message();

      if(glob_res->get_fnum()==1)
      {
	 FileInfo *info=glob_res->curr();
	 rl_completion_append_character=' ';
	 if(info->defined&info->TYPE && info->filetype==info->DIRECTORY)
	    rl_completion_append_character='/';
      }
      rl_filename_completion_desired=1;
      generator = file_generator;
      break;
   }
   } /* end switch */

   assert(generator);

   char quoted=((lftp_char_is_quoted(rl_line_buffer,start) &&
		 strchr(rl_completer_quote_characters,rl_line_buffer[start-1]))
		? rl_line_buffer[start-1] : 0);
   xstring_ca textq(bash_dequote_filename(text, quoted));
   len=strlen(textq);

   char **matches=lftp_rl_completion_matches(textq,generator);

   if(rg)
      delete rg;

   if(vars)
   {
      // delete vars?
   }

   if(!matches)
   {
      rl_attempted_completion_over = 1;
      return 0;
   }

   if(type==REMOTE_DIR)
      rl_completion_append_character='/';

   return matches;
}

extern "C" void lftp_line_complete()
{
   delete glob_res;
   glob_res=0;
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
  int c;
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
  int c;
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
  char *bn = basename_ptr(string);

  result = (char*)xmalloc (2 * strlen (string) + 1);

  for (r = result, s = string; s && (c = *s); s++)
    {
      switch (c)
	{
	case '(': case ')':
	case '{': case '}':			/* reserved words */
	case '^':
	case '$': case '`':			/* expansion chars */
	  if(!shell_cmd)
	    goto def;
	case '*': case '[': case '?': case ']':	/* globbing chars */
	  if(!shell_cmd && !quote_glob && (!quote_glob_basename || s<bn))
	    goto def;
	  /*fallthrough*/
	case ' ': case '\t': case '\n':		/* IFS white space */
	case '"': case '\'': case '\\':		/* quoting chars */
	case '|': case '&': case ';':		/* shell metacharacters */
	case '<': case '>': case '!':
	  *r++ = '\\';
	  *r++ = c;
	  break;
	case '~':				/* tilde expansion */
	  if (s == string && inhibit_tilde)
	    *r++ = '.', *r++ = '/';
	  goto def;
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
bash_dequote_filename (const char *text, int quote_char)
{
  char *ret;
  const char *p;
  char *r;
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

static int skip_quoted(const char *s, int i, char q)
{
   while(s[i] && s[i]!=q)
   {
      if(s[i]=='\\' && s[i+1])
	 i++;
      i++;
   }
   if(s[i])
      i++;
   return i;
}

int lftp_char_is_quoted(const char *string,int eindex)
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
      else if (string[i] == '"' || string[i] == '\'')
        {
	  char quote = string[i];
          i = skip_quoted (string, ++i, quote);
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

extern "C" int (*rl_last_func)(int,int);
static int lftp_complete_remote(int count,int key)
{
   if(rl_last_func == lftp_complete_remote)
      rl_last_func = rl_complete;

   force_remote = true;
   int ret=rl_complete(count,key);
   force_remote = false;
   return ret;
}

int lftp_rl_getc(FILE *file)
{
   SignalHook::DoCount(SIGINT);
   SMTaskRef<CharReader> rr(new CharReader(fileno(file)));
   CharReader& r=*rr.get_non_const();
   for(;;)
   {
      SMTask::Schedule();
      int res=r.GetChar();
      if(res==r.EOFCHAR)
	 return EOF;
      if(res!=r.NOCHAR)
	 return res;
      lftp_rl_redisplay_maybe();
      SMTask::Block();
      if(SignalHook::GetCount(SIGINT)>0)
      {
	 if(rl_line_buffer && rl_end>0)
	    rl_kill_full_line(0,0);
	 return '\n';
      }
   }
}

extern int lftp_slot(int,int);

/* Tell the GNU Readline library how to complete.  We want to try to complete
   on command names if this is the first word in the line, or on filenames
   if not. */
void lftp_readline_init ()
{
   lftp_rl_init(
      "lftp",		      // rl_readline_name
      lftp_completion,	      // rl_attempted_completion_function
      lftp_rl_getc,	      // rl_getc_function
      "\"'",		      // rl_completer_quote_characters
      " \t\n\"'",	      // rl_completer_word_break_characters
      " \t\n\\\"'>;|&()*?[]~!",// rl_filename_quote_characters
      bash_quote_filename,    // rl_filename_quoting_function
      bash_dequote_filename,  // rl_filename_dequoting_function
      lftp_char_is_quoted);   // rl_char_is_quoted_p

   lftp_rl_add_defun("complete-remote",lftp_complete_remote,-1);
   lftp_rl_bind("Meta-Tab","complete-remote");

   lftp_rl_add_defun("slot-change",lftp_slot,-1);
   char key[7];
   strcpy(key,"Meta-N");
   for(int i=0; i<10; i++)
   {
      key[5]='0'+i;
      lftp_rl_bind(key,"slot-change");
   }
}

extern "C" void completion_display_list (char **matches, int len)
{
   JobRef<OutputJob> b(new OutputJob((FDStream *) NULL, "completion"));

   if(glob_res) {
      /* Our last completion action was of files, and we kept that
       * data around.  Take the files in glob_res which are in matches
       * and put them in another FileSet.  (This is a little wasteful,
       * since we're going to use it briefly and discard it, but it's
       * not worth adding temporary-filtering options to FileSet.) */
      FileSet tmp;
      for(int i = 1; i <= len; i++) {
	 FileInfo *fi = glob_res->FindByName(matches[i]);
	 assert(fi);
	 tmp.Add(new FileInfo(*fi));
      }

      FileSetOutput fso;
      fso.config(b);

      fso.parse_res(ResMgr::Query("cmd:cls-completion-default", 0));

      fso.print(tmp, b);
   } else {
      /* Just pass it through ColumnInfo. */
      ColumnOutput c;
      for(int i = 1; i <= len; i++) {
	 c.append();
	 c.add(matches[i], "");
      }
      c.print(b, b->GetWidth(), b->IsTTY());
   }

   b->PutEOF();

   while(!b->Done())
   {
      SMTask::Schedule();
      if(SignalHook::GetCount(SIGINT))
      {
	 SignalHook::ResetCount(SIGINT);
	 break;
      }
   }
}
