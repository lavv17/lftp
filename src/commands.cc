/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "modconfig.h"

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <assert.h>

#include "CmdExec.h"
#include "GetJob.h"
#include "CatJob.h"
#include "LsCache.h"
#include "mgetJob.h"
#include "mkdirJob.h"
#include "rmJob.h"
#include "SysCmdJob.h"
#include "mvJob.h"
#include "pgetJob.h"
#include "FtpCopy.h"
#include "SleepJob.h"
#include "FindJob.h"
#include "ChmodJob.h"
#include "CopyJob.h"

#include "misc.h"
#include "alias.h"
#include "netrc.h"
#include "url.h"
#include "GetPass.h"
#include "SignalHook.h"
#include "FileFeeder.h"
#include "xalloca.h"
#include "bookmark.h"
#include "log.h"
#include "module.h"
#include "getopt.h"
#include "FileCopy.h"

#include "confpaths.h"

#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)

Bookmark lftp_bookmarks;
History	 cwd_history;

CMD(alias); CMD(anon);   CMD(cd);      CMD(debug);
CMD(exit);  CMD(get);    CMD(help);    CMD(jobs);
CMD(kill);  CMD(lcd);    CMD(ls);
CMD(open);  CMD(pwd);    CMD(set);
CMD(shell); CMD(source); CMD(user);    CMD(rm);
CMD(wait);  CMD(subsh);  CMD(mirror);
CMD(mv);    CMD(cat);    CMD(cache);
CMD(mkdir); CMD(scache); CMD(mrm);
CMD(ver);   CMD(close);  CMD(bookmark);CMD(lftp);
CMD(echo);  CMD(suspend);CMD(ftpcopy); CMD(sleep);
CMD(at);    CMD(find);   CMD(command); CMD(module);
CMD(lpwd);  CMD(glob);	 CMD(chmod);   CMD(queue);
CMD(repeat);CMD(get1);

#ifdef MODULE_CMD_MIRROR
# define cmd_mirror 0
#endif
#ifdef MODULE_CMD_SLEEP
# define cmd_sleep  0
# define cmd_at     0
# define cmd_repeat 0
#endif

#define S "\001"

const struct CmdExec::cmd_rec CmdExec::static_cmd_table[]=
{
   {"!",       cmd_shell,  N_("!<shell_command>"),
	 N_("Launch shell or shell command\n")},
   {"(",       cmd_subsh,  N_("(commands)"),
	 N_("Group commands together to be executed as one command\n"
	 "You can launch such a group in background\n")},
   {"?",       cmd_help,   0,"help"},
   {"alias",   cmd_alias,  N_("alias [<name> [<value>]]"),
	 N_("Define or undefine alias <name>. If <value> omitted,\n"
	 "the alias is undefined, else is takes the value <value>.\n"
         "If no argument is given the current aliases are listed.\n")},
   {"anon",    cmd_anon,   "anon",
	 N_("anon - login anonymously (by default)\n")},
   {"at",      cmd_at},
   {"bookmark",cmd_bookmark,N_("bookmark [SUBCMD]"),
	 N_("bookmark command controls bookmarks\n\n"
	 "The following subcommands are recognized:\n"
	 "  add <name> [<loc>] - add current place or given location to bookmarks\n"
	 "                       and bind to given name\n"
	 "  del <name>         - remove bookmark with the name\n"
	 "  edit               - start editor on bookmarks file\n"
	 "  import <type>      - import foreign bookmarks\n"
	 "  list               - list bookmarks (default)\n")},
   {"bye",     cmd_exit,   0,"exit"},
   {"cache",   cmd_cache,  N_("cache [SUBCMD]"),
	 N_("cache command controls local memory cache\n\n"
	 "The following subcommands are recognized:\n"
	 "  stat        - print cache status (default)\n"
	 "  on|off      - turn on/off caching\n"
	 "  flush       - flush cache\n"
	 "  size <lim>  - set memory limit, -1 means unlimited\n"
	 "  expire <Nx> - set cache expiration time to N seconds (x=s)\n"
	 "                minutes (x=m) hours (x=h) or days (x=d)\n")},
   {"cat",     cmd_cat,    N_("cat [-b] <files>"),
	 N_("cat - output remote files to stdout (can be redirected)\n"
	 " -b  use binary mode (ascii is the default)\n")},
   {"cd",      cmd_cd,     N_("cd <rdir>"),
	 N_("Change current remote directory to <rdir>. The previous remote directory\n"
	 "is stored as `-'. You can do `cd -' to change the directory back.\n"
	 "The previous directory for each site is also stored on disk, so you can\n"
	 "do `open site; cd -' even after lftp restart.\n")},
   {"chmod",   cmd_chmod,   N_("chmod mode file..."), 0},
   {"close",   cmd_close,   "close [-a]",
	 N_("Close idle connections. By default only with current server.\n"
	 " -a  close idle connections with all servers\n")},
   {"connect", cmd_open,   0,"open"},
   {"command", cmd_command},
   {"debug",   cmd_debug,  N_("debug [<level>|off] [-o <file>]"),
	 N_("Set debug level to given value or turn debug off completely.\n"
	 " -o <file>  redirect debug output to the file.\n")},
   {"echo",    cmd_echo,   0},
   {"exit",    cmd_exit,   N_("exit [<code>]"),
	 N_("exit - exit from lftp or move to background if jobs are active\n\n"
	 "If no jobs active, the code is passed to operating system as lftp\n"
	 "termination status. If omitted, exit code of last command is used.\n")},
   {"fg",      cmd_wait,   0,"wait"},
   {"find",    cmd_find},
   {"ftpcopy", cmd_ftpcopy},
   {"get",     cmd_get,    N_("get [OPTS] <rfile> [-o <lfile>]"),
	 N_("Retrieve remote file <rfile> and store it to local file <lfile>.\n"
	 " -o <lfile> specifies local file name (default - basename of rfile)\n"
	 " -c  continue, reget\n"
	 " -e  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n")},
   {"get1",    cmd_get1,   0,0},
   {"glob",    cmd_glob,   0,0},
   {"help",    cmd_help,   N_("help [<cmd>]"),
	 N_("Print help for command <cmd>, or list of available commands\n")},
   {"jobs",    cmd_jobs,   "jobs [-v]",
	 N_("List running jobs. -v means verbose, several -v can be specified.\n")},
   {"kill",    cmd_kill,   N_("kill all|<job_no>"),
	 N_("Delete specified job with <job_no> or all jobs\n")},
   {"lcd",     cmd_lcd,    N_("lcd <ldir>"),
	 N_("Change current local directory to <ldir>. The previous local directory\n"
	 "is stored as `-'. You can do `lcd -' to change the directory back.\n")},
   {"lftp",    cmd_lftp,   N_("lftp [OPTS] <site>"),
	 N_("`lftp' is the first command executed by lftp after rc files\n"
	 " -f <file>           execute commands from the file and exit\n"
	 " -c <cmd>            execute the commands and exit\n"
	 " --help              print this help and exit\n"
	 " --version           print lftp version and exit\n"
	 "Other options are the same as in `open' command\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"lpwd",    cmd_lpwd},
   {"login",   cmd_user,   0,"user"},
   {"ls",      cmd_ls,	    N_("ls [<args>]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	 "or via pipe to external command.\n"
	 "By default, ls output is cached, to see new listing use `rels' or\n"
	 "`cache flush'.\n")},
   {"mget",    cmd_get,	   N_("mget [OPTS] <files>"),
	 N_("Gets selected files with expanded wildcards\n"
	 " -c  continue, reget\n"
	 " -d  create directories the same as in file names and get the\n"
	 "     files into them instead of current directory\n"
	 " -e  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n")},
   {"mirror",  cmd_mirror, N_("mirror [OPTS] [remote [local]]"),
	 N_("\nMirror specified remote directory to local directory\n\n"
	 " -c, --continue         continue a mirror job if possible\n"
	 " -e, --delete           delete files not present at remote site\n"
	 " -s, --allow-suid       set suid/sgid bits according to remote site\n"
	 " -n, --only-newer       download only newer files (-c won't work)\n"
	 " -r, --no-recursion     don't go to subdirectories\n"
	 " -p, --no-perms         don't set file permissions\n"
	 "     --no-umask         don't apply umask to file modes\n"
	 " -R, --reverse          reverse mirror (put files)\n"
	 " -L, --dereference      download symbolic links as files\n"
	 " -N, --newer-than FILE  download only files newer than the file\n"
	 " -i RX, --include RX    include matching files (only one allowed)\n"
	 " -x RX, --exclude RX    exclude matching files (only one allowed)\n"
	 "                        RX is extended regular expression\n"
	 " -t Nx, --time-prec Nx  set time precision to N seconds (x=s)\n"
	 "                        minutes (x=m) hours (x=h) or days (x=d)\n"
	 "                        default - 12 hours\n"
	 " -v, --verbose          verbose operation\n"
	 "\n"
	 "When using -R, the first directory is local and the second is remote.\n"
	 "If the second directory is omitted, basename of first directory is used.\n"
	 "If both directories are omitted, current local and remote directories are used.\n"
	 )},
   {"mkdir",   cmd_mkdir,  N_("mkdir [-p] <dirs>"),
	 N_("Make remote directories\n"
	 " -p  make all levels of path\n")},
   {"module",  cmd_module, N_("module name [args]"),
	 N_("Load module (shared object). The module should contain function\n"
	 "   void module_init(int argc,const char *const *argv)\n"
	 "If name contains a slash, then the module is searched in current\n"
	 "directory, otherwise in PKGLIBDIR.\n")},
   {"more",    cmd_cat,    N_("more <files>"),
	 N_("Same as `cat <files> | more'. if PAGER is set, it is used as filter\n")},
   {"mput",    cmd_get,	   N_("mput [OPTS] <files>"),
	 N_("Upload files with wildcard expansion\n"
	 " -c  continue, reput\n"
	 " -d  create directories the same as in file names and put the\n"
	 "     files into them instead of current directory\n"
	 " -e  delete remote files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n")},
   {"mrm",     cmd_mrm,    N_("mrm <files>"),
	 N_("Removes specified files with wildcard expansion\n")},
   {"mv",      cmd_mv,	    N_("mv <file1> <file2>"),
	 N_("Rename <file1> to <file2>\n")},
   {"nlist",   cmd_ls,     N_("nlist [<args>]"),
	 N_("List remote file names\n")},
   {"open",    cmd_open,   N_("open [OPTS] <site>"),
	 N_("Select a server, URL or bookmark\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"pget",    cmd_get,    N_("pget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Gets the specified file using several connections. This can speed up transfer,\n"
	 "but loads the net heavily impacting other users. Use only if you really\n"
	 "have to transfer the file ASAP, or some other user may go mad :)\n"
	 "\nOptions:\n"
	 " -n <maxconn>  set maximum number of connections (default 5)\n")},
   {"put",     cmd_get,    N_("put [OPTS] <lfile> [-o <rfile>]"),
	 N_("Upload <lfile> with remote name <rfile>.\n"
	 " -o <rfile> specifies remote file name (default - basename of lfile)\n"
	 " -c  continue, reput\n"
	 "     it requires permission to overwrite remote files\n"
	 " -e  delete local files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n")},
   {"pwd",     cmd_pwd,    "pwd [-u]",
	 N_("Print current remote URL.\n"
	 " -u  show password\n")},
   {"queue",   cmd_queue,  0,
	 N_("Usage: queue <command>\n"
	 "Add the command to queue for current site. Each site has its own\n"
	 "command queue.\n")},
   {"quit",    cmd_exit,   0,"exit"},
   {"quote",   cmd_ls,	   N_("quote <cmd>"),
	 N_("Send the command uninterpreted. Use with caution - it can lead to\n"
	 "unknown remote state and thus will cause reconnect. You cannot\n"
	 "be sure that any change of remote state because of quoted command\n"
	 "is solid - it can be reset by reconnect at any time.\n")},
   {"reget",   cmd_get,    N_("reget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Same as `get -c'\n")},
   {"rels",    cmd_ls,	    N_("rels [<args>]"),
	 N_("Same as `ls', but don't look in cache\n")},
   {"renlist", cmd_ls,	    N_("renlist [<args>]"),
	 N_("Same as `nlist', but don't look in cache\n")},
   {"repeat",  cmd_repeat},
   {"reput",   cmd_get,    N_("reput <lfile> [-o <rfile>]"),
	 N_("Same as `put -c'\n")},
   {"rm",      cmd_rm,	    N_("rm [-r] <files>"),
	 N_("Remove remote files\n"
	    " -r  recursive directory remove, be careful\n")},
   {"rmdir",   cmd_rm,	    N_("rmdir <dirs>"),
	 N_("Remove remote directories\n")},
   {"scache",  cmd_scache, N_("scache [<session_no>]"),
	 N_("List cached sessions or switch to specified session number\n")},
   {"set",     cmd_set,    N_("set [OPT] [<var> [<val>]]"),
	 N_("Set variable to given value. If the value is omitted, unset the variable.\n"
	 "Variable name has format ``name/closure'', where closure can specify\n"
	 "exact application of the setting. See lftp(1) for details.\n"
         "If set is called with no variable then only altered settings are listed.\n"
	 "It can be changed by options:\n"
   	 " -a  list all settings, including default values\n"
	 " -d  list only default values, not necessary current ones\n")},
   {"shell",   cmd_shell,  0,"!"},
   {"site",    cmd_ls,	   N_("site <site_cmd>"),
	 N_("Execute site command <site_cmd> and output the result\n"
	 "You can redirect its output\n")},
   {"sleep",   cmd_sleep, 0,
	 N_("Usage: sleep <time>[unit]\n"
	 "Sleep for given amount of time. The time argument can be optionally\n"
	 "followed by unit specifier: d - days, h - hours, m - minutes, s - seconds.\n"
	 "By default time is assumed to be seconds.\n")},
   {"source",  cmd_source, N_("source <file>"),
	 N_("Execute commands recorded in file <file>\n")},
   {"suspend", cmd_suspend},
   {"user",    cmd_user,   N_("user <user> [<pass>]"),
	 N_("Use specified info for remote login\n")},
   {"version", cmd_ver,    "version",
	 N_("Shows lftp version\n")},
   {"wait",    cmd_wait,   N_("wait [<jobno>]"),
	 N_("Wait for specified job to terminate. If jobno is omitted, wait\n"
	 "for last backgrounded job.\n")},
   {"zcat",    cmd_cat,    N_("zcat <files>"),
	 N_("Same as cat, but filter each file through zcat\n")},
   {"zmore",   cmd_cat,    N_("zmore <files>"),
	 N_("Same as more, but filter each file through zcat\n")},

   {NULL,NULL}
};

// returns:
//    0 - no match
//    1 - found, if *res==0 then ambiguous
static
int find_command(const char *unprec_name,const char * const *names,
	         const char **res)
{
   const char *match=0;
   for( ; *names; names++)
   {
      const char *s,*u;
      for(s=*names,u=unprec_name; *s && *u==*s; s++,u++)
	 ;
      if(*s && !*u)
      {
	 if(match)
	 {
	    *res=0;
	    return 1;
	 }
	 match=*names;
      }
      else if(!*s && !*u)
      {
	 *res=*names;
	 return 1;
      }
   }
   if(match)
   {
      *res=match;
      return 1;
   }
   *res=0;
   return 0;
}

Job *CmdExec::builtin_lcd()
{
   if(args->count()==1)
      args->Append("~");

   if(args->count()!=2)
   {
      eprintf(_("Usage: %s local-dir\n"),args->getarg(0));
      return 0;
   }
   const char *cd_to=args->getarg(1);

   if(!strcmp(cd_to,"-"))
   {
      if(old_lcwd)
	 cd_to=old_lcwd;
   }

   cd_to=expand_home_relative(cd_to);

   RestoreCWD();

   int res=chdir(cd_to);
   if(res==-1)
   {
      perror(cd_to);
      exit_code=1;
      return 0;
   }

   xfree(old_lcwd);
   old_lcwd=cwd;
   cwd=0;

   SaveCWD();  // this allocates cwd again

   if(interactive)
      eprintf(_("lcd ok, local cwd=%s\n"),cwd);

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_cd()
{
   if(args->count()==1)
      args->Append("~");

   if(args->count()!=2)
   {
      // xgettext:c-format
      eprintf(_("Usage: cd remote-dir\n"));
      return 0;
   }

   const char *dir=args->getarg(1);

   if(url::is_url(dir))
      return builtin_open();

   if(!strcmp(dir,"-"))
   {
      dir=cwd_history.Lookup(session);
      if(!dir)
      {
	 eprintf(_("%s: no old directory for this site\n"),args->a0());
	 return 0;
      }
      args->setarg(1,dir); // for status line
   }

   xfree(old_cwd);
   old_cwd=xstrdup(session->GetCwd());

   if (!verify_path || background)
   {
      session->Chdir(dir,false);
      exit_code=0;
      return 0;
   }
   session->Chdir(dir);
   while(session->Do()==MOVED);
   builtin=BUILTIN_CD;
   return this;
}

Job *CmdExec::builtin_exit()
{
   if(args->count()>=2)
   {
      if(sscanf(args->getarg(1),"%i",&prev_exit_code)!=1)
      {
	 eprintf(_("Usage: %s [<exit_code>]\n"),args->a0());
	 return 0;
      }
   }
   while(!Done())
      RemoveFeeder();
   exit_code=prev_exit_code;
   return 0;
}

CmdFeeder *lftp_feeder=0;
Job *CmdExec::builtin_lftp()
{
   int c;
   const char *cmd=0;
   char *acmd;
   static struct option lftp_options[]=
   {
      {"help",no_argument,0,'h'},
      {"version",no_argument,0,'v'},
      {0,0,0,0}
   };

   args->rewind();
   opterr=false;
   while((c=args->getopt_long("+f:c:vh",lftp_options,0))!=EOF)
   {
      switch(c)
      {
      case('h'):
	 cmd="help lftp;";
	 break;
      case('v'):
	 cmd="version;";
	 break;
      case('f'):
	 acmd=string_alloca(20+2*strlen(optarg));
	 strcpy(acmd,"source \"");
	 unquote(acmd+strlen(acmd),optarg);
	 strcat(acmd,"\";");
	 cmd=acmd;
	 break;
      case('c'):
	 acmd=string_alloca(4+strlen(optarg));
	 sprintf(acmd,"%s\n\n",optarg);
	 cmd=acmd;
	 break;
      }
   }
   opterr=true;

   if(cmd)
      PrependCmd(cmd);

   if(Done() && lftp_feeder)  // no feeder and no commands
   {
      SetCmdFeeder(lftp_feeder);
      lftp_feeder=0;
      SetInteractive(isatty(0));
      FeedCmd("||command exit\n");   // if the command fails, quit
   }

   if(!cmd)
   {
      /* if no lftp-specific options were found, call open */
      return builtin_open();
   }
   return 0;
}

Job *CmdExec::builtin_open()
{
   bool	 debug=false;
   char	 *port=NULL;
   char	 *host=NULL;
   char  *path=NULL;
   char	 *user=NULL;
   char	 *pass=NULL;
   int	 c;
   NetRC::Entry *nrc=0;
   char  *cmd_to_exec=0;
   char  *op=args->a0();
   bool insecure=false;
   bool no_bm=false;

   args->rewind();
   while((c=args->getopt("u:p:e:dB"))!=EOF)
   {
      switch(c)
      {
      case('p'):
	 port=optarg;
	 break;
      case('u'):
         user=optarg;
         pass=strchr(optarg,',');
	 if(pass==NULL)
	    pass=strchr(optarg,' ');
	 if(pass==NULL)
   	    break;
	 *pass=0;
	 pass++;
	 insecure=true;
         break;
      case('d'):
	 debug=true;
	 break;
      case('e'):
	 cmd_to_exec=optarg;
	 break;
      case('B'):
	 no_bm=true;
	 break;
      case('?'):
	 if(!strcmp(op,"lftp"))
	    eprintf(_("Try `%s --help' for more information\n"),op);
	 else
	    eprintf(_("Usage: %s [-e cmd] [-p port] [-u user[,pass]] <host|url>\n"),
	       op);
	 return 0;
      }
   }

   if(optind<args->count())
      host=args->getarg(optind++);

   ParsedURL *url=0;

   const char *bm=0;

   if(cmd_to_exec)
      PrependCmd(cmd_to_exec);

   if(!no_bm && host && (bm=lftp_bookmarks.Lookup(host))!=0)
   {
      char *cmd=string_alloca(5+3+strlen(bm)+2+1);
      sprintf(cmd,"open -B %s%s\n",bm,background?"&":"");
      PrependCmd(cmd);
   }
   else
   {
      if(host)
      {
	 url=new ParsedURL(host);

	 const ParsedURL &uc=*url;
	 if(uc.host)
	 {
	    cwd_history.Set(session,session->GetCwd());

	    FileAccess *new_session=0;
	    // get session with specified protocol if protocol differs
	    if(uc.proto && strcmp(uc.proto,session->GetProto())
	    || session->GetProto()[0]==0) // or if current session is dummy
	    {
	       const char *p=uc.proto;
	       if(!p)
		  p="ftp";
	       new_session=FileAccess::New(p);
	       if(!new_session)
	       {
		  eprintf("%s: %s%s\n",args->a0(),p,
			   _(" - not supported protocol"));
		  return 0;
	       }
	    }
	    else
	    {
	       new_session=session->Clone();
	       new_session->AnonymousLogin();
	    }
	    Reuse(session);
	    session=new_session;

	    if(uc.user && !user)
	       user=uc.user;
	    if(uc.pass && !pass)
	    {
	       pass=uc.pass;
	       insecure=true;
	    }
	    host=uc.host;
	    if(uc.port!=0 && port==0)
	       port=uc.port;
	    if(uc.path && !path)
	       path=uc.path;
	 }

	 if(!strcmp(session->GetProto(),"ftp")
	 || !strcmp(session->GetProto(),"hftp"))
	 {
	    nrc=NetRC::LookupHost(host);
	    if(nrc)
	    {
	       if(nrc->user && !user
	       || (nrc->user && user && !strcmp(nrc->user,user) && !pass))
	       {
		  user=nrc->user;
		  if(nrc->pass)
		     pass=nrc->pass;
	       }
	    }
	 }
      }
      if(host)
	 session->Connect(host,port);
      if(user)
      {
	 if(!pass)
	    pass=GetPass(_("Password: "));
	 if(!pass)
	    eprintf(_("%s: GetPass() failed -- assume anonymous login\n"),
	       args->getarg(0));
	 else
	 {
	    session->Login(user,0);
	    // assume the new password is the correct one.
	    session->SetPasswordGlobal(pass);
	    session->InsecurePassword(insecure && !no_bm);
	 }
      }
      if(host)
      {
	 if(verify_host && !background)
	 {
	    session->ConnectVerify();
	    builtin=BUILTIN_OPEN;
	 }
      }
      if(nrc)
	 delete nrc;
   } // !bookmark

   if(path)
   {
      char *s=string_alloca(strlen(path)*4+40);
      strcpy(s,"&& cd \"");
      unquote(s+strlen(s),path);
      strcat(s,"\"");
      if(background)
	 strcat(s,"&");
      strcat(s,"\n");
#if 0
      char *slash=strrchr(path,'/');
      if(slash && slash[1])
      {
	 *slash=0;
	 strcat(s,"|| (cd \"");
	 unquote(s+strlen(s),path);
	 strcat(s,"\" && get -- \"");
	 unquote(s+strlen(s),slash+1);
	 strcat(s,"\")\n");
      }
#endif
      PrependCmd(s);
   }

   if(debug)
      PrependCmd("debug\n");

   if(url)
      delete url;

   Reconfig(0);

   if(builtin==BUILTIN_OPEN)
      return this;

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_restart()
{
   builtin=BUILTIN_EXEC_RESTART;
   return this;
}

Job *CmdExec::builtin_glob()
{
   if(args->count()<2)
   {
      eprintf(_("Usage: %s command args...\n"),args->a0());
      return 0;
   }
   assert(args_glob==0 && glob==0);
   args_glob=new ArgV();
   args->rewind();
   args_glob->Append(args->getnext());
   const char *pat=args->getnext();
   if(!pat)
   {
      delete args_glob;
      args_glob=0;
      args->rewind();
      return cmd_command(this);
   }
   glob=new GlobURL(session,pat);
   builtin=BUILTIN_GLOB;
   return this;
}

Job *CmdExec::builtin_queue()
{
   if(args->count()==1)
   {
      eprintf(_("Usage: %s command args...\n"),args->a0());
      return 0;
   }

   CmdExec *queue=FindQueue();
   if(queue==0)
   {
      queue=new CmdExec(session->Clone());
      queue->parent=this;
      queue->AllocJobno();
      const char *url=session->GetConnectURL(FA::NO_PATH);
      queue->cmdline=(char*)xmalloc(9+strlen(url));
      sprintf(queue->cmdline,"queue (%s)",url);
      queue->is_queue=true;
      queue->queue_cwd=xstrdup(session->GetCwd());
      queue->queue_lcwd=xstrdup(cwd);

      assert(FindQueue()==queue);
   }

   if(xstrcmp(queue->queue_cwd,session->GetCwd()))
   {
      ArgV cd("cd");
      cd.Append(session->GetCwd());
      cd.Append("&");
      queue->FeedArgV(&cd);

      xfree(queue->queue_cwd);
      queue->queue_cwd=xstrdup(session->GetCwd());
   }
   if(xstrcmp(queue->queue_lcwd,cwd))
   {
      ArgV cd("lcd");
      cd.Append(cwd);
      cd.Append("&");
      queue->FeedArgV(&cd);

      xfree(queue->queue_lcwd);
      queue->queue_lcwd=xstrdup(cwd);
   }

   queue->FeedArgV(args,1);

   exit_code=0;

   return 0;
}

// below are only non-builtin commands
#define args	  (parent->args)
#define exit_code (parent->exit_code)
#define output	  (parent->output)
#define session	  (parent->session)
#define Clone()	  session->Clone()
#define eprintf	  parent->eprintf

CMD(lcd)
{
   return parent->builtin_lcd();
}

CMD(ls)
{
   bool nlist=false;
   bool re=false;
   int mode=FA::LIST;
   const char *op=args->a0();
   if(strstr(op,"nlist"))
      nlist=true;
   if(!strncmp(op,"re",2))
      re=true;
   if(!strcmp(op,"quote") || !strcmp(op,"site"))
   {
      if(args->count()<=1)
      {
	 eprintf(_("Usage: %s <cmd>\n"),op);
	 return 0;
      }
      nlist=true;
      mode=FA::QUOTE_CMD;
      if(!strcmp(op,"site"))
	 args->insarg(1,"SITE");
   }

   char *a=args->Combine(1);

   if(!nlist && args->count()==1)
      args->Append(parent->var_ls);

   FileCopyPeer *src_peer=0;
   if(!nlist)
      src_peer=new FileCopyPeerDirList(Clone(),args);
   else
      src_peer=new FileCopyPeerFA(Clone(),a,mode);

   if(re)
      src_peer->NoCache();
   src_peer->SetDate(NO_DATE);
   src_peer->SetSize(NO_SIZE);
   FileCopyPeer *dst_peer=new FileCopyPeerFDStream(output,FileCopyPeer::PUT);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->DontCopyDate();
   c->LineBuffered();
   c->Ascii();

   CopyJob *j=new CopyJob(c,a,op);
   if(!output || output->usesfd(1))
      j->NoStatus();
   xfree(a);
   output=0;
   if(!nlist)
      args=0;  // `ls' consumes args itself.

   return j;
}

CMD(cat)
{
   const char *op=args->a0();
   int opt;
   bool ascii=true;

   while((opt=args->getopt("+bau"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 ascii=true;
	 break;
      case('b'):
	 ascii=false;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);
   args->rewind();
   if(args->count()<=1)
   {
      eprintf(_("Usage: %s [OPTS] files...\n"),op);
      return 0;
   }
   CatJob *j=new CatJob(Clone(),output,args);
   if(ascii && args->a0()[0]!='z')
      j->Ascii();
   output=0;
   args=0;
   return j;
}

CMD(get)
{
   int opt;
   bool cont=false;
   const char *opts="+ceua";
   const char *op=args->a0();
   ArgV	 *get_args=new ArgV(op);
   int n_conn=0;
   bool del=false;
   bool ascii=false;
   bool glob=false;
   bool make_dirs=false;
   bool reverse=false;

   args->rewind();
   if(!strncmp(op,"re",2))
   {
      cont=true;
      opts="+eua";
   }
   if(!strcmp(op,"pget"))
   {
      opts="+n:u";
      n_conn=-1;
   }
   else if(!strcmp(op,"put") || !strcmp(op,"reput"))
   {
      reverse=true;
   }
   else if(!strcmp(op,"mget"))
   {
      glob=true;
      opts="+cead";
   }
   else if(!strcmp(op,"mput"))
   {
      glob=true;
      opts="+cead";
      reverse=true;
   }
   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('n'):
	 if(!isdigit(optarg[0]))
	 {
	    eprintf(_("%s: -n: Number expected. "),op);
	    goto err;
	 }
	 n_conn=atoi(optarg);
	 break;
      case('e'):
	 del=true;
	 break;
      case('a'):
	 ascii=true;
	 break;
      case('d'):
	 make_dirs=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 delete get_args;
	 return 0;
      }
   }
   if(glob)
   {
      if(args->getcurr()==0)
      {
      file_name_missed:
	 // xgettext:c-format
	 eprintf(_("File name missed. "));
	 goto err;
      }
      delete get_args;
      // remove options
      while(args->getindex()>1)
	 args->delarg(1);
      mgetJob *j=new mgetJob(Clone(),args,cont,make_dirs);
      if(reverse)
	 j->Reverse();
      if(del)
	 j->DeleteFiles();
      if(ascii)
	 j->Ascii();
      args=0;
      return j;
   }
   args->back();
   const char *a=args->getnext();
   const char *a1;
   if(a==0)
      goto file_name_missed;
   while(a)
   {
      get_args->Append(a);
      ParsedURL url(a,true);
      a1=0;
      if(url.proto && url.path)
	 a1=basename_ptr(url.path);
      if(a1==0)
	 a1=basename_ptr(a);
      a=args->getnext();
      if(a && !strcmp(a,"-o"))
      {
	 a=args->getnext();
	 if(a)
	 {
	    ParsedURL url1(a,true);
	    if(url1.proto==0 && !reverse)
	    {
	       a=expand_home_relative(a);
	       struct stat st;
	       int res=stat(a,&st);
	       if(res!=-1 && S_ISDIR(st.st_mode))
	       {
		  char *comb=string_alloca(strlen(a)+strlen(a1)+2);
		  sprintf(comb,"%s/%s",a,a1);
		  a=comb;
	       }
	    }
	    else
	    {
	       if(a[0] && a[strlen(a)-1]=='/')
	       {
		  char *dst1=string_alloca(strlen(a)+strlen(a1)+1);
		  strcpy(dst1,a);
		  strcat(dst1,a1);
		  a=dst1;
	       }
	    }
	    get_args->Append(a);
	 }
	 else
	    get_args->Append(a1);
	 a=args->getnext();
      }
      else
	 get_args->Append(a1);
   }

   if(n_conn==0)
   {
      GetJob *j=new GetJob(Clone(),get_args,cont);
      if(del)
	 j->DeleteFiles();
      if(ascii)
	 j->Ascii();
      if(reverse)
	 j->Reverse();
      return j;
   }
   else
   {
      pgetJob *j=new pgetJob(Clone(),get_args);
      if(n_conn!=-1)
	 j->SetMaxConn(n_conn);
      return j;
   }
}

CMD(shell)
{
   Job *j;
   if(args->count()<=1)
      j=new SysCmdJob(0);
   else
   {
      char *a=args->Combine(1);
      j=new SysCmdJob(a);
      free(a);
   }
   return j;
}

CMD(mrm)
{
   args->setarg(0,"glob");
   args->insarg(1,"rm");
   return parent->builtin_restart();
}
CMD(rm)
{
   int opt;
   bool recursive=false;
   const char *opts="+r";

   if(!strcmp(args->a0(),"rmdir"))
      opts="+";

   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('r'):
	 recursive=true;
	 break;
      case('?'):
      print_usage:
	 eprintf(_("Usage: %s files...\n"),args->a0());
	 return 0;
      }
   }
   args->back();
   char *curr=args->getnext();
   if(curr==0)
      goto print_usage;

   if(recursive)
   {
      Job *j=new FinderJob_Cmd(Clone(),args,FinderJob_Cmd::RM);
      args=0;
      return j;
   }

   rmJob *j=(strcmp(args->a0(),"rmdir")
	     ?new rmJob(Clone(),new ArgV(args->a0()))
	     :new rmdirJob(Clone(),new ArgV(args->a0())));

   while(curr)
   {
      j->AddFile(curr);
      curr=args->getnext();
   }

   return j;
}
CMD(mkdir)
{
   Job *j=new mkdirJob(Clone(),args);
   args=0;
   return j;
}

CMD(source)
{
   if(args->count()<2)
   {
      // xgettext:c-format
      eprintf(_("Usage: %s <file>\n"),args->a0());
      return 0;
   }
   parent->SetCmdFeeder(new FileFeeder(new FileStream(args->getarg(1),O_RDONLY)));
   exit_code=0;
   return 0;
}

CMD(jobs)
{
   int opt;
   args->rewind();
   int v=1;
   while((opt=args->getopt("+v"))!=EOF)
   {
      switch(opt)
      {
      case('v'):
	 v++;
	 break;
      case('?'):
         // xgettext:c-format
	 eprintf(_("Usage: %s [-v] [-v] ...\n"),args->a0());
	 return 0;
      }
   }
   parent->ListJobs(v);
   exit_code=0;
   return 0;
}

CMD(cd)
{
   return parent->builtin_cd();
}

CMD(pwd)
{
   int opt;
   args->rewind();
   int flags=0;
   while((opt=args->getopt("p"))!=EOF)
   {
      switch(opt)
      {
      case('p'):
	 flags|=FA::WITH_PASSWORD;
	 break;
      case('?'):
         // xgettext:c-format
	 eprintf(_("Usage: %s [-p]\n"),args->a0());
	 return 0;
      }
   }
   char *url=alloca_strdup(session->GetConnectURL(flags));
   int len=strlen(url);
   url[len++]='\n';  // replaces \0
   Job *j=CopyJob::NewEcho(url,len,output,args->a0());
   output=0;
   return j;
}

CMD(exit)
{
   return parent->builtin_exit();
}

CMD(debug)
{
   const char *op=args->a0();
   int	 new_dlevel=9;
   char	 *debug_file_name=0;
   int 	 fd=-1;
   bool  enabled=true;

   int opt;
   while((opt=args->getopt("o:"))!=EOF)
   {
      switch(opt)
      {
      case('o'):
	 debug_file_name=optarg;
	 if(fd!=-1)
	    close(fd);
	 fd=open(debug_file_name,O_WRONLY|O_CREAT|O_APPEND,0600);
	 if(fd==-1)
	 {
	    perror(debug_file_name);
	    return 0;
	 }
	 fcntl(fd,F_SETFL,O_NONBLOCK);
	 fcntl(fd,F_SETFD,FD_CLOEXEC);
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }

   if(fd==-1)
      Log::global->SetOutput(2,false);
   else
      Log::global->SetOutput(fd,true);

   char *a=args->getcurr();
   if(a)
   {
      if(!strcasecmp(args->getarg(1),"off"))
      {
	 enabled=false;
      }
      else
      {
	 new_dlevel=atoi(args->getarg(1));
	 if(new_dlevel<0)
	    new_dlevel=0;
	 enabled=true;
      }
   }

   if(enabled)
   {
      Log::global->Enable();
      Log::global->SetLevel(new_dlevel);
   }
   else
      Log::global->Disable();


#if 0
   if(interactive)
   {
      if(enabled)
	 printf(_("debug level is %d, output goes to %s\n"),new_dlevel,
		     debug_file_name?debug_file_name:"<stderr>");
      else
	 printf(_("debug is off\n"));
   }
#endif
   exit_code=0;
   return 0;
}

CMD(user)
{
   char	 *pass;
   if(args->count()<2 || args->count()>3)
   {
      eprintf(_("Usage: %s userid [pass]\n"),args->getarg(0));
      return 0;
   }
   bool insecure=false;
   if(args->count()==2)
      pass=GetPass(_("Password: "));
   else
   {
      pass=args->getarg(2);
      insecure=true;
   }
   if(pass)
   {
      ParsedURL u(args->getarg(1),true);
      if(u.proto)
      {
	 FA *s=FA::New(&u);
	 if(s)
	 {
	    s->SetPasswordGlobal(pass);
	    s->InsecurePassword(insecure);
	    SessionPool::Reuse(s);
	 }
	 else
	 {
	    eprintf("%s: %s%s\n",args->a0(),u.proto,
		     _(" - not supported protocol"));
	    return 0;
	 }
      }
      else
      {
	 session->Login(args->getarg(1),0);
	 session->SetPasswordGlobal(pass);
	 session->InsecurePassword(insecure);
      }
   }
   exit_code=0;
   return 0;
}
CMD(anon)
{
   session->AnonymousLogin();
   exit_code=0;
   return 0;
}

CMD(lftp)
{
   return parent->builtin_lftp();
}

CMD(open)
{
   return parent->builtin_open();
}

CMD(kill)
{
   int n;
   const char *op=args->a0();
   if(args->count()<2)
   {
#if 0 // too dangerous to kill last job. Better require explicit number.
      n=parent->last_bg;
      if(n==-1)
      {
	 eprintf(_("%s: no current job\n"),op);
	 return 0;
      }
      printf("%s %d\n",op,n);
      if(Job::Running(n))
      {
	 parent->Kill(n);
	 exit_code=0;
      }
      else
	 eprintf(_("%s: %d - no such job\n"),op,n);
#else
      eprintf(_("Usage: %s <jobno> ... | all\n"),args->getarg(0));
#endif
      return 0;
   }
   if(!strcmp(args->getarg(1),"all"))
   {
      parent->KillAll();
      exit_code=0;
      return 0;
   }
   args->rewind();
   exit_code=0;
   for(;;)
   {
      char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!isdigit(arg[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),op,arg);
	 exit_code=1;
      	 continue;
      }
      n=atoi(arg);
      if(Job::Running(n))
	 parent->Kill(n);
      else
      {
	 eprintf(_("%s: %d - no such job\n"),op,n);
	 exit_code=1;
      }
   }
   return 0;
}

CMD(set)
{
   const char *op=args->a0();
   bool with_defaults=false;
   bool only_defaults=false;
   int c;

   args->rewind();
   while((c=args->getopt("+ad"))!=EOF)
   {
      switch(c)
      {
      case 'a':
	 with_defaults=true;
	 break;
      case 'd':
	 only_defaults=true;
	 break;
      default:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   char *a=args->getnext();

   if(a==0)
   {
      char *s=ResMgr::Format(with_defaults,only_defaults);
      Job *j=CopyJob::NewEcho(s,output,args->a0());
      xfree(s);
      output=0;
      return j;
   }

   char *sl=strchr(a,'/');
   char *closure=0;
   if(sl)
   {
      *sl=0;
      closure=sl+1;
   }

   ResDecl *type;
   // find type of given variable
   const char *msg=ResMgr::FindVar(a,&type);
   if(msg)
   {
      eprintf(_("%s: %s. Use `set -a' to look at all variables.\n"),a,msg);
      return 0;
   }

   args->getnext();
   char *val=(args->getcurr()==0?0:args->Combine(args->getindex()));
   msg=ResMgr::Set(a,closure,val);

   if(msg)
   {
      eprintf("%s: %s.\n",val,msg);
      xfree(val);
      return 0;
   }
   xfree(val);

   exit_code=0;
   return 0;
}

CMD(alias)
{
   if(args->count()<2)
   {
      char *list=Alias::Format();
      Job *j=CopyJob::NewEcho(list,output,args->a0());
      xfree(list);
      output=0;
      return j;
   }
   else if(args->count()==2)
   {
      Alias::Del(args->getarg(1));
   }
   else
   {
      char *val=args->Combine(2);
      Alias::Add(args->getarg(1),val);
      free(val);
   }
   exit_code=0;
   return 0;
}

CMD(wait)
{
   char *op=args->a0();
   if(args->count()>2)
   {
      eprintf(_("Usage: %s [<jobno>]\n"),op);
      return 0;
   }
   int n=-1;
   args->rewind();
   char *jn=args->getnext();
   if(jn)
   {
      if(!isdigit(jn[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),op,jn);
	 return 0;
      }
      n=atoi(jn);
   }
   if(n==-1)
   {
      n=parent->last_bg;
      if(n==-1)
      {
	 eprintf(_("%s: no current job\n"),op);
	 return 0;
      }
      printf("%s %d\n",op,n);
   }
   Job *j=parent->FindJob(n);
   if(j==0)
   {
      eprintf(_("%s: %d - no such job\n"),op,n);
      return 0;
   }
   if(j->parent && j->parent->waiting==j)
   {
      eprintf(_("%s: some other job waits for job %d\n"),op,n);
      return 0;
   }
   j->parent=0;
   return j;
}

CMD(subsh)
{
   CmdExec *e=new CmdExec(Clone());

   char *c=args->getarg(1);
   e->FeedCmd(c);
   e->FeedCmd("\n");
   e->cmdline=(char*)xmalloc(strlen(c)+3);
   sprintf(e->cmdline,"(%s)",c);
   return e;
}

CMD(mv)
{
   if(args->count()!=3)
   {
      // xgettext:c-format
      eprintf(_("Usage: mv <file1> <file2>\n"));
      return 0;
   }
   Job *j=new mvJob(Clone(),args->getarg(1),args->getarg(2));
   return j;
}

const char *const cache_subcmd[]={
   "status","flush","on","off","size","expire",
   NULL
};

CMD(cache)  // cache control
{
   args->rewind();
   const char *op=args->getnext();

   if(!op)
      op="status";
   else if(!find_command(op,cache_subcmd,&op))
   {
      // xgettext:c-format
      eprintf(_("Invalid command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }
   if(!op)
   {
      // xgettext:c-format
      eprintf(_("Ambiguous command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }

   exit_code=0;
   if(!op || !strcmp(op,"status"))
      LsCache::List();
   else if(!strcmp(op,"flush"))
      LsCache::Flush();
   else if(!strcmp(op,"on"))
      LsCache::On();
   else if(!strcmp(op,"off"))
      LsCache::Off();
   else if(!strcmp(op,"size"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for size\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      long lim=-1;
      if(strcmp(op,"unlim") && sscanf(op,"%ld",&lim)!=1)
      {
	 eprintf(_("%s: Invalid number for size\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      LsCache::SetSizeLimit(lim);
   }
   else if(!strcmp(op,"expire"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for `expire'\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      TimeInterval exp(op);
      if(exp.Error())
      {
	 eprintf("%s: %s: %s.\n",args->a0(),op,exp.ErrorText());
	 exit_code=1;
	 return 0;
      }
      LsCache::SetExpire(exp);
   }
   return 0;
}

CMD(scache)
{
   if(args->count()==1)
   {
      SessionPool::Print(stdout);
      exit_code=0;
   }
   else
   {
      char *a=args->getarg(1);
      if(!isdigit(a[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),args->a0(),a);
	 return 0;
      }
      FileAccess *new_session=SessionPool::GetSession(atoi(a));
      if(new_session==0)
      {
	 eprintf(_("%s: %s - no such cached session. Use `scache' to look at session list.\n"),args->a0(),a);
	 return 0;
      }
      parent->ChangeSession(new_session);
   }
   return 0;
}

void CmdExec::print_cmd_help(const char *cmd)
{
   const cmd_rec *c;
   int part=find_cmd(cmd,&c);

   if(part==1)
   {
      if(c->long_desc==0 && c->short_desc==0)
      {
	 printf(_("Sorry, no help for %s\n"),cmd);
	 return;
      }
      if(c->short_desc==0 && !strchr(c->long_desc,' '))
      {
	 printf(_("%s is a built-in alias for %s\n"),cmd,c->long_desc);
	 print_cmd_help(c->long_desc);
	 return;
      }
      if(c->short_desc)
	 printf(_("Usage: %s\n"),_(c->short_desc));
      if(c->long_desc)
	 printf("%s",_(c->long_desc));
      return;
   }
   const char *a=Alias::Find(cmd);
   if(a)
   {
      printf(_("%s is an alias to `%s'\n"),cmd,a);
      return;
   }
   if(part==0)
      printf(_("No such command `%s'. Use `help' to see available commands.\n"),cmd);
   else
      printf(_("Ambiguous command `%s'. Use `help' to see available commands.\n"),cmd);
}

void CmdExec::print_cmd_index()
{
   int i=0;
   const char *c1;
   const cmd_rec *cmd_table=dyn_cmd_table?dyn_cmd_table:static_cmd_table;
   while(cmd_table[i].name)
   {
      while(cmd_table[i].name && !cmd_table[i].short_desc)
	 i++;
      if(!cmd_table[i].name)
	 break;
      c1=cmd_table[i].short_desc;
      i++;
      while(cmd_table[i].name && !cmd_table[i].short_desc)
	 i++;
      if(cmd_table[i].name)
      {
	 printf("\t%-35s %s\n",gettext(c1),gettext(cmd_table[i].short_desc));
	 i++;
      }
      else
	 printf("\t%s\n",_(c1));
   }
}

CMD(help)
{
   if(args->count()>1)
   {
      args->rewind();
      for(;;)
      {
	 char *cmd=args->getnext();
	 if(cmd==0)
	    break;
	 parent->print_cmd_help(cmd);
      }
      return 0;
   }

   parent->print_cmd_index();

   return 0;
}

CMD(ver)
{
   printf(
      _("Lftp | Version %s | Copyright (c) 1996-1999 Alexander V. Lukyanov\n"),VERSION);
   printf(
      _("This is free software with ABSOLUTELY NO WARRANTY. See COPYING for details.\n"));
   printf(
      _("Send bug reports and questions to <%s>.\n"),"lftp@uniyar.ac.ru");
   exit_code=0;
   return 0;
}

CMD(close)
{
   const char *op=args->a0();
   bool all=false;
   int opt;
   while((opt=args->getopt("a"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 all=true;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   if(all)
      session->CleanupAll();
   else
      session->Cleanup();
   exit_code=0;
   return 0;
}

const char * const bookmark_subcmd[]=
   {"add","delete","list","edit","import",0};
static ResDecl res_save_passwords
   ("bmk:save-passwords","no",ResMgr::BoolValidate,0);

CMD(bookmark)
{

   args->rewind();
   const char *op=args->getnext();

   if(!op)
      op="list";
   else if(!find_command(op,bookmark_subcmd,&op))
   {
      // xgettext:c-format
      eprintf(_("Invalid command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }
   if(!op)
   {
      // xgettext:c-format
      eprintf(_("Ambiguous command. "));
      eprintf(_("Try `help %s' for more information.\n"),args->a0());
      return 0;
   }

   if(!strcmp(op,"list"))
   {
      char *list=lftp_bookmarks.Format();
      Job *j=CopyJob::NewEcho(list,output,args->a0());
      xfree(list);
      output=0;
      return j;
   }
   else if(!strcmp(op,"add"))
   {
      const char *key=args->getnext();
      if(key==0 || key[0]==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else
      {
	 const char *value=args->getnext();
	 int flags=0;
	 if((bool)res_save_passwords.Query(0))
	    flags|=session->WITH_PASSWORD;
	 if(value==0)
	 {
	    value=session->GetConnectURL(flags);
	    char *a=string_alloca(strlen(value)+2);
	    strcpy(a,value);
	    if(value[0] && value[strlen(value)-1]!='/')
	       strcat(a,"/");
	    value=a;
	 }
	 if(value==0 || value[0]==0)   // cannot add empty bookmark
	 {
	    eprintf(_("%s: cannot add empty bookmark\n"),args->a0());
	    return 0;
	 }
	 if(strchr(key,' ') || strchr(key,'\t'))
	 {
	    eprintf(_("%s: spaces in bookmark name are not allowed\n"),args->a0());
	    return 0;
	 }
	 lftp_bookmarks.Add(key,value);
   	 exit_code=0;
      }
   }
   else if(!strcmp(op,"delete"))
   {
      const char *key=args->getnext();
      if(key==0 || key[0]==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else if(lftp_bookmarks.Lookup(key)==0)
	 eprintf(_("%s: no such bookmark `%s'\n"),args->a0(),key);
      else
      {
	 lftp_bookmarks.Remove(key);
	 exit_code=0;
      }
   }
   else if(!strcmp(op,"edit"))
   {
      parent->PrependCmd("shell \"/bin/sh -c 'exec ${EDITOR:-vi} $HOME/.lftp/bookmarks'\"\n");
   }
   else if(!strcmp(op,"import"))
   {
      op=args->getnext();
      if(!op)
	 eprintf(_("%s: import type required (netscape,ncftp)\n"),args->a0());
      else
      {
	 const char *fmt="shell " PKGDATADIR "/import-%s\n";
	 char *cmd=string_alloca(strlen(op)+strlen(fmt)+1);
	 sprintf(cmd,fmt,op);
	 parent->PrependCmd(cmd);
	 exit_code=0;
      }
   }
   return 0;
}

CMD(echo)
{
   char *s=args->Combine(1);
   int len=strlen(s);
   if(args->count()>1 && !strcmp(args->getarg(1),"-n"))
   {
      if(len<=3)
      {
	 exit_code=0;
	 xfree(s);
	 return 0;
      }
      memmove(s,s+3,len-=3);
   }
   else
   {
      s[len++]='\n'; // replaces \0 char
   }
   Job *j=CopyJob::NewEcho(s,len,output,args->a0());
   xfree(s);
   output=0;
   return j;
}

CMD(suspend)
{
   kill(getpid(),SIGSTOP);
   return 0;
}

CMD(ftpcopy)
{
   Job *j=new FtpCopy(args,session);
   args=0;
   return j;
}

CMD(find)
{
   const char *path=".";
   if(args->count()>1)
      path=args->getarg(1);
   Job *j=new class FinderJob_List(Clone(),path,
      output?output:new FDStream(1,"<stdout>"));
   output=0;
   return j;
}

CMD(command)
{
   args->delarg(0);
   return parent->builtin_restart();
}

CMD(module)
{
   char *op=args->a0();
   if(args->count()<2)
   {
      eprintf(_("Usage: %s module [args...]\n"),args->a0());
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }
   void *map=module_load(args->getarg(1),args->count()-1,args->GetV()+1);
   if(map==0)
   {
      eprintf("%s\n",module_error_message());
      return 0;
   }
   exit_code=0;
   return 0;
}

CMD(lpwd)
{
   if(!parent->cwd)
   {
      eprintf("%s: cannot get current directory\n",args->a0());
      return 0;
   }
   printf("%s\n",parent->cwd);
   exit_code=0;
   return 0;
}

CMD(glob)
{
   return parent->builtin_glob();
}

CMD(chmod)
{
   if(args->count()<3)
   {
      eprintf(_("Usage: %s mode file...\n"),args->a0());
      return 0;
   }
   int m;
   if(sscanf(args->getarg(1),"%o",&m)!=1)
   {
      eprintf(_("%s: %s - not an octal number\n"),args->a0(),args->getarg(1));
      return 0;
   }
   args->delarg(1);
   Job *j=new ChmodJob(Clone(),m,args);
   args=0;
   return j;
}

CMD(queue)
{
   return parent->builtin_queue();
}

CMD(get1)
{
   static struct option get1_options[]=
   {
      {"ascii",no_argument,0,'a'},
      {"region",required_argument,0,'r'},
      {"continue",no_argument,0,'c'},
      {"output",required_argument,0,'o'},
      {0,0,0,0}
   };
   int opt;
   const char *src=0;
   const char *dst=0;
   bool cont=false;
   bool ascii=false;

   args->rewind();
   while((opt=args->getopt_long("arco:",get1_options,0))!=EOF)
   {
      switch(opt)
      {
      case 'c':
	 cont=true;
	 break;
      case 'a':
	 ascii=true;
	 break;
      case 'o':
	 dst=optarg;
	 break;
      case 'r':
	 // FIXME
	 break;
      case '?':
      usage:
	 eprintf(_("Usage: %s [OPTS] file\n"),args->a0());
	 return 0;
      }
   }
   src=args->getcurr();
   if(src==0)
      goto usage;
   if(args->getnext()!=0)
      goto usage;

   if(dst==0 || dst[0]==0)
   {
      dst=basename_ptr(src);
   }
   else
   {
      if(dst[strlen(dst)-1]=='/')
      {
	 const char *slash=strrchr(src,'/');
	 if(slash)
	    slash++;
	 else
	    slash=src;
	 char *dst1=string_alloca(strlen(dst)+strlen(slash)+1);
	 strcpy(dst1,dst);
	 strcat(dst1,slash);
	 dst=dst1;
      }
   }

   ParsedURL dst_url(dst,true);

   if(dst_url.proto==0)
   {
      dst=expand_home_relative(dst);
      // check if dst is a directory.
      struct stat st;
      if(stat(dst,&st)!=-1)
      {
	 if(S_ISDIR(st.st_mode))
	 {
	    const char *slash=strrchr(src,'/');
	    if(slash)
	       slash++;
	    else
	       slash=src;
	    char *dst1=string_alloca(strlen(dst)+strlen(slash)+2);
	    strcpy(dst1,dst);
	    strcat(dst1,"/");
	    strcat(dst1,slash);
	    dst=dst1;
	 }
      }
   }

   FileCopyPeer *src_peer=0;
   FileCopyPeer *dst_peer=0;

   src_peer=FileCopyPeerFA::New(Clone(),src,FA::RETRIEVE,true);

   if(dst_url.proto==0)
      dst_peer=FileCopyPeerFDStream::NewPut(dst,cont);
   else
      dst_peer=new FileCopyPeerFA(&dst_url,FA::STORE);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,cont);

   if(ascii)
      c->Ascii();

   return new CopyJob(c,src,args->a0());
}
