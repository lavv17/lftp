/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef LFTP_RL_H
#define LFTP_RL_H

CDECL_BEGIN

void lftp_readline_init (void);
int  lftp_history_expand (const char *, char **);
char *lftp_readline (const char *prompt);
void lftp_add_history_nodups(const char *cmd);
void lftp_history_clear();
void lftp_history_list(int cnt);
int lftp_history_write(const char *fn);
int lftp_history_read(const char *fn);
void lftp_rl_clear(void);
void lftp_rl_redisplay_maybe(void);
void lftp_rl_set_ignore_some_completions_function(int (*func)(char**));
char **lftp_rl_completion_matches(const char *text,char *(*compentry)(const char *,int));
void lftp_rl_add_defun(const char *name,int (*func)(int,int),int key);
void lftp_rl_bind(const char *key,const char *func);
void lftp_rl_set_prompt(const char *p);
void lftp_rl_write_history();
void lftp_rl_read_history();
void lftp_rl_history_stifle(int s);

void lftp_rl_init(
   const char *readline_name,
   char **(*attempted_completion_function)(const char *,int,int),
   int (*getc_function)(FILE*),
   const char *completer_quote_characters,
   const char *completer_word_break_characters,
   const char *filename_quote_characters,
   char *(*filename_quoting_function)(char *,int,char *),
   char *(*filename_dequoting_function)(const char *,int),
   int (*char_is_quoted_p)(const char *,int));

CDECL_END

#endif /* LFTP_RL_H */
