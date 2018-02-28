/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef CMDEXEC_H
#define CMDEXEC_H

#include <stdarg.h>

#include "Job.h"
#include "ArgV.h"
#include "Filter.h"
#include "alias.h"
#include "History.h"
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

   virtual bool IsInteractive() const { return false; }
};

extern CmdFeeder *lftp_feeder;	 // feeder to use after 'lftp' command

class CmdExec : public SessionJob, public ResClient
{
public:
// current command data
   Ref<ArgV> args;
   Ref<FDStream> output;
   bool background;
   int	 exit_code;
   int	 prev_exit_code;

private:
   CmdExec *parent_exec;
   Buffer cmd_buf;
   bool partial_cmd;
   int alias_field; // length of expanded alias (and ttl for used_aliases)
   int failed_exit_code;

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

      static int cmp(const CmdExec::cmd_rec *a,const CmdExec::cmd_rec *b);
   };
   static const cmd_rec static_cmd_table[];
   static const int static_cmd_table_length;
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

   bool fed_at_finish;
   void AtFinish();

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

   void init(LocalDirectory *c);

public:
   void FeedCmd(const char *c);
   void FeedArgV(const ArgV *,int start=0);
   void PrependCmd(const char *c);
   void ExecParsed(ArgV *a,FDStream *o=0,bool b=false);
   static bool needs_quotation(const char *buf,int len);
   static bool needs_quotation(const char *buf) { return needs_quotation(buf,strlen(buf)); }
   static bool quotable(char c,char in_quotes);
   static bool is_space(char c) { return c==' ' || c=='\t'; }
   static bool is_quote(char c) { return c=='"' || c=='\''; }
   void FeedQuoted(const char *c);
   void Exit(int);
   void AtExit();
   void AtExitBg();
   void AtExitFg();
   void AtBackground();
   void AtTerminate();
   void EmptyCmds();
   bool WriteCmds(int fd) const;
   bool ReadCmds(int fd); // does not clear queue before reading (appends)

   void AddNewJob(Job *new_job);
   void SuspendJob(Job *j);

   CmdExec(FileAccess *s,LocalDirectory *c);
   CmdExec(CmdExec *parent);
   ~CmdExec();

   bool Idle();	// when we have no command running and command buffer is empty
   int Done();
   int ExitCode() { return failed_exit_code ? failed_exit_code : exit_code; }
   int Do();
   xstring& FormatStatus(xstring&,int,const char *prefix="\t");
   void ShowRunStatus(const SMTaskRef<StatusLine>& s);
   int AcceptSig(int sig);

   const char *FormatPrompt(const char *scan);
   const char *MakePrompt();

   bool interactive;
   bool show_status;
   bool top_level;
   bool verbose;
   bool auto_terminate_in_bg;
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
   Ref<LocalDirectory> cwd;
   void	 SaveCWD();
   int	 RestoreCWD();

   FDStream *default_output;

   void top_vfprintf(FILE *file,const char *f,va_list v);

   void SetInteractive(bool i);
   void SetInteractive();
   void SetTopLevel()
      {
	 top_level=true;
	 Reconfig(0);
	 SetInteractive();
      }
   void SetStatusLine(StatusLine *s) { status_line=s; }
   void SetAutoTerminateInBackground(bool b) { auto_terminate_in_bg=b; }

   static void RegisterCommand(const char *name,cmd_creator_t creator,
      const char *short_name=0,const char *long_name=0);

   static void RegisterCompatCommand(const char *name,cmd_creator_t creator,
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
   Job *builtin_local();

   bool load_cmd_module(const char *op);
   Job *default_cmd();

   void ChangeSession(FileAccess *new_session);

   bool print_cmd_help(const char *cmd);
   void print_cmd_index();

   static const char *CmdByIndex(int i);

   void enable_debug(const char *opt=0);

   int	 last_bg;
   bool	 wait_all;

   void pre_stdout();

   void ChangeSlot(const char *n);

   static JobRef<CmdExec> top;
};

extern const char * const bookmark_subcmd[];
extern const char * const cache_subcmd[];

#endif//CMDEXEC_H
