/*
 * lftp and utils
 *
 * Copyright (c) 1996-1998 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include "CmdExec.h"
#include "GetJob.h"
#include "PutJob.h"
#include "CatJob.h"
#include "LsJob.h"
#include "LsCache.h"
#include "mgetJob.h"
#include "mputJob.h"
#include "mkdirJob.h"
#include "rmJob.h"
#include "mrmJob.h"
#include "SysCmdJob.h"
#include "QuoteJob.h"
#include "MirrorJob.h"
#include "mvJob.h"
#include "pgetJob.h"
#include "FtpCopy.h"
#include "SleepJob.h"
#include "FindJob.h"

#include "misc.h"
#include "alias.h"
#include "netrc.h"
#include "url.h"
#include "GetPass.h"
#include "SignalHook.h"
#include "ProtoList.h"
#include "FileFeeder.h"
#include "xalloca.h"
#include "bookmark.h"
#include "log.h"

#include "confpaths.h"

#define MINUTE (60)
#define HOUR   (60*MINUTE)
#define DAY    (24*HOUR)

Bookmark lftp_bookmarks;
History	 cwd_history;

const struct CmdExec::cmd_rec CmdExec::cmd_table[]=
{
   {"!",       &CmdExec::do_shell,  N_("!<shell_command>"),
	 N_("Launch shell or shell command\n")},
   {"(",       &CmdExec::do_subsh,  N_("(commands)"),
	 N_("Group commands together to be executed as one command\n"
	 "You can launch such a group in background\n")},
   {"?",       &CmdExec::do_help,   0,"help"},
   {"alias",   &CmdExec::do_alias,  N_("alias [<name> [<value>]]"),
	 N_("Define or undefine alias <name>. If <value> omitted,\n"
	 "the alias is undefined, else is takes the value <value>.\n"
         "If no argument is given the current aliases are listed.\n")},
   {"anon",    &CmdExec::do_anon,   "anon",
	 N_("anon - login anonymously (by default)\n")},
   {"at",      &CmdExec::do_at},
   {"bookmark",&CmdExec::do_bookmark,N_("bookmark [SUBCMD]"),
	 N_("bookmark command controls bookmarks\n\n"
	 "The following subcommands are recognized:\n"
	 "  add <name> [<loc>] - add current place or given location to bookmarks\n"
	 "                       and bind to given name\n"
	 "  del <name>         - remove bookmark with the name\n"
	 "  edit               - start editor on bookmarks file\n"
	 "  import <type>      - import foreign bookmarks\n"
	 "  list               - list bookmarks (default)\n")},
   {"bye",     &CmdExec::do_exit,   0,"exit"},
   {"cache",   &CmdExec::do_cache,  N_("cache [SUBCMD]"),
	 N_("cache command controls local memory cache\n\n"
	 "The following subcommands are recognized:\n"
	 "  stat        - print cache status (default)\n"
	 "  on|off      - turn on/off caching\n"
	 "  flush       - flush cache\n"
	 "  size <lim>  - set memory limit, -1 means unlimited\n"
	 "  expire <Nx> - set cache expiration time to N seconds (x=s)\n"
	 "                minutes (x=m) hours (x=h) or days (x=d)\n")},
   {"cat",     &CmdExec::do_cat,    N_("cat [-u] <files>"),
	 N_("cat - output remote files to stdout\n"
	 " -u  try to recognize URLs\n")},
   {"cd",      &CmdExec::do_cd,     N_("cd <rdir>"),
	 N_("Change current remote directory to <rdir>. The previous remote directory\n"
	 "is stored as `-'. You can do `cd -' to change the directory back.\n"
	 "The previous directory for each site is also stored on disk, so you can\n"
	 "do `open site; cd -' even after lftp restart.\n")},
   {"close",   &CmdExec::do_close,   "close [-a]",
	 N_("Close idle connections. By default only with current server.\n"
	 " -a  close idle connections with all servers\n")},
   {"connect", &CmdExec::do_open,   0,"open"},
   {"command", &CmdExec::do_command},
   {"debug",   &CmdExec::do_debug,  N_("debug [<level>|off] [-o <file>]"),
	 N_("Set debug level to given value or turn debug off completely.\n"
	 " -o <file>  redirect debug output to the file.\n")},
   {"echo",    &CmdExec::do_echo,   0},
   {"exit",    &CmdExec::do_exit,   N_("exit [<code>]"),
	 N_("exit - exit from lftp or move to background if jobs are active\n\n"
	 "If no jobs active, the code is passed to operating system as lftp\n"
	 "termination status. If omitted, exit code of last command is used.\n")},
   {"fg",      &CmdExec::do_wait,   0,"wait"},
   {"find",    &CmdExec::do_find},
   {"ftpcopy", &CmdExec::do_ftpcopy},
   {"get",     &CmdExec::do_get,    N_("get [OPTS] <rfile> [-o <lfile>]"),
	 N_("Retrieve remote file <rfile> and store it to local file <lfile>.\n"
	 " -o <lfile> specifies local file name (default - basename of rfile)\n"
	 " -c  continue, reget\n"
	 " -e  delete remote files after successful transfer\n"
	 " -u  try to recognize URLs\n")},
   {"help",    &CmdExec::do_help,   N_("help [<cmd>]"),
	 N_("Print help for command <cmd>, or list of available commands\n")},
   {"jobs",    &CmdExec::do_jobs,   "jobs [-v]",
	 N_("List running jobs. -v means verbose, several -v can be specified.\n")},
   {"kill",    &CmdExec::do_kill,   N_("kill all|<job_no>"),
	 N_("Delete specified job with <job_no> or all jobs\n")},
   {"lcd",     &CmdExec::do_lcd,    N_("lcd <ldir>"),
	 N_("Change current local directory to <ldir>. The previous local directory\n"
	 "is stored as `-'. You can do `lcd -' to change the directory back.\n")},
   {"lftp",    &CmdExec::do_lftp,   N_("lftp [OPTS] <site>"),
	 N_("`lftp' is the first command executed by lftp after rc files\n"
	 " -f <file>           execute commands from the file and exit\n"
	 " -c <cmd>            execute the commands and exit\n"
	 "Other options are the same as in `open' command\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"login",   &CmdExec::do_user,   0,"user"},
   {"ls",      &CmdExec::do_ls,	    N_("ls [<args>]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	 "or via pipe to external command.\n"
	 "By default, ls output is cached, to see new listing use `rels' or\n"
	 "`cache flush'.\n")},
   {"mget",    &CmdExec::do_mget,   N_("mget [-c] [-d] [-e] <files>"),
	 N_("Gets selected files with expanded wildcards\n"
	 " -c  continue, reget\n"
	 " -d  create directories the same as in file names and get the\n"
	 "     files into them instead of current directory\n"
	 " -e  delete remote files after successful transfer\n")},
   {"mirror",  &CmdExec::do_mirror, N_("mirror [OPTS] [remote [local]]"),
	 N_("\nMirror specified remote directory to local directory\n\n"
	 " -c, --continue         continue a mirror job if possible\n"
	 " -e, --delete           delete files not present at remote site\n"
	 " -s, --allow-suid       set suid/sgid bits according to remote site\n"
	 " -n, --only-newer       download only newer files (-c won't work)\n"
	 " -r, --no-recursion     don't go to subdirectories\n"
	 " -p, --no-perms         don't set file permissions\n"
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
   {"mkdir",   &CmdExec::do_mkdir,  N_("mkdir [-p] <dirs>"),
	 N_("Make remote directories\n"
	 " -p  make all levels of path\n")},
   {"more",    &CmdExec::do_cat,    N_("more [-u] <files>"),
	 N_("Same as `cat <files> | more'. if PAGER is set, it is used as filter\n"
	 " -u  try to recognize URLs\n")},
   {"mput",    &CmdExec::do_mput,   N_("mput [-c] [-d] <files>"),
	 N_("Upload files with wildcard expansion\n"
	 " -c  continue, reput\n"
	 " -d  create directories the same as in file names and put the\n"
	 "     files into them instead of current directory\n")},
   {"mrm",     &CmdExec::do_mrm,    N_("mrm <files>"),
	 N_("Removes specified files with wildcard expansion\n")},
   {"mv",      &CmdExec::do_mv,	    N_("mv <file1> <file2>"),
	 N_("Rename <file1> to <file2>\n")},
   {"nlist",   &CmdExec::do_ls,	    N_("nlist [<args>]"),
	 N_("List remote file names\n")},
   {"open",    &CmdExec::do_open,   N_("open [OPTS] <site>"),
	 N_("Select a server, URL or bookmark\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"pget",    &CmdExec::do_get,    N_("pget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Gets the specified file using several connections. This can speed up transfer,\n"
	 "but loads the net heavily impacting other users. Use only if you really\n"
	 "have to transfer the file ASAP, or some other user may go mad :)\n"
	 "\nOptions:\n"
	 " -n <maxconn>  set maximum number of connections (default 5)\n"
	 " -u            try to recognize URLs\n")},
   {"put",     &CmdExec::do_put,    N_("put [-c] <lfile> [-o <rfile>]"),
	 N_("Upload <lfile> with remote name <rfile>.\n"
	 " -o <rfile> specifies remote file name (default - basename of lfile)\n"
	 " -c  continue, reput\n"
	 "     it requires permission to overwrite remote files\n")},
   {"pwd",     &CmdExec::do_pwd,    "pwd",
	 N_("Print current remote directory\n")},
   {"quit",    &CmdExec::do_exit,   0,"exit"},
   {"quote",   &CmdExec::do_quote,  N_("quote <cmd>"),
	 N_("Send the command uninterpreted. Use with caution - it can lead to\n"
	 "unknown remote state and thus will cause reconnect. You cannot\n"
	 "be sure that any change of remote state because of quoted command\n"
	 "is solid - it can be reset by reconnect at any time.\n")},
   {"reget",   &CmdExec::do_get,    N_("reget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Same as `get -c'\n")},
   {"rels",    &CmdExec::do_ls,	    N_("rels [<args>]"),
	 N_("Same as `ls', but don't look in cache\n")},
   {"renlist", &CmdExec::do_ls,	    N_("renlist [<args>]"),
	 N_("Same as `nlist', but don't look in cache\n")},
   {"reput",   &CmdExec::do_put,    N_("reput <lfile> [-o <rfile>]"),
	 N_("Same as `put -c'\n")},
   {"rm",      &CmdExec::do_rm,	    N_("rm [-r] <files>"),
	 N_("Remove remote files\n"
	    " -r  recursive directory remove, be careful\n")},
   {"rmdir",   &CmdExec::do_rm,	    N_("rmdir <dirs>"),
	 N_("Remove remote directories\n")},
   {"scache",  &CmdExec::do_scache, N_("scache [<session_no>]"),
	 N_("List cached sessions or switch to specified session number\n")},
   {"set",     &CmdExec::do_set,    N_("set [<var> [<val>]]"),
	 N_("Set variable to given value. If the value is omitted, unset the variable.\n"
         "If called with no variable, currently set variables are listed.\n")},
   {"shell",   &CmdExec::do_shell,  0,"!"},
   {"site",    &CmdExec::do_site,   N_("site <site_cmd>"),
	 N_("Execute site command <site_cmd> and output the result\n"
	 "You can redirect its output\n")},
   {"sleep",   &CmdExec::do_sleep},
   {"source",  &CmdExec::do_source, N_("source <file>"),
	 N_("Execute commands recorded in file <file>\n")},
   {"suspend", &CmdExec::do_suspend},
   {"user",    &CmdExec::do_user,   N_("user <user> [<pass>]"),
	 N_("Use specified info for remote login\n")},
   {"version", &CmdExec::do_ver,    "version",
	 N_("Shows lftp version\n")},
   {"wait",    &CmdExec::do_wait,   N_("wait <jobno>"),
	 N_("Wait for specified job to terminate.\n")},
   {"zcat",    &CmdExec::do_cat,    N_("zcat [-u] <files>"),
	 N_("Same as cat, but filter each file through zcat\n")},
   {"zmore",   &CmdExec::do_cat,    N_("zmore [-u] <files>"),
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

CMD(lcd)
{
   if(args->count()<2)
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

CMD(ls)
{
   int mode=Ftp::LONG_LIST;
   if(strstr(args->a0(),"nlist"))
      mode=Ftp::LIST;
   if(mode==Ftp::LONG_LIST && args->count()==1)
      args->Append(var_ls);
   LsJob *j=new LsJob(Clone(),output,args->Combine(1),mode);
   output=0;
   if(!strncmp(args->a0(),"re",2))
      j->NoCache();
   return j;
}

CMD(cat)
{
   char *op=args->a0();
   ArgV *n_args=new ArgV(op);
   bool use_urls=false;
   int opt;

   while((opt=args->getopt("+u"))!=EOF)
   {
      switch(opt)
      {
      case('u'):
	 use_urls=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   char *a;
   while((a=args->getnext())!=0)
      n_args->Append(a);
   if(n_args->count()<=1)
   {
      eprintf(_("Usage: %s files...\n"),args->a0());
      goto err;
   }
   CatJob *j=new CatJob(Clone(),output,n_args);
   output=0;
   if(use_urls)
      j->UseURLs();
   return j;
}

CMD(get)
{
   int opt;
   bool use_urls=false;
   bool cont=false;
   char *opts="+ceu";
   char *op=args->a0();
   ArgV	 *get_args=new ArgV(op);
   int n_conn=0;
   bool del=false;

   args->rewind();
   if(!strncmp(op,"re",2))
   {
      cont=true;
      opts="+eu";
   }
   if(!strcmp(op,"pget"))
   {
      opts="+n:u";
      n_conn=-1;
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
      case('u'):
	 use_urls=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   const char *a=args->getnext();
   const char *a1;
   if(a==0)
   {
      // xgettext:c-format
      eprintf(_("File name missed. "));
      goto err;
   }
   while(a)
   {
      get_args->Append(a);
      a1=basename_ptr(a);
      a=args->getnext();
      if(a && !strcmp(a,"-o"))
      {
	 a=args->getnext();
	 if(a)
	 {
	    a=expand_home_relative(a);
	    struct stat st;
	    int res=stat(a,&st);
	    if(res!=-1 && S_ISDIR(st.st_mode))
	    {
	       char *comb=(char*)alloca(strlen(a)+strlen(a1)+2);
	       sprintf(comb,"%s/%s",a,a1);
	       get_args->Append(comb);
	    }
	    else
	    {
	       get_args->Append(a);
	    }
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
      if(use_urls)
	 j->UseURLs();
      return j;
   }
   else
   {
      pgetJob *j=new pgetJob(Clone(),get_args);
      if(n_conn!=-1)
	 j->SetMaxConn(n_conn);
      if(use_urls)
	 j->UseURLs();
      return j;
   }
}

CMD(put)
{
   int opt;
   bool cont=false;
   char *opts="+c";
   char *op=args->a0();
   ArgV	 *get_args=new ArgV(op);

   args->rewind();
   if(!strncmp(op,"re",2))
   {
      cont=true;
      opts="+";
   }
   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   const char *a=args->getnext();
   const char *a1;
   if(a==0)
   {
      // xgettext:c-format
      eprintf(_("File name missed. "));
      goto err;
   }
   while(a)
   {
      a=expand_home_relative(a);
      get_args->Append(a);
      a1=basename_ptr(a);
      a=args->getnext();
      if(a && !strcmp(a,"-o"))
      {
	 a=args->getnext();
	 if(a)
	    get_args->Append(a);
	 else
	    get_args->Append(a1);
	 a=args->getnext();
      }
      else
	 get_args->Append(a1);
   }

   PutJob *j=new PutJob(Clone(),get_args,cont);
   return j;
}

CMD(mget)
{
   Job *j=new mgetJob(Clone(),args);
   args=0;
   return j;
}

CMD(mput)
{
   Job *j=new mputJob(Clone(),args);
   args=0;
   return j;
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
   Job *j=new mrmJob(Clone(),args);
   args=0;
   return j;
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
      Job *j=new FindJob_Cmd(Clone(),args,FindJob_Cmd::RM);
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
   SetCmdFeeder(new FileFeeder(new FileStream(args->getarg(1),O_RDONLY)));
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
   Job::ListJobs(v);
   exit_code=0;
   return 0;
}

CMD(cd)
{
   if(args->count()!=2)
   {
      // xgettext:c-format
      eprintf(_("Usage: cd remote-dir\n"));
      return 0;
   }

   const char *dir=args->getarg(1);

   char c;
   if(sscanf(dir,"%*[a-z]://%*[^/]%c",&c)==1)
      return do_open();

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

   if (!verify_path)
   {
      session->Chdir(dir,false);
      return 0;
   }
   session->Chdir(dir);
   builtin=BUILTIN_CD;
   return this;
}

CMD(pwd)
{
   char *url=xstrdup(session->GetConnectURL());
   int len=strlen(url);
   url[len++]='\n';  // replaces \0
   Job *j=new CatJob(output,url,len);
   output=0;
   return j;
}

CMD(exit)
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
	 if(new_dlevel>9)
	    new_dlevel=9;
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


   if(interactive)
   {
      if(enabled)
	 printf(_("debug level is %d, output goes to %s\n"),new_dlevel,
		     debug_file_name?debug_file_name:"<stderr>");
      else
	 printf(_("debug is off\n"));
   }
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
   if(args->count()==2)
      pass=GetPass(_("Password: "));
   else
      pass=args->getarg(2);
   if(pass)
      session->Login(args->getarg(1),pass);
   exit_code=0;
   return 0;
}
CMD(anon)
{
   session->AnonymousLogin();
   exit_code=0;
   return 0;
}

CmdFeeder *lftp_feeder=0;
static struct option lftp_options[]=
{
   {"help",no_argument,0,'h'},
   {"version",no_argument,0,'v'},
   {0}
};

CMD(lftp)
{
   int c;
   char *cmd=0;

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
	 cmd=(char*)alloca(20+2*strlen(optarg));
	 strcpy(cmd,"source \"");
	 unquote(cmd+strlen(cmd),optarg);
	 strcat(cmd,"\";");
	 break;
      case('c'):
	 cmd=(char*)alloca(4+strlen(optarg));
	 sprintf(cmd,"%s\n\n",optarg);
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
      FeedCmd("||exit\n");   // if the command fails, quit
   }

   if(!cmd)
   {
      /* if no lftp-specific options were found, call open */
      return do_open();
   }
   return 0;
}

CMD(open)
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

   args->rewind();
   while((c=args->getopt("u:p:e:d"))!=EOF)
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
         break;
      case('d'):
	 debug=true;
	 break;
      case('e'):
	 cmd_to_exec=optarg;
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

   if(host && (bm=lftp_bookmarks.Lookup(host))!=0)
   {
      char *cmd=(char*)alloca(5+strlen(bm)+1+1);
      sprintf(cmd,"open %s\n",bm);
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
	       new_session=Protocol::NewSession(p);
	       if(!new_session)
	       {
		  eprintf(N_("%s: %s - not supported protocol\n"),
			   args->getarg(0),p);
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
	       pass=uc.pass;
	    host=uc.host;
	    if(uc.port!=0 && port==0)
	       port=uc.port;
	    if(uc.path && !path)
	       path=uc.path;
	 }

	 if(!strcmp(session->GetProto(),"ftp"))
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
      if(user)
      {
	 if(!pass)
	    pass=GetPass(_("Password: "));
	 if(!pass)
	    eprintf(_("%s: GetPass() failed -- assume anonymous login\n"),
	       args->getarg(0));
	 else
	    session->Login(user,pass);
      }
      if(host)
      {
	 int port_num=0;
	 if(port)
	 {
	    if(!isdigit(port[0]))
	    {
	       struct servent *serv=getservbyname(port,"tcp");
	       if(serv==NULL)
	       {
		  eprintf(_("%s: %s - no such tcp service\n"),op,port);
		  return 0;
	       }
	       port_num=serv->s_port;
	    }
	    else
	       port_num=atoi(port);
	 }
	 session->Connect(host,port_num);
	 if(verify_host)
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
      char *s=(char*)alloca(strlen(path)*4+40);
      strcpy(s,"&& cd \"");
      unquote(s+strlen(s),path);
      strcat(s,"\"\n");
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

   if(host && !bm && verify_host)
      return this;

   exit_code=0;
   return 0;
}

CMD(kill)
{
   if(args->count()<2)
   {
      eprintf(_("Usage: %s <jobno> ... | all\n"),args->getarg(0));
      return 0;
   }
   if(!strcmp(args->getarg(1),"all"))
   {
      Job::KillAll();
      exit_code=0;
      return 0;
   }
   args->rewind();
   for(;;)
   {
      char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!isdigit(arg[0]))
      {
	 eprintf(_("%s: %s - not a number\n"),args->getarg(0),arg);
      	 continue;
      }
      int n=atoi(arg);
      if(Job::Running(n))
	 Job::Kill(n);
      else
	 eprintf(_("%s: %d - no such job\n"),args->getarg(0),n);
   }
   exit_code=0;
   return 0;
}

CMD(set)
{
   if(args->count()<2)
   {
/* can't happen
      if(args->count()!=1)
      {
	 eprintf(_("Usage: %s <variable> [<value>]\n"),args->getarg(0));
	 return 0;
      }
*/
      char *s=ResMgr::Format();
      Job *j=new CatJob(output,s,strlen(s));
      output=0;
      return j;
   }

   char *a=args->getarg(1);
   char *sl=strchr(a,'/');
   char *closure=0;
   if(sl)
   {
      *sl=0;
      closure=sl+1;
   }

   char *val=(args->count()<=2?0:args->Combine(2));
   const char *msg=ResMgr::Set(a,closure,val);
   xfree(val);

   if(msg)
   {
      eprintf(_("%s: %s. Use `set' to look at all variables.\n"),a,msg);
      return 0;
   }
   exit_code=0;
   return 0;
}

CMD(alias)
{
   if(args->count()<2)
   {
      char *list=Alias::Format();
      Job *j=new CatJob(output,list,strlen(list));
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
   if(args->count()!=2)
   {
      eprintf(_("Usage: %s <jobno>\n"),op);
      return 0;
   }
   args->rewind();
   char *jn=args->getnext();
   if(!isdigit(jn[0]))
   {
      eprintf(_("%s: %s - not a number\n"),op,jn);
      return 0;
   }
   int n=atoi(jn);
   Job *j=FindJob(n);
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

CMD(quote)
{
   if(args->count()<=1)
   {
      eprintf(_("Usage: %s <raw_cmd>\n"),args->a0());
      return 0;
   }
   Job *j=new QuoteJob(Clone(),args->a0(),args->Combine(1),
      output?output:new FDStream(1,"<stdout>"));
   output=0;
   return j;
}

CMD(site)
{
   if(args->count()<=1)
   {
      // xgettext:c-format
      eprintf(_("Usage: site <site_cmd>\n"));
      return 0;
   }
   char *cmd=args->Combine(1);
   cmd=(char*)xrealloc(cmd,strlen(cmd)+6);
   memmove(cmd+5,cmd,strlen(cmd)+1);
   memcpy(cmd,"SITE ",5);
   Job *j=new QuoteJob(Clone(),args->a0(),cmd,
      output?output:new FDStream(1,"<stdout>"));
   output=0;
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

time_t decode_delay(const char *s)
{
   long prec;
   char ch;
   int n=sscanf(s,"%lu%c",&prec,&ch);
   if(n<1)
      return -1;
   if(n==1)
      ch='s';
   else if(ch=='m')
      prec*=MINUTE;
   else if(ch=='h')
      prec*=HOUR;
   else if(ch=='d')
      prec*=DAY;
   else if(ch!='s')
      return -1;
   return prec;
}

CMD(mirror)
{
   static struct option mirror_opts[]=
   {
      {"delete",no_argument,0,'e'},
      {"allow-suid",no_argument,0,'s'},
      {"include",required_argument,0,'i'},
      {"exclude",required_argument,0,'x'},
      {"time-prec",required_argument,0,'t'},
      {"only-newer",no_argument,0,'n'},
      {"no-recursion",no_argument,0,'r'},
      {"no-perms",no_argument,0,'p'},
      {"continue",no_argument,0,'c'},
      {"reverse",no_argument,0,'R'},
      {"verbose",optional_argument,0,'v'},
      {"newer-than",required_argument,0,'N'},
      {"dereference",no_argument,0,'L'},
      {0}
   };

   char cwd[2*1024];
   const char *rcwd;
   int opt;
   int	 flags=0;

   static char *include=0;
   static int include_alloc=0;
   static char *exclude=0;
   static int exclude_alloc=0;
#define APPEND_STRING(s,a,s1) \
   {			                  \
      int len,len1=strlen(s1);            \
      if(!s)		                  \
      {			                  \
	 s=(char*)xmalloc(a = len1+1);    \
      	 strcpy(s,s1);	                  \
      }			                  \
      else				  \
      {					  \
	 len=strlen(s);		       	  \
	 if(a < len+1+len1+1)		  \
	    s=(char*)xrealloc(s, a = len+1+len1+1); \
	 if(s[0]) strcat(s,"|");	  \
	 strcat(s,s1);			  \
      }					  \
   } /* END OF APPEND_STRING */

   if(include)
      include[0]=0;
   if(exclude)
      exclude[0]=0;

   time_t prec=12*HOUR;
   bool	 create_remote_dir=false;
   int	 verbose=0;
   const char *newer_than=0;

   args->rewind();
   while((opt=args->getopt_long("esi:x:t:nrpcRvN:L",mirror_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 flags|=MirrorJob::DELETE;
	 break;
      case('s'):
	 flags|=MirrorJob::ALLOW_SUID;
	 break;
      case('r'):
	 flags|=MirrorJob::NO_RECURSION;
	 break;
      case('n'):
	 flags|=MirrorJob::ONLY_NEWER;
	 break;
      case('p'):
	 flags|=MirrorJob::NO_PERMS;
	 break;
      case('c'):
	 flags|=MirrorJob::CONTINUE;
	 break;
      case('t'):
	 prec=decode_delay(optarg);
	 if(prec==(time_t)-1)
	 {
	    eprintf(_("%s: %s - invalid time precision\n"),args->a0(),optarg);
	    eprintf(_("Try `help %s' for more information.\n"),args->a0());
	    return 0;
	 }
	 break;
      case('x'):
	 APPEND_STRING(exclude,exclude_alloc,optarg);
	 break;
      case('i'):
	 APPEND_STRING(include,include_alloc,optarg);
	 break;
      case('R'):
	 flags|=MirrorJob::REVERSE;
	 break;
      case('L'):
	 flags|=MirrorJob::RETR_SYMLINKS;
	 break;
      case('v'):
	 if(optarg)
	    verbose=atoi(optarg);
	 else
	    verbose++;
	 if(verbose>1)
	    flags|=MirrorJob::REPORT_NOT_DELETED;
	 break;
      case('N'):
	 newer_than=optarg;
	 break;
      case('?'):
	 return 0;
      }
   }

   if(getcwd(cwd,sizeof(cwd)/2)==0)
   {
      perror("getcwd()");
      return 0;
   }

   args->back();
   if(flags&MirrorJob::REVERSE)
   {
      char *arg=args->getnext();
      if(!arg)
	 rcwd=".";
      else
      {
	 if(arg[0]=='/')
	    strcpy(cwd,arg);
	 else
	 {
	    strcat(cwd,"/");
	    strcat(cwd,arg);
	 }
	 rcwd=args->getnext();
	 if(!rcwd)
	 {
	    rcwd=basename_ptr(cwd);
	    if(rcwd[0]=='/')
	       rcwd=".";
	    else
	       create_remote_dir=true;
	 }
	 else
	    create_remote_dir=true;
      }
   }
   else	/* !REVERSE (normal) */
   {
      rcwd=args->getnext();
      if(!rcwd)
	 rcwd=".";
      else
      {
	 strcat(cwd,"/");
	 char *arg=args->getnext();
	 if(arg)
	 {
	    if(arg[0]=='/')
	       strcpy(cwd,arg);
	    else
	       strcat(cwd,arg);
	    if(create_directories(arg)==-1)
	       return 0;
	 }
	 else
	 {
	    int len=strlen(cwd);
	    const char *base=basename_ptr(rcwd);
	    if(base[0]!='/')
	    {
	       strcat(cwd,base);
	       if(create_directories(cwd+len)==-1)
		  return 0;
	    }
	 }
      }
   }
   MirrorJob *j=new MirrorJob(Clone(),cwd,rcwd);
   j->SetFlags(flags,1);
   j->SetVerbose(verbose);
   if(create_remote_dir)
      j->CreateRemoteDir();

   const char *err;
   const char *err_tag;

   err_tag="include";
   if(include && include[0] && (err=j->SetInclude(include)))
      goto err_out;
   err_tag="exclude";
   if(exclude && exclude[0] && (err=j->SetExclude(exclude)))
      goto err_out;

   j->SetPrec((time_t)prec);
   if(newer_than)
      j->SetNewerThan(newer_than);
   return j;

err_out:
   eprintf("%s: %s: %s\n",args->a0(),err_tag,err);
   delete j;
   return 0;
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

static char *const cache_subcmd[]={
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
      time_t exp=decode_delay(op);
      if(exp==-1)
      {
	 eprintf(_("%s: Invalid expire period (use Ns - in sec, Nm - in min, Nh - in hours)\n"),args->a0());
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
      Reuse(session);
      session=new_session;
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
      if(!strchr(c->long_desc,' '))
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
	 print_cmd_help(cmd);
      }
      return 0;
   }

   int i=0;
   const char *c1;
   while(cmd_table[i].name)
   {
      while(cmd_table[i].name && !cmd_table[i].short_desc)
	 i++;
      if(cmd_table[i].name)
      {
	 c1=cmd_table[i].short_desc;
	 i++;
	 while(cmd_table[i].name && !cmd_table[i].short_desc)
	    i++;
	 if(cmd_table[i].name)
	 {
	    printf("\t%-35s %s\n",_(c1),_(cmd_table[i].short_desc));
	    i++;
	 }
	 else
	    printf("\t%s\n",_(c1));
      }
   }
   return 0;
}

CMD(ver)
{
   printf(
      _("Lftp | Version %s | Copyright (c) 1996-98 Alexander V. Lukyanov\n"),VERSION);
   printf(
      _("This is free software with ABSOLUTELY NO WARRANTY. See COPYING for details.\n"));
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
   session->Cleanup(all);
   exit_code=0;
   return 0;
}

static const char * const bookmark_subcmd[]=
   {"add","delete","list","edit","import",0};

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
      Job *j=new CatJob(output,list,strlen(list));
      output=0;
      return j;
   }
   else if(!strcmp(op,"add"))
   {
      const char *key=args->getnext();
      if(key==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else
      {
	 const char *value=args->getnext();
	 int flags=0;
	 if(save_passwords)
	    flags|=session->WITH_PASSWORD;
	 if(value==0)
	    value=session->GetConnectURL(flags);
	 lftp_bookmarks.Add(key,value);
   	 exit_code=0;
      }
   }
   else if(!strcmp(op,"delete"))
   {
      const char *key=args->getnext();
      if(key==0)
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
      PrependCmd("shell \"/bin/sh -c \\\"exec ${EDITOR:-vi} $HOME/.lftp/bookmarks\\\"\"\n");
   }
   else if(!strcmp(op,"import"))
   {
      op=args->getnext();
      if(!op)
	 eprintf(_("%s: import type required (netscape,ncftp)\n"),args->a0());
      else
      {
	 const char *fmt="shell " PKGDATADIR "/import-%s\n";
	 char *cmd=(char*)alloca(strlen(op)+strlen(fmt)+1);
	 sprintf(cmd,fmt,op);
	 PrependCmd(cmd);
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
   Job *j=new CatJob(output,s,len);
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

CMD(sleep)
{
   char *op=args->a0();
   if(args->count()!=2)
   {
      eprintf(_("%s: argument required. "),op);
   err:
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }
   char *t=args->getarg(1);
   time_t delay=decode_delay(t);
   if(delay==(time_t)-1)
   {
      eprintf(_("%s: invalid delay. "),op);
      goto err;
   }
   return new SleepJob(time(0)+delay);
}

extern "C" {
#include "getdate.h"
}
CMD(at)
{
   int count=1;
   int cmd_start=0;
   int date_len=0;
   for(;;)
   {
      char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!strcmp(arg,"--"))
      {
	 cmd_start=count+1;
	 break;
      }
      date_len+=strlen(arg)+1;
      count++;
   }

#if 0
   char **av=(char**)xmemdup(args->GetV(),(count+1)*sizeof(char**));
   av[count]=0;
   time_t when=parsetime(count-1,av+1);
   xfree(av);
#endif
   char *date=args->Combine(1);
   date[date_len]=0;
   time_t now=time(0);
   time_t when=get_date(date,&now);
   xfree(date);

   if(when==0 || when==(time_t)-1)
      return 0;

   char *cmd = cmd_start ? args->Combine(cmd_start) : 0;
   FileAccess *s = cmd ? Clone() : 0;
   return new SleepJob(when, s, cmd);
}

CMD(find)
{
   char *path=".";
   if(args->count()>1)
      path=args->getarg(1);
   Job *j=new class FindJob_List(Clone(),path,
      output?output:new FDStream(1,"<stdout>"));
   output=0;
   return j;
}

CMD(command)
{
   args->delarg(0);
   builtin=BUILTIN_EXEC_RESTART;
   return this;
}
