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

#ifndef CMDEXEC_H
#define CMDEXEC_H

#include <stdarg.h>

#include "Job.h"
#include "ArgV.h"
#include "Filter.h"
#include "alias.h"
#include "history.h"

extern History cwd_history;

#define	CMD(name) Job *CmdExec::do_##name()
#define	in_CMD(name) Job *do_##name()

class CmdFeeder
{
public:
   char *saved_buf;
   CmdFeeder *prev;
   virtual char *NextCmd(class CmdExec *exec,const char *prompt) = 0;
};

extern CmdFeeder *lftp_feeder;	 // feeder to use after 'lftp' command

class CmdExec : public SessionJob
{
// current command data
   char *cmd;
   ArgV *args;
   FDStream *output;
   bool background;

   char *next_cmd;
   char *cmd_buf;
   bool partial_cmd;
   int alias_field; // length of expanded alias (and ttl for used_aliases)

   TouchedAlias *used_aliases;
   void free_used_aliases()
      {
	 if(used_aliases)
	 {
	    TouchedAlias::FreeChain(used_aliases);
	    used_aliases=0;
	 }
	 alias_field=0;
      }

   enum
   {
      COND_ANY,
      COND_AND,
      COND_OR
   }
      condition;

   int	 prev_exit_code;
   int	 exit_code;

   struct cmd_rec
   {
      const char  *name;
      Job   *(CmdExec::*func)();
      const char  *short_desc;
      const char  *long_desc;
   };
   static const cmd_rec cmd_table[];

   void print_cmd_help(const char *cmd);
   static int find_cmd(const char *cmd_name,const cmd_rec **ret);

   void exec_parsed_command();

   enum parse_result
   {
      PARSE_OK,
      PARSE_ERR,
      PARSE_AGAIN
   };
   parse_result parse_one_cmd();

   enum builtins
   {
      BUILTIN_OPEN,
      BUILTIN_CD
   }
      builtin;

   static CmdExec *debug_shell;
   static void debug_callback(char *);

   CmdFeeder *feeder;

   char *old_cwd;
   char *old_lcwd;

   FILE *debug_file;
   void CloseDebug();
   int  OpenDebug(const char *file);

public:
   void FeedCmd(const char *c);
   void PrependCmd(const char *c);
   void ExecParsed(ArgV *a,FDStream *o=0,bool b=false);

   in_CMD(alias); in_CMD(anon);  in_CMD(cd);      in_CMD(debug);
   in_CMD(exit);  in_CMD(get);   in_CMD(help);    in_CMD(jobs);
   in_CMD(kill);  in_CMD(lcd);   in_CMD(ls);      in_CMD(mget);
   in_CMD(open);  in_CMD(pwd);   in_CMD(put);     in_CMD(set);
   in_CMD(shell); in_CMD(source);in_CMD(user);    in_CMD(rm);
   in_CMD(wait);  in_CMD(site);  in_CMD(subsh);   in_CMD(mirror);
   in_CMD(mput);  in_CMD(mv);	 in_CMD(cat);     in_CMD(cache);
   in_CMD(mkdir); in_CMD(quote); in_CMD(scache);  in_CMD(mrm);
   in_CMD(ver);	  in_CMD(close); in_CMD(bookmark);in_CMD(lftp);

   static const char * const var_list[];

   CmdExec(FileAccess *s);
   ~CmdExec();

   bool Idle();	// when we have no command running and command buffer is empty
   int Done();
   int ExitCode() { return exit_code; }
   int Do();
   void PrintStatus(int);
   void ShowRunStatus(StatusLine *s);
   int AcceptSig(int sig);

   char *MakePrompt();

   bool interactive;
   StatusLine *status_line;
   void SetCmdFeeder(CmdFeeder *new_feeder);
   void	RemoveFeeder();

   friend char *command_generator(char *,int);	  // readline completor
   friend int remote_cmd(int);

   char	 *var_ls;
   char	 *var_prompt;
   bool	 remote_completion;
   int	 long_running;
   bool	 csh_history;

   void	 Reconfig();

   void	 beep_if_long();
   time_t start_time;

   static CmdExec *cwd_owner;
   char	 *cwd;
   void	 SaveCWD();
   void	 RestoreCWD();

   FDStream *default_output;

   void vfprintf(FILE *file,const char *f,va_list v);

   void SetInteractive(bool i);
};

#endif//CMDEXEC_H
