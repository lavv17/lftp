/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

void lftp_add_history_nodups(const char *cmd_buf)
{
   HIST_ENTRY *temp;
   using_history();
   temp=previous_history();
   if(temp==0 || strcmp(temp->line,cmd_buf))
      add_history(cmd_buf);
   using_history();
}

const char *lftp_readline(const char *prompt)
{
   return readline(prompt);
}

int lftp_history_expand(const char *what, char **where)
{
   return history_expand(what,where);
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
