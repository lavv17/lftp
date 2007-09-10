/*
 * lftp and utils
 *
 * Copyright (c) 1996-2007 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "bookmark.h"
#include "FileGlob.h"

class QueueFeeder;
class LocalDirectory;

extern History cwd_history;

#define	CMD(name) Job *cmd_##name(CmdExec *parent)

typedef Job * (*cmd_creator_t)(class CmdExec *parent);

class CmdFeeder
{
public:
   xstring_c saved_buf;
   CmdFeeder *prev;
   virtual const char *NextCmd(class CmdExec *exec,const char *prompt) = 0;
   virtual ~CmdFeeder() {}

   virtual void clear() {}
   virtual bool RealEOF() { return true; }

   virtual void Fg() {}
   virtual void Bg() {}
};

extern CmdFeeder *lftp_feeder;	 // feeder to use after 'lftp' command

class CmdExec : public SessionJob
{
public:
// current command data
   Ref<ArgV> args;
   Ref<FDStream> output;
   bool background;
   int	 exit_code;
   int	 prev_exit_code;

private:
   Buffer cmd_buf;
   bool partial_cmd;
   int alias_field; // length of expanded alias (and ttl for used_aliases)

   TouchedAlias *used_aliases;
   void free_used_aliases();

   void skip_cmd(int len);

   enum
   {
      COND_ANY,
      COND_AND,
      COND_OR
   }
      condition;

   struct cmd_rec
   {
      const char  *name;
      cmd_creator_t creator;
      const char  *short_desc;
      const char  *long_desc;
   };
   static const cmd_rec static_cmd_table[];
   static xarray<cmd_rec> dyn_cmd_table;

   static int find_cmd(const char *cmd_name,const cmd_rec **ret);

   void exec_parsed_command();

   enum parse_result
   {
      PARSE_OK,
      PARSE_ERR,
      PARSE_AGAIN
   };
   parse_result parse_one_cmd();

   CmdFeeder *feeder;
   bool feeder_called;

   enum builtins
   {
      BUILTIN_NONE=0,
      BUILTIN_OPEN,
      BUILTIN_CD,
      BUILTIN_EXEC_RESTART,
      BUILTIN_GLOB
   }
      builtin;

   FileAccess::Path old_cwd;
   xstring_c old_lcwd;
   xstring_c slot;

   Ref<GlobURL> glob;
   Ref<ArgV> args_glob;

   int redirections;

   static CmdExec *chain;
   CmdExec *next;

   QueueFeeder *queue_feeder;
   CmdExec *GetQueue(bool create = true);
   bool SameQueueParameters(CmdExec *,const char *);
   int max_waiting;

   FileAccessRef saved_session;
   void ReuseSavedSession();
   void RevertToSavedSession();

public:
   void FeedCmd(const char *c);
   void FeedArgV(const ArgV *,int start=0);
   void PrependCmd(const char *c);
   void ExecParsed(ArgV *a,FDStream *o=0,bool b=false);
   static int unquote(char *buf,const char *str);  // returns buf length
   static const char *unquote(const char *str);
   static bool needs_quotation(const char *buf);
   static bool quotable(char c,char in_quotes);
   static bool is_space(char c) { return c==' ' || c=='\t'; }
   static bool is_quote(char c) { return c=='"' || c=='\''; }
   void FeedQuoted(const char *c);
   void Exit(int);
   void AtExit();
   void EmptyCmds();
   bool WriteCmds(int fd) const;
   bool ReadCmds(int fd); // does not clear queue before reading (appends)

   void SuspendJob(Job *j);

   CmdExec(FileAccess *s,LocalDirectory *c);
   ~CmdExec();

   bool Idle();	// when we have no command running and command buffer is empty
   int Done();
   int ExitCode() { return exit_code; }
   int Do();
   void PrintStatus(int,const char *prefix="\t");
   void ShowRunStatus(const SMTaskRef<StatusLine>& s);
   int AcceptSig(int sig);

   const char *FormatPrompt(const char *scan);
   const char *MakePrompt();

   bool interactive;
   bool top_level;
   bool verbose;
   SMTaskRef<StatusLine> status_line;
   void SetCmdFeeder(CmdFeeder *new_feeder);
   void	RemoveFeeder();

   friend char *command_generator(char *,int);	  // readline completor
   static const char *GetFullCommandName(const char *);

   bool	 remote_completion;
   int	 long_running;
   bool	 csh_history;
   bool	 verify_host;
   bool	 verify_path;
   bool	 verify_path_cached;

   void	 Reconfig(const char *name=0);

   void	 beep_if_long();
   time_t start_time;

   static CmdExec *cwd_owner;
   LocalDirectory *cwd;
   void	 SaveCWD();
   int	 RestoreCWD();

   FDStream *default_output;

   void top_vfprintf(FILE *file,const char *f,va_list v);

   void SetInteractive(bool i);
   void SetTopLevel()
      {
	 top_level=true;
	 Reconfig(0);
      }
   void SetStatusLine(StatusLine *s) { status_line=s; }

   static void RegisterCommand(const char *name,cmd_creator_t creator,
      const char *short_name=0,const char *long_name=0);

   Job *builtin_lcd();
   Job *builtin_cd();
   Job *builtin_open();
   Job *builtin_exit();
   Job *builtin_lftp();
   Job *builtin_restart();
   Job *builtin_glob();
   Job *builtin_queue();
   Job *builtin_queue_edit();

   Job *default_cmd();

   void ChangeSession(FileAccess *new_session);

   void print_cmd_help(const char *cmd);
   void print_cmd_index();

   static const cmd_rec *CmdByIndex(int i);

   int	 last_bg;
   bool	 wait_all;

   void pre_stdout();

   void ChangeSlot(const char *n);

   static JobRef<CmdExec> top;
};

extern const char * const bookmark_subcmd[];
extern const char * const cache_subcmd[];

#endif//CMDEXEC_H
