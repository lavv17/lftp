/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <readline/readline.h>
#include <readline/history.h>
#include "lftp_rl.h"
#include "xalloca.h"

void lftp_add_history_nodups(const char *cmd_buf)
{
   HIST_ENTRY *temp;
   using_history();
   temp=previous_history();
   if(temp==0 || strcmp(temp->line,cmd_buf))
      add_history(cmd_buf);
   using_history();
}

char *lftp_readline(const char *prompt)
{
   return readline(prompt);
}

int lftp_history_expand(const char *what, char **where)
{
   return history_expand((char*)what,where);
}

int lftp_history_read(const char *fn)
{
   using_history();
   return read_history(fn);
}

int lftp_history_write(const char *fn)
{
   using_history();
   return write_history(fn);
}

void lftp_history_list(int cnt)
{
   HISTORY_STATE *st = history_get_history_state();
   HIST_ENTRY *hist;
   int i;
   using_history();

   i = history_base + st->length - cnt;
   if(cnt == -1 || i < history_base) i = history_base;

   while((hist = history_get(i)))
      printf("%5i  %s\n", i++, hist->line);
}

void lftp_history_clear()
{
   clear_history();
}

static int is_clear=0;

void lftp_rl_clear()
{
   extern char *rl_display_prompt;
   extern int _rl_mark_modified_lines;
   int old_end=rl_end;
   char *old_prompt=rl_display_prompt;
   int old_mark=_rl_mark_modified_lines;

   rl_end=0;
   rl_display_prompt="";
   rl_expand_prompt(0);
   _rl_mark_modified_lines=0;

   rl_redisplay();

   rl_end=old_end;
   rl_display_prompt=old_prompt;
   _rl_mark_modified_lines=old_mark;
   if(rl_display_prompt==rl_prompt)
      rl_expand_prompt(rl_prompt);

   is_clear=1;
}

void lftp_rl_redisplay_maybe()
{
   if(is_clear)
      rl_redisplay();
   is_clear=0;
}

/* prototype hell differences in various readline versions make it impossible
 * to use certain functions/variables in C++ */

void lftp_rl_set_ignore_some_completions_function(int (*func)(char**))
{
   rl_ignore_some_completions_function=func;
}

char **lftp_rl_completion_matches(const char *text,char *(*compentry)(const char *,int))
{
   return rl_completion_matches(text,compentry);
}

void lftp_rl_init(
   const char *readline_name,
   char **(*attempted_completion_function)(const char *,int,int),
   int (*getc_function)(FILE*),
   const char *completer_quote_characters,
   const char *completer_word_break_characters,
   const char *filename_quote_characters,
   char *(*filename_quoting_function)(char *,int,char *),
   char *(*filename_dequoting_function)(char *,int),
   int (*char_is_quoted_p)(char *,int))
{
   rl_readline_name                  =readline_name;
   rl_attempted_completion_function  =attempted_completion_function;
   rl_getc_function                  =getc_function;
   rl_completer_quote_characters     =completer_quote_characters;
   rl_completer_word_break_characters=completer_word_break_characters;
   rl_filename_quote_characters      =filename_quote_characters;
   rl_filename_quoting_function      =filename_quoting_function;
   rl_filename_dequoting_function    =filename_dequoting_function;
   rl_char_is_quoted_p               =char_is_quoted_p;
}

void lftp_rl_add_defun(const char *name,int (*func)(int,int),int key)
{
   rl_add_defun(name,func,key);
}
void lftp_rl_bind(const char *key,const char *func)
{
   char *line=alloca(strlen(key)+2+strlen(func)+1);
   sprintf(line,"%s: %s",key,func);
   rl_parse_and_bind(line);
}
