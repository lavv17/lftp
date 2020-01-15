/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2020 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "modconfig.h"

#include "trio.h"
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <pwd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <assert.h>
#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif
#include <mbswidth.h>
#include <human.h>

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
#include "SleepJob.h"
#include "FindJob.h"
#include "FindJobDu.h"
#include "ChmodJob.h"
#include "CopyJob.h"
#include "OutputJob.h"
#include "echoJob.h"
#include "EditJob.h"
#include "mmvJob.h"

#include "misc.h"
#include "alias.h"
#include "netrc.h"
#include "url.h"
#include "GetPass.h"
#include "SignalHook.h"
#include "FileFeeder.h"
#include "bookmark.h"
#include "log.h"
#include "module.h"
#include "FileCopy.h"
#include "DummyProto.h"
#include "QueueFeeder.h"
#include "lftp_rl.h"
#include "FileSetOutput.h"
#include "PatternSet.h"
#include "LocalDir.h"
#include "ConnectionSlot.h"

#include "configmake.h"

History	 cwd_history;

CMD(alias); CMD(anon); CMD(at); CMD(bookmark); CMD(cache); CMD(cat);
CMD(cd); CMD(chmod); CMD(close); CMD(cls); CMD(command); CMD(debug);
CMD(du); CMD(echo); CMD(edit); CMD(eval); CMD(exit); CMD(find); CMD(get);
CMD(get1); CMD(glob); CMD(help); CMD(jobs); CMD(kill); CMD(lcd); CMD(lftp);
CMD(ln); CMD(local); CMD(lpwd); CMD(ls); CMD(mirror); CMD(mkdir);
CMD(module); CMD(mrm); CMD(mv); CMD(open); CMD(pwd); CMD(queue);
CMD(repeat); CMD(rm); CMD(scache); CMD(set); CMD(shell); CMD(sleep);
CMD(slot); CMD(source); CMD(subsh); CMD(suspend); CMD(tasks); CMD(torrent);
CMD(user); CMD(ver); CMD(wait); CMD(empty); CMD(notempty); CMD(true);
CMD(false); CMD(mmv);

#define HELP_IN_MODULE "m"
#define ALIAS_FOR(cmd) cmd_##cmd,0,#cmd
#define ALIAS_FOR2(a,cmd) cmd_##cmd,0,a

#ifdef MODULE_CMD_MIRROR
# define cmd_mirror 0
#endif
#ifdef MODULE_CMD_SLEEP
# define cmd_sleep  0
# define cmd_at     0
# define cmd_repeat 0
#endif
#ifdef MODULE_CMD_TORRENT
# define cmd_torrent 0
#endif

enum { DEFAULT_DEBUG_LEVEL=9 };

const struct CmdExec::cmd_rec CmdExec::static_cmd_table[]=
{
   {"!",       cmd_shell,  N_("!<shell-command>"),
	 N_("Launch shell or shell command\n")},
   {"(",       cmd_subsh,  N_("(commands)"),
	 N_("Group commands together to be executed as one command\n"
	 "You can launch such a group in background\n")},
   {"?", ALIAS_FOR(help)},
   {"alias",   cmd_alias,  N_("alias [<name> [<value>]]"),
	 N_("Define or undefine alias <name>. If <value> omitted,\n"
	 "the alias is undefined, else is takes the value <value>.\n"
         "If no argument is given the current aliases are listed.\n")},
   {"anon",    cmd_anon,   0,
	 N_("anon - login anonymously (by default)\n")},
   {"at",      cmd_at, 0, HELP_IN_MODULE},
   {"bookmark",cmd_bookmark,N_("bookmark [SUBCMD]"),
	 N_("bookmark command controls bookmarks\n\n"
	 "The following subcommands are recognized:\n"
	 "  add <name> [<loc>] - add current place or given location to bookmarks\n"
	 "                       and bind to given name\n"
	 "  del <name>         - remove bookmark with the name\n"
	 "  edit               - start editor on bookmarks file\n"
	 "  import <type>      - import foreign bookmarks\n"
	 "  list               - list bookmarks (default)\n")},
   {"bye", ALIAS_FOR(exit)},
   {"cache",   cmd_cache,  N_("cache [SUBCMD]"),
	 N_("cache command controls local memory cache\n\n"
	 "The following subcommands are recognized:\n"
	 "  stat        - print cache status (default)\n"
	 "  on|off      - turn on/off caching\n"
	 "  flush       - flush cache\n"
	 "  size <lim>  - set memory limit\n"
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
   {"chmod",   cmd_chmod,   N_("chmod [OPTS] mode file..."),
	 N_("Change the mode of each FILE to MODE.\n"
	    "\n"
	    " -c, --changes        - like verbose but report only when a change is made\n"
	    " -f, --quiet          - suppress most error messages\n"
	    " -v, --verbose        - output a diagnostic for every file processed\n"
	    " -R, --recursive      - change files and directories recursively\n"
	    "\n"
	    "MODE can be an octal number or symbolic mode (see chmod(1))\n")},
   {"close",   cmd_close,   "close [-a]",
	 N_("Close idle connections. By default only with current server.\n"
	 " -a  close idle connections with all servers\n")},
   {"cls",     cmd_cls,     N_("[re]cls [opts] [path/][pattern]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	    "or via pipe to external command.\n"
	    "\n"
	    /* note: I've tried to keep options which are likely to be always
	     * turned on (via cmd:cls-default, etc) capital, to leave lowercase
	     * available for options more commonly used manually.  -s/-S is an
	     * exception; they both seem to be options used manually, so I made
	     * them align with GNU ls options. */
	    " -1                   - single-column output\n"
	    " -a, --all            - show dot files\n"
	    " -B, --basename       - show basename of files only\n"
	    "     --block-size=SIZ - use SIZ-byte blocks\n"
	    " -d, --directory      - list directory entries instead of contents\n"
	    " -F, --classify       - append indicator (one of /@) to entries\n"
	    " -h, --human-readable - print sizes in human readable format (e.g., 1K)\n"
	    "     --si             - likewise, but use powers of 1000 not 1024\n"
	    " -k, --kilobytes      - like --block-size=1024\n"
	    " -l, --long           - use a long listing format\n"
	    " -q, --quiet          - don't show status\n"
	    " -s, --size           - print size of each file\n"
	    "     --filesize       - if printing size, only print size for files\n"
	    " -i, --nocase         - case-insensitive pattern matching\n"
	    " -I, --sortnocase     - sort names case-insensitively\n"
	    " -D, --dirsfirst      - list directories first\n"
	    "     --sort=OPT       - \"name\", \"size\", \"date\"\n"
	    " -S                   - sort by file size\n"
	    " --user, --group, --perms, --date, --linkcount, --links\n"
	    "                      - show individual fields\n"
	    " --time-style=STYLE   - use specified time format\n"
	    "\n"
	    "By default, cls output is cached, to see new listing use `recls' or\n"
	    "`cache flush'.\n"
	    "\n"
	    "The variables cls-default and cls-completion-default can be used to\n"
	    "specify defaults for cls listings and completion listings, respectively.\n"
	    "For example, to make completion listings show file sizes, set\n"
	    "cls-completion-default to \"-s\".\n"
	    "\n"
	    /* FIXME: poorly worded. another explanation of --filesize: if a person
	     * wants to only see file sizes for files (not dirs) when he uses -s,
	     * add --filesize; it won't have any effect unless -s is also used, so
	     * it can be enabled all the time. (that's also poorly worded, and too
	     * long.) */
	    "Tips: Use --filesize with -D to pack the listing better.  If you don't\n"
	    "always want to see file sizes, --filesize in cls-default will affect the\n"
	    "-s flag on the commandline as well.  Add `-i' to cls-completion-default\n"
	    "to make filename completion case-insensitive.\n"
	   )},
   {"connect", ALIAS_FOR(open)},
   {"command", cmd_command},
   {"debug",   cmd_debug,  N_("debug [OPTS] [<level>|off]"),
	 N_("Set debug level to given value or turn debug off completely.\n"
	 " -o <file>  redirect debug output to the file\n"
	 " -c  show message context\n"
	 " -p  show PID\n"
	 " -t  show timestamps\n")},
   {"du",      cmd_du,  N_("du [options] <dirs>"),
	 N_("Summarize disk usage.\n"
	 " -a, --all             write counts for all files, not just directories\n"
	 "     --block-size=SIZ  use SIZ-byte blocks\n"
	 " -b, --bytes           print size in bytes\n"
	 " -c, --total           produce a grand total\n"
	 " -d, --max-depth=N     print the total for a directory (or file, with --all)\n"
	 "                       only if it is N or fewer levels below the command\n"
	 "                       line argument;  --max-depth=0 is the same as\n"
	 "                       --summarize\n"
	 " -F, --files           print number of files instead of sizes\n"
	 " -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)\n"
	 " -H, --si              likewise, but use powers of 1000 not 1024\n"
	 " -k, --kilobytes       like --block-size=1024\n"
	 " -m, --megabytes       like --block-size=1048576\n"
	 " -S, --separate-dirs   do not include size of subdirectories\n"
	 " -s, --summarize       display only a total for each argument\n"
	 "     --exclude=PAT     exclude files that match PAT\n")},
   {"echo",    cmd_echo,   0},
   {"edit",    cmd_edit,   N_("edit [OPTS] <file>"),
	 N_("Retrieve remote file to a temporary location, run a local editor on it\n"
	 "and upload the file back if changed.\n"
	 " -k  keep the temporary file\n"
	 " -o <temp>  explicit temporary file location\n")},
   {"eval",    cmd_eval,   0},
   {"exit",    cmd_exit,   N_("exit [<code>|bg]"),
	 N_("exit - exit from lftp or move to background if jobs are active\n\n"
	 "If no jobs active, the code is passed to operating system as lftp\n"
	 "termination status. If omitted, exit code of last command is used.\n"
	 "`bg' forces moving to background if cmd:move-background is false.\n")},
   {"fg", ALIAS_FOR(wait)},
   {"find",    cmd_find,0,
	 N_("Usage: find [OPTS] [directory]\n"
	 "Print contents of specified directory or current directory recursively.\n"
	 "Directories in the list are marked with trailing slash.\n"
	 "You can redirect output of this command.\n"
	 " -d, --maxdepth=LEVELS  Descend at most LEVELS of directories.\n")},
   {"get",     cmd_get,    N_("get [OPTS] <rfile> [-o <lfile>]"),
	 N_("Retrieve remote file <rfile> and store it to local file <lfile>.\n"
	 " -o <lfile> specifies local file name (default - basename of rfile)\n"
	 " -c  continue, resume transfer\n"
	 " -E  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"get1",    cmd_get1,   0,0},
   {"glob",    cmd_glob,   N_("glob [OPTS] <cmd> <args>"),
	 N_(
	 "Expand wildcards and run specified command.\n"
	 "Options can be used to expand wildcards to list of files, directories,\n"
	 "or both types. Type selection is not very reliable and depends on server.\n"
	 "If entry type cannot be determined, it will be included in the list.\n"
	 " -f  plain files (default)\n"
	 " -d  directories\n"
	 " -a  all types\n"
	 " --exist      return zero exit code when the patterns expand to non-empty list\n"
	 " --not-exist  return zero exit code when the patterns expand to an empty list\n")},
   {"help",    cmd_help,   N_("help [<cmd>]"),
	 N_("Print help for command <cmd>, or list of available commands\n")},
   {"jobs",    cmd_jobs,   "jobs [-v] [<job_no...>]",
	 N_("List running jobs. -v means verbose, several -v can be specified.\n"
	    "If <job_no> is specified, only list a job with that number.\n")},
   {"kill",    cmd_kill,   N_("kill all|<job_no>"),
	 N_("Delete specified job with <job_no> or all jobs\n")},
   {"lcd",     cmd_lcd,    N_("lcd <ldir>"),
	 N_("Change current local directory to <ldir>. The previous local directory\n"
	 "is stored as `-'. You can do `lcd -' to change the directory back.\n")},
   {"lftp",    cmd_lftp,   N_("lftp [OPTS] <site>"),
	 N_("`lftp' is the first command executed by lftp after rc files\n"
	 " -f <file>           execute commands from the file and exit\n"
	 " -c <cmd>            execute the commands and exit\n"
	 " --norc              don't execute rc files from the home directory\n"
	 " --help              print this help and exit\n"
	 " --version           print lftp version and exit\n"
	 "Other options are the same as in `open' command:\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " -s <slot>           assign the connection to this slot\n"
	 " -d                  switch on debugging mode\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"ln",      cmd_ln,	    N_("ln [-s] <file1> <file2>"),
	 N_("Link <file1> to <file2>\n")},
   {"lpwd",    cmd_lpwd},
   {"local",   cmd_local},
   {"login", ALIAS_FOR(user)},
   {"ls",      cmd_ls,	    N_("ls [<args>]"),
	 N_("List remote files. You can redirect output of this command to file\n"
	 "or via pipe to external command.\n"
	 "By default, ls output is cached, to see new listing use `rels' or\n"
	 "`cache flush'.\n"
	 "See also `help cls'.\n")},
   {"mget",    cmd_get,	   N_("mget [OPTS] <files>"),
	 N_("Gets selected files with expanded wildcards\n"
	 " -c  continue, resume transfer\n"
	 " -d  create directories the same as in file names and get the\n"
	 "     files into them instead of current directory\n"
	 " -E  delete remote files after successful transfer\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"mirror",  cmd_mirror, N_("mirror [OPTS] [remote [local]]"), HELP_IN_MODULE},
   {"mkdir",   cmd_mkdir,  N_("mkdir [OPTS] <dirs>"),
	 N_("Make remote directories\n"
	 " -p  make all levels of path\n"
	 " -f  be quiet, suppress messages\n")},
   {"module",  cmd_module, N_("module name [args]"),
	 N_("Load module (shared object). The module should contain function\n"
	 "   void module_init(int argc,const char *const *argv)\n"
	 "If name contains a slash, then the module is searched in current\n"
	 "directory, otherwise in directories specified by setting module:path.\n")},
   {"more",    cmd_cat,    N_("more <files>"),
	 N_("Same as `cat <files> | more'. if PAGER is set, it is used as filter\n")},
   {"mput",    cmd_get,	   N_("mput [OPTS] <files>"),
	 N_("Upload files with wildcard expansion\n"
	 " -c  continue, reput\n"
	 " -d  create directories the same as in file names and put the\n"
	 "     files into them instead of current directory\n"
	 " -E  delete local files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"mrm",     cmd_mrm,    N_("mrm <files>"),
	 N_("Removes specified files with wildcard expansion\n")},
   {"mv",      cmd_mv,	   N_("mv <file1> <file2>"),
	 N_("Rename <file1> to <file2>\n")},
   {"mmv",      cmd_mmv,   N_("mmv [OPTS] <files> <target-dir>"),
	 N_("Move <files> to <target-directory> with wildcard expansion\n"
	 " -O <dir>  specifies the target directory (alternative way)\n")},
   {"nlist",   cmd_ls,     N_("[re]nlist [<args>]"),
	 N_("List remote file names.\n"
	 "By default, nlist output is cached, to see new listing use `renlist' or\n"
	 "`cache flush'.\n")},
   {"open",    cmd_open,   N_("open [OPTS] <site>"),
	 N_("Select a server, URL or bookmark\n"
	 " -e <cmd>            execute the command just after selecting\n"
	 " -u <user>[,<pass>]  use the user/password for authentication\n"
	 " -p <port>           use the port for connection\n"
	 " -s <slot>           assign the connection to this slot\n"
	 " -d                  switch on debugging mode\n"
	 " <site>              host name, URL or bookmark name\n")},
   {"pget",    cmd_get,    N_("pget [OPTS] <rfile> [-o <lfile>]"),
	 N_("Gets the specified file using several connections. This can speed up transfer,\n"
	 "but loads the net heavily impacting other users. Use only if you really\n"
	 "have to transfer the file ASAP.\n"
	 "\nOptions:\n"
	 " -c  continue transfer. Requires <lfile>.lftp-pget-status file.\n"
	 " -n <maxconn>  set maximum number of connections (default is is taken from\n"
	 "     pget:default-n setting)\n"
	 " -O <base> specifies base directory where files should be placed\n")},
   {"put",     cmd_get,    N_("put [OPTS] <lfile> [-o <rfile>]"),
	 N_("Upload <lfile> with remote name <rfile>.\n"
	 " -o <rfile> specifies remote file name (default - basename of lfile)\n"
	 " -c  continue, reput\n"
	 "     it requires permission to overwrite remote files\n"
	 " -E  delete local files after successful transfer (dangerous)\n"
	 " -a  use ascii mode (binary is the default)\n"
	 " -O <base> specifies base directory or URL where files should be placed\n")},
   {"pwd",     cmd_pwd,    "pwd [-p]",
	 N_("Print current remote URL.\n"
	 " -p  show password\n")},
   {"queue",   cmd_queue,  N_("queue [OPTS] [<cmd>]"),
	 N_("\n"
	 "       queue [-n num] <command>\n\n"
	 "Add the command to queue for current site. Each site has its own command\n"
	 "queue. `-n' adds the command before the given item in the queue. It is\n"
	 "possible to queue up a running job by using command `queue wait <jobno>'.\n"
	 "\n"
	 "       queue --delete|-d [index or wildcard expression]\n\n"
	 "Delete one or more items from the queue. If no argument is given, the last\n"
	 "entry in the queue is deleted.\n"
	 "\n"
	 "       queue --move|-m <index or wildcard expression> [index]\n\n"
	 "Move the given items before the given queue index, or to the end if no\n"
	 "destination is given.\n"
	 "\n"
	 "Options:\n"
	 " -q                  Be quiet.\n"
	 " -v                  Be verbose.\n"
	 " -Q                  Output in a format that can be used to re-queue.\n"
	 "                     Useful with --delete.\n"
	 )},
   {"quit", ALIAS_FOR(exit)},
   {"quote",   cmd_ls,	   N_("quote <cmd>"),
	 N_("Send the command uninterpreted. Use with caution - it can lead to\n"
	 "unknown remote state and thus will cause reconnect. You cannot\n"
	 "be sure that any change of remote state because of quoted command\n"
	 "is solid - it can be reset by reconnect at any time.\n")},
   {"recls",    cmd_cls,   0,
	 N_("recls [<args>]\n"
	 "Same as `cls', but don't look in cache\n")},
   {"reget",   cmd_get,    0,
	 N_("Usage: reget [OPTS] <rfile> [-o <lfile>]\n"
	 "Same as `get -c'\n")},
   {"rels",    cmd_ls,	    0,
	 N_("Usage: rels [<args>]\n"
	    "Same as `ls', but don't look in cache\n")},
   {"renlist", cmd_ls,	    0,
	 N_("Usage: renlist [<args>]\n"
	 "Same as `nlist', but don't look in cache\n")},
   {"repeat",  cmd_repeat, N_("repeat [OPTS] [delay] [command]"), HELP_IN_MODULE},
   {"reput",   cmd_get,    0,
	 N_("Usage: reput <lfile> [-o <rfile>]\n"
	 "Same as `put -c'\n")},
   {"rm",      cmd_rm,	    N_("rm [-r] [-f] <files>"),
	 N_("Remove remote files\n"
	    " -r  recursive directory removal, be careful\n"
	    " -f  work quietly\n")},
   {"rmdir",   cmd_rm,	    N_("rmdir [-f] <dirs>"),
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
   {"shell", ALIAS_FOR2("!",shell)},
   {"site",    cmd_ls,	   N_("site <site-cmd>"),
	 N_("Execute site command <site_cmd> and output the result\n"
	 "You can redirect its output\n")},
   {"sleep",   cmd_sleep, 0, HELP_IN_MODULE},
   {"slot",    cmd_slot, 0,
        N_("Usage: slot [<label>]\n"
	"List assigned slots.\n"
	"If <label> is specified, switch to the slot named <label>.\n")},
   {"source",  cmd_source, N_("source <file>"),
	 N_("Execute commands recorded in file <file>\n")},
   {"suspend", cmd_suspend},
   {"torrent", cmd_torrent, N_("torrent [OPTS] <file|URL>..."), HELP_IN_MODULE},
   {"user",    cmd_user,   N_("user <user|URL> [<pass>]"),
	 N_("Use specified info for remote login. If you specify URL, the password\n"
	 "will be cached for future usage.\n")},
   {"version", cmd_ver,    0,
	 N_("Shows lftp version\n")},
   {"wait",    cmd_wait,   N_("wait [<jobno>]"),
	 N_("Wait for specified job to terminate. If jobno is omitted, wait\n"
	 "for last backgrounded job.\n")},
   {"zcat",    cmd_cat,    N_("zcat <files>"),
	 N_("Same as cat, but filter each file through zcat\n")},
   {"zmore",   cmd_cat,    N_("zmore <files>"),
	 N_("Same as more, but filter each file through zcat\n")},
   {"bzcat",    cmd_cat,    0,
	 N_("Same as cat, but filter each file through bzcat\n")},
   {"bzmore",   cmd_cat,    0,
	 N_("Same as more, but filter each file through bzcat\n")},

   {".tasks",  cmd_tasks,  0,0},
   {".empty",  cmd_empty,  0,0},
   {".notempty",cmd_notempty,0,0},
   {".true",   cmd_true,   0,0},
   {".false",  cmd_false,  0,0},
   {".mplist", cmd_ls,	   0,0},
};
const int CmdExec::static_cmd_table_length=sizeof(static_cmd_table)/sizeof(static_cmd_table[0]);

#define charcasecmp(a,b) (tolower((unsigned char)(a))-tolower((unsigned char)(b)))
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
      for(s=*names,u=unprec_name; *s && !charcasecmp(*u,*s); s++,u++)
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

   if(RestoreCWD()==-1)
   {
      if(cd_to[0]!='/')
      {
	 eprintf("No current local directory, use absolute path.\n");
	 return 0;
      }
   }

   int res=chdir(cd_to);
   if(res==-1)
   {
      perror(cd_to);
      exit_code=1;
      return 0;
   }

   old_lcwd.set(cwd->GetName());

   SaveCWD();

   const char *name=cwd->GetName();
   if(interactive)
      eprintf(_("lcd ok, local cwd=%s\n"),name?name:"?");

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_cd()
{
   if(args->count()==1)
      args->Append("~");

   bool is_file=false;

   if(args->count()!=2)
   {
      // xgettext:c-format
      eprintf(_("Usage: cd remote-dir\n"));
      return 0;
   }

   const char *dir=args->getarg(1);
   const char *url=0;

   if(!strcmp(dir,"-"))
   {
      dir=cwd_history.Lookup(session);
      if(!dir)
      {
	 eprintf(_("%s: no old directory for this site\n"),args->a0());
	 return 0;
      }
      args->setarg(1,dir); // for status line
      dir=args->getarg(1);
   }

   bool dir_needs_slash=false;
   if(url::is_url(dir))
   {
      ParsedURL u(dir,true);
      FileAccess *new_session=FileAccess::New(&u);
      bool same_site=session->SameSiteAs(new_session);
      Delete(new_session);
      if(!same_site)
	 return builtin_open();
      url=dir;
      dir=alloca_strdup(u.path);
      dir_needs_slash=url::dir_needs_trailing_slash(u.proto);
   }
   else
   {
      dir_needs_slash=url::dir_needs_trailing_slash(session->GetProto());
   }

   if(dir_needs_slash)
      is_file=(last_char(dir)!='/');

   int cache_is_dir=FileAccess::cache->IsDirectory(session,dir);
   if(cache_is_dir==1)
   {
      if(is_file && dir_needs_slash && last_char(dir)!='/')
	 dir=xstring::get_tmp(dir).append('/');
      is_file=false;
   }
   else if(cache_is_dir==0)
      is_file=true;

   old_cwd=session->GetCwd();
   FileAccess::Path new_cwd(old_cwd);
   new_cwd.Change(dir,is_file);
   if(url)
      new_cwd.SetURL(url);
   if(!verify_path || background
   || (!verify_path_cached && cache_is_dir==1))
   {
      cwd_history.Set(session,old_cwd);
      session->SetCwd(new_cwd);
      if(slot)
	 ConnectionSlot::SetCwd(slot,new_cwd);
      exit_code=0;
      return 0;
   }
   session->PathVerify(new_cwd);
   session->Roll();
   builtin=BUILTIN_CD;
   return this;
}

Job *CmdExec::builtin_exit()
{
   bool detach=ResMgr::QueryBool("cmd:move-background-detach",0);
   bool bg=false;
   bool kill=false;
   int code=prev_exit_code;
   CmdExec *exec=this;
   const char *a;
   args->rewind();
   while((a=args->getnext())!=0)
   {
      if(!strcmp(a,"bg"))
	 bg=true;
      if(!strcmp(a,"top") || !strcmp(a,"bg"))
      {
	 if(top)
	    exec=top.get_non_const();
      }
      else if(!strcmp(a,"parent"))
      {
	 if(parent_exec)
	    exec=parent_exec;
      }
      else if(!strcmp(a,"kill"))
      {
	 kill=true;
	 bg=false;
      }
      else if(sscanf(a,"%i",&code)!=1)
      {
	 eprintf(_("Usage: %s [<exit_code>]\n"),args->a0());
	 return 0;
      }
   }
   // Note: one job is this CmdExec.
   if(!bg && exec->top_level
   && !ResMgr::QueryBool("cmd:move-background",0) && NumberOfChildrenJobs()>0)
   {
      eprintf(_(
	 "There are running jobs and `cmd:move-background' is not set.\n"
	 "Use `exit bg' to force moving to background or `kill all' to terminate jobs.\n"
      ));
      return 0;
   }
   if(!detach && Job::NumberOfChildrenJobs()==0)
      detach=true;
   if(kill)
      Job::KillAll();
   if(detach) {
      for(CmdExec *e=this; e!=exec; e=e->parent_exec)
	 e->Exit(code);
      exec->Exit(code);
   } else {
      int loc=0;
      exec->SetAutoTerminateInBackground(true);
      eprintf(_(
	 "\n"
	 "lftp now tricks the shell to move it to background process group.\n"
	 "lftp continues to run in the background despite the `Stopped' message.\n"
	 "lftp will automatically terminate when all jobs are finished.\n"
	 "Use `fg' shell command to return to lftp if it is still running.\n"
      ));
      // trick the shell
      switch(pid_t pid=fork()) {
      case 0: // child
	 sleep(1);   // wait for the parent to stop (is there a safer way?)
	 ::kill(getppid(),SIGCONT);
	 _exit(0);
      default: // parent
	 raise(SIGSTOP);
	 waitpid(pid,&loc,0); // clean-up
	 break;
      case -1:
	 exec->Exit(code);
	 break;
      }
   }
   exit_code=code;
   return 0;
}
void CmdExec::Exit(int code)
{
   while(feeder)
      RemoveFeeder();
   cmd_buf.Empty();
   if(interactive)
   {
      ListDoneJobs();
      BuryDoneJobs();
      if(FindJob(last_bg)==0)
	 last_bg=-1;
   }
   exit_code=prev_exit_code=code;
   return;
}

void CmdExec::enable_debug(const char *opt)
{
   int level=DEFAULT_DEBUG_LEVEL;
   if(opt && isdigit((unsigned char)opt[0]))
      level=atoi(opt);
   const char *c="debug";
   ResMgr::Set("log:enabled",c,"yes");
   ResMgr::Set("log:level",c,xstring::format("%d",level));
}

CmdFeeder *lftp_feeder=0;
Job *CmdExec::builtin_lftp()
{
   int c;
   xstring cmd;
   xstring rc;
   ArgV open("open");
   open.Add("--lftp");

   enum {
      OPT_USER,
      OPT_PASSWORD,
      OPT_ENV_PASSWORD,
   };
   static struct option lftp_options[]=
   {
      {"help",no_argument,0,'h'},
      {"version",no_argument,0,'v'},
      {"debug",optional_argument,0,'d'},
      {"rcfile",required_argument,0,'r'},
      // other options are for "open" command
      {"port",required_argument,0,'p'},
      {"user",required_argument,0,OPT_USER},
      {"password",required_argument,0,OPT_PASSWORD},
      {"env-password",no_argument,0,OPT_ENV_PASSWORD},
      {"execute",required_argument,0,'e'},
      {"no-bookmark",no_argument,0,'B'},
      {"slot",required_argument,0,'s'},
      {0,0,0,0}
   };

   while((c=args->getopt_long("+f:c:vhdu:p:e:s:B",lftp_options))!=EOF)
   {
      switch(c)
      {
      case('h'):
	 cmd.set("help lftp;");
	 break;
      case('v'):
	 cmd.set("version;");
	 break;
      case('f'):
	 cmd.set("source ");
	 cmd.append_quoted(optarg);
	 cmd.append(';');
	 break;
      case('c'):
	 args->CombineCmdTo(cmd,args->getindex()-1).append("\n\n");
	 args->seek(args->count());
	 break;
      case('d'):
	 enable_debug(optarg);
	 break;
      case('r'):
	 rc.append("&&source ").append_quoted(optarg);
	 break;

      // "open" command options are passed along
      case('s'):
	 open.Add("-s");
	 break;
      case('p'):
	 open.Add("-p").Add(optarg);
	 break;
      case('u'):
	 open.Add("-u").Add(optarg);
	 break;
      case(OPT_USER):
	 open.Add("--user").Add(optarg);
	 break;
      case(OPT_PASSWORD):
	 open.Add("--password").Add(optarg);
	 break;
      case(OPT_ENV_PASSWORD):
	 open.Add("--env-password");
	 break;
      case('e'):
	 open.Add("-e").Add(optarg);
	 break;
      case('B'):
	 open.Add("-B");
	 break;

      case('?'):
	 eprintf(_("Try `%s --help' for more information\n"),args->a0());
	 return 0;
      }
   }

   for(const char *arg=args->getcurr(); arg; arg=args->getnext())
      open.Add(arg);

   // feeder should be set before PrependCmd
   if(!cmd && lftp_feeder)  // no feeder and no commands
   {
      SetCmdFeeder(lftp_feeder);
      lftp_feeder=0;
      FeedCmd("||command exit\n");   // if the command fails, quit
   }

   // prepended commands are executed in reverse order: rc, cmd, open
   if(open.count()>2) {
      if(cmd) {
	 eprintf(_("%s: -c, -f, -v, -h conflict with other `open' options and arguments\n"),args->a0());
	 return 0;
      }
      xstring_ca open_cmd(open.CombineQuoted());
      PrependCmd(open_cmd);
   }

   if(cmd)
      PrependCmd(cmd);
   if(rc)
      PrependCmd(rc);

   exit_code=0;
   return 0;
}

Job *CmdExec::builtin_open()
{
   ReuseSavedSession();

   const char *port=NULL;
   const char *host=NULL;
   const char *path=NULL;
   const char *user=NULL;
   const char *pass=NULL;
   int	 c;
   NetRC::Entry *nrc=0;
   char  *cmd_to_exec=0;
   const char *op=args->a0();
   bool insecure=false;
   bool no_bm=false;

   enum {
      OPT_USER,
      OPT_PASSWORD,
      OPT_ENV_PASSWORD,
      OPT_LFTP,
   };
   static struct option open_options[]=
   {
      {"port",required_argument,0,'p'},
      {"user",required_argument,0,OPT_USER},
      {"password",required_argument,0,OPT_PASSWORD},
      {"env-password",no_argument,0,OPT_ENV_PASSWORD},
      {"execute",required_argument,0,'e'},
      {"debug",optional_argument,0,'d'},
      {"no-bookmark",no_argument,0,'B'},
      {"slot",required_argument,0,'s'},
      {"lftp",no_argument,0,OPT_LFTP},
      {0,0,0,0}
   };

   while((c=args->getopt_long("u:p:e:s:dB",open_options))!=EOF)
   {
      switch(c)
      {
      case('s'):
	if (*optarg) ChangeSlot(optarg);
        break;
      case('p'):
	 port=optarg;
	 break;
      case('u'):
      {
         user=optarg;
         char *sep=strchr(optarg,',');
	 if(sep==NULL)
	    sep=strchr(optarg,' ');
	 if(sep==NULL)
	    sep=strchr(optarg,':');
	 if(sep==NULL)
	    break;
	 *sep=0;
	 pass=sep+1;
	 insecure=true;
         break;
      }
      case(OPT_USER):
	 user=optarg;
	 break;
      case(OPT_PASSWORD):
	 pass=optarg;
	 break;
      case(OPT_ENV_PASSWORD):
	 pass=getenv("LFTP_PASSWORD");
	 break;
      case('d'):
	 enable_debug(optarg);
	 break;
      case('e'):
	 cmd_to_exec=optarg;
	 break;
      case('B'):
	 no_bm=true;
	 break;
      case(OPT_LFTP):
	 op="lftp";
	 break;
      case('?'):
	 eprintf(_("Usage: %s [-e cmd] [-p port] [-u user[,pass]] <host|url>\n"),op);
	 return 0;
      }
   }

   if(optind<args->count())
      host=args->getarg(optind++);

   Ref<ParsedURL> url;

   const char *bm=0;

   if(cmd_to_exec)
      PrependCmd(cmd_to_exec);

   if(!no_bm && host && (bm=lftp_bookmarks.Lookup(host))!=0)
   {
      xstring& cmd=xstring::get_tmp("open -B ");
      if(user)
      {
	 cmd.append("--user ").append_quoted(user);
	 if(pass)
	    cmd.append(" --password ").append_quoted(pass);
	 cmd.append(' ');
      }
      if(port)
      {
	 cmd.append("-p ");
	 cmd.append_quoted(port);
	 cmd.append(' ');
      }

      cmd.append(bm);

      if(background)
	 cmd.append(" &\n");
      else
	 cmd.append(";\n");

      PrependCmd(cmd);
   }
   else
   {
      if(host && host[0])
      {
	 url=new ParsedURL(host);
	 bool no_proto=(!url->proto);

	 if(no_proto && url->host)
	 {
	    const char *p=ResMgr::Query("cmd:default-protocol",url->host);
	    if(!p)
	       p="ftp";
	    url=new ParsedURL(xstring::format("%s://%s",p,host));
	 }
	 if(user || port)
	 {
	    if(user)
	    {
	       url->user.set(user);
	       url->pass.set(pass);
	    }
	    if(port)
	       url->port.set(port);
	    xstring_ca host1(url->Combine());
	    url=new ParsedURL(host1);
	 }

	 const ParsedURL &uc=*url;
	 if(uc.host && uc.host[0] && uc.proto)
	 {
	    cwd_history.Set(session,session->GetCwd());

	    if(uc.user && !user)
	       user=uc.user;
	    if(uc.pass && !pass)
	    {
	       pass=uc.pass;
	       insecure=true;
	    }
	    host=uc.host;
	    if(uc.port && !port)
	       port=uc.port;
	    if(uc.path && !path)
	       path=uc.path;

	    FileAccess *new_session=FileAccess::New(uc.proto,host,port);
	    if(!new_session)
	    {
	       eprintf("%s: %s%s\n",op,uc.proto.get(),
			_(" - not supported protocol"));
	       return 0;
	    }

	    saved_session=session.borrow();
	    ChangeSession(new_session);
	 }

	 // user gets substituted only if no proto is specified.
	 if(!pass && (user || no_proto))
	 {
	    nrc=NetRC::LookupHost(host,user);
	    if(nrc)
	    {
	       if(!user)
		  ProtoLog::LogNote(3,"using user `%s' and password from ~/.netrc",nrc->user.get());
	       else
		  ProtoLog::LogNote(3,"using password from ~/.netrc");
	       user=nrc->user;
	       pass=nrc->pass;
	    }
	 }
      }
      else if(host && !host[0])
      {
	 ChangeSession(new DummyProto);
      }
      if(host && host[0] && session->GetHostName()==0)
	 session->Connect(host,port);
      if(user && *session->GetProto())
      {
	 if(!pass)
	    pass=GetPass(_("Password: "));
	 if(!pass)
	    eprintf(_("%s: GetPass() failed -- assume anonymous login\n"),
	       args->getarg(0));
	 else
	 {
	    session->Login(user,pass);
	    // assume the new password is the correct one.
	    session->SetPasswordGlobal(pass);
	    session->InsecurePassword(insecure && !no_bm);
	 }
      }
      if(host && host[0])
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
      const char *old=cwd_history.Lookup(session);
      if(old)
      {
	 bool is_file=false;
	 const char *old_url=0;
	 if(url::is_url(old))
	 {
	    ParsedURL u(old,true);
	    old_url=old;
	    old=alloca_strdup(u.path);
	    if(url::dir_needs_trailing_slash(u.proto))
	       is_file=(last_char(old)!='/');
	 }
	 else
	 {
	    if(url::dir_needs_trailing_slash(session->GetProto()))
	       is_file=(last_char(old)!='/');
	 }
	 session->SetCwd(FileAccess::Path(old,is_file,old_url));
      }

      const char *cd_arg=(url && url->orig_url)?url->orig_url.get():path;
      xstring& s=xstring::get_tmp("&& cd ");
      s.append_quoted(cd_arg);
      if(background)
	 s.append('&');
      s.append('\n');
      PrependCmd(s);
   }

   if(slot)
      ConnectionSlot::Set(slot,session);

   Reconfig(0);

   if(builtin==BUILTIN_OPEN)
      return this;

   ReuseSavedSession();

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
   const char *op=args->a0();
   int opt;
   GlobURL::type_select glob_type=GlobURL::FILES_ONLY;
   const char *cmd=0;
   bool nullglob=false;

   static struct option glob_options[]=
   {
      {"exist",no_argument,0,'e'},
      {"not-exist",no_argument,0,'E'},
      {0}
   };

   while((opt=args->getopt_long("+adf",glob_options))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 glob_type=GlobURL::ALL;
	 break;
      case('d'):
	 glob_type=GlobURL::DIRS_ONLY;
	 break;
      case('f'):
	 glob_type=GlobURL::FILES_ONLY;
	 break;
      case('e'):
	 cmd=".notempty";
	 nullglob=true;
	 break;
      case('E'):
	 cmd=".empty";
	 nullglob=true;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);	   // remove options.
   if(cmd)
      args->insarg(1,cmd);
   if(args->count()<2)
   {
      eprintf(_("Usage: %s [OPTS] command args...\n"),op);
      RevertToSavedSession();
      return 0;
   }
   assert(args_glob==0 && glob==0);
   args_glob=new ArgV();
   args->rewind();
   args_glob->Append(args->getnext());
   const char *pat=args->getnext();
   if(!pat)
   {
      args_glob=0;
      args->rewind();
      RevertToSavedSession();
      return cmd_command(this);
   }
   glob=new GlobURL(session,pat,glob_type);
   if(nullglob)
      glob->NullGlob();
   builtin=BUILTIN_GLOB;
   return this;
}

Job *CmdExec::builtin_queue()
{
   static struct option queue_options[]=
   {
      {"move",required_argument,0,'m'},
      {"delete",no_argument,0,'d'},
      {"quiet",no_argument,0,'q'},
      {"verbose",no_argument,0,'v'},
      {"queue",required_argument,0,'Q'},
      {0,0,0,0}
   };
   enum { ins, del, move } mode = ins;

   const char *arg = NULL;
   /* position to insert at (ins only) */
   int pos = -1; /* default to the end */
   int verbose = -1; /* default */

   int opt;
   while((opt=args->getopt_long("+dm:n:qvQw",queue_options))!=EOF)
   {
      switch(opt)
      {
      case 'n':
	 /* Actually, sending pos == -1 will work, but it'll put the
	  * job at the end; it's confusing for "-n 0" to mean "put
	  * it at the end", and that's the default anyway, so disallow
	  * it. */
	 if(!isdigit((unsigned char)optarg[0]) || atoi(optarg) == 0)
	 {
	    eprintf(_("%s: -n: positive number expected. "), args->a0());
	    goto err;
	 }
	 /* make offsets match the jobs output (starting at 1) */
	 pos = atoi(optarg) - 1;
	 break;

      case 'm':
	 mode = move;
	 arg = optarg;
	 break;

      case 'd':
	 mode = del;
	 break;

      case 'q':
	 verbose = 0;
	 break;

      case 'v':
	 verbose = 2;
	 break;

      case 'Q':
	 verbose = QueueFeeder::PrintRequeue;
	 break;

      case '?':
	 err:
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }

   if(verbose == -1)
   {
      if(mode == ins || mode == move)
	 verbose = 0;
      else
	 verbose = 1;
   }

   const int args_remaining = args->count() - args->getindex();
   switch(mode) {
      case ins: {
	 CmdExec *queue=GetQueue(false);
	 if(args_remaining==0)
	 {
	    if(!queue)
	    {
	       if(verbose)
		  printf(_("Created a stopped queue.\n"));
	       queue=GetQueue(true);
	       queue->Suspend();
	    }
	    else
	    {
	       xstring& buf=xstring::get_tmp("");
	       queue->FormatStatus(buf,2,"");
	       printf("%s",buf.get());
	    }
	    exit_code=0;
	    break;
	 }
	 if(!queue)
	    queue=GetQueue(true);

	 xstring_ca cmd(args->CombineCmd(args->getindex()));

	 if(!strcasecmp(cmd,"stop"))
	    queue->Suspend();
	 else if(!strcasecmp(cmd,"start"))
	    queue->Resume();
	 else
	    queue->queue_feeder->QueueCmd(cmd, session->GetCwd(),
					  cwd?cwd->GetName():0, pos, verbose);

	 last_bg=queue->jobno;
	 exit_code=0;
      }
      break;

      case del: {
         /* Accept:
	  * queue -d (delete the last job)
	  * queue -d 1  (delete entry 1)
	  * queue -d "get" (delete all *get*)
	  *
	  * We want an optional argument, but don't use getopt ::, since
	  * that'll disallow the space between arguments, which we want. */
         arg = args->getarg(args->getindex());

	 CmdExec *queue=GetQueue(false);
	 if(!queue) {
	    eprintf(_("%s: No queue is active.\n"), args->a0());
	    break;
	 }

	 if(!arg)
	    exit_code=!queue->queue_feeder->DelJob(-1, verbose); /* delete the last job */
	 else if(atoi(arg) != 0)
	    exit_code=!queue->queue_feeder->DelJob(atoi(arg)-1, verbose);
	 else
	    exit_code=!queue->queue_feeder->DelJob(arg, verbose);
      }
      break;

      case move: {
         /* Accept:
	  * queue -m 1 2  (move entry 1 to position 2)
	  * queue -m "*get*" 1
	  * queue -m 3    (move entry 3 to the end) */
         const char *a1 = args->getarg(args->getindex());
	 if(a1 && !isdigit((unsigned char)a1[0])) {
	    eprintf(_("%s: -m: Number expected as second argument. "), args->a0());
	    goto err;
	 }
	 /* default to moving to the end */
	 int to = a1? atoi(a1)-1:-1;

	 CmdExec *queue=GetQueue(false);
	 if(!queue) {
	    eprintf(_("%s: No queue is active.\n"), args->a0());
	    break;
	 }

	 if(atoi(arg) != 0) {
	    exit_code=!queue->queue_feeder->MoveJob(atoi(arg)-1, to, verbose);
	    break;
	 }

	 exit_code=!queue->queue_feeder->MoveJob(arg, to, verbose);
      }
      break;
   }

   return 0;
}

// below are only non-builtin commands
#define args	  (parent->args)
#define exit_code (parent->exit_code)
#define output	  (parent->output)
#define session	  (parent->session)
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
   bool ascii=true;
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
      ascii=false;
      mode=FA::QUOTE_CMD;
      if(!strcmp(op,"site"))
	 args->insarg(1,"SITE");
   }
   else if(!strcmp(op,".mplist")) {
      nlist=true;
      mode=FA::MP_LIST;
   }

   xstring_ca a(args->Combine(nlist?1:0));

   const char *var_ls=ResMgr::Query("cmd:ls-default",session->GetConnectURL(FA::NO_PATH));
   if(!nlist && args->count()==1 && var_ls[0])
      args->Append(var_ls);

   bool no_status=(!output || output->usesfd(1));

   FileCopyPeer *src_peer=0;
   if(!nlist)
   {
      FileCopyPeerDirList *dir_list=new FileCopyPeerDirList(session->Clone(),args.borrow());
      dir_list->UseColor(ResMgr::QueryTriBool("color:use-color",0,(!output && isatty(1))));
      src_peer=dir_list;
   }
   else
      src_peer=new FileCopyPeerFA(session->Clone(),a,mode);

   if(re)
      src_peer->NoCache();
   src_peer->SetDate(NO_DATE);
   src_peer->SetSize(NO_SIZE);
   FileCopyPeer *dst_peer=new FileCopyPeerFDStream(output.borrow(),FileCopyPeer::PUT);

   FileCopy *c=FileCopy::New(src_peer,dst_peer,false);
   c->DontCopyDate();
   c->LineBuffered();
   if(ascii)
      c->Ascii();

   CopyJob *j=new CopyJob(c,a,op);
   if(no_status)
      j->NoStatusOnWrite();

   return j;
}

/* this seems to belong here more than in FileSetOutput.cc ... */
const char *FileSetOutput::parse_argv(const Ref<ArgV>& a)
{
   enum {
      OPT_BLOCK_SIZE,
      OPT_DATE,
      OPT_FILESIZE,
      OPT_GROUP,
      OPT_LINKCOUNT,
      OPT_LINKS,
      OPT_PERMS,
      OPT_SI,
      OPT_SORT,
      OPT_TIME_STYLE,
      OPT_USER
   };
   static struct option cls_options[] = {
      {"all",no_argument,0,'a'},
      {"basename",no_argument,0,'B'},
      {"directory",no_argument,0,'d'},
      {"human-readable",no_argument,0,'h'},
      {"block-size",required_argument,0,OPT_BLOCK_SIZE},
      {"si",no_argument,0,OPT_SI},
      {"classify",no_argument,0,'F'},
      {"long",no_argument,0,'l'},
      {"quiet",no_argument,0,'q'},
      {"size",no_argument,0,'s'},	/* show size */
      {"filesize",no_argument,0,OPT_FILESIZE},	/* for files only */
      {"nocase",no_argument,0,'i'},
      {"sortnocase",no_argument,0,'I'},
      {"dirsfirst",no_argument,0,'D'},
      {"time-style",required_argument,0,OPT_TIME_STYLE},

      {"sort",required_argument,0,OPT_SORT},
      {"reverse",no_argument,0,'r'},
      {"user",no_argument,0,OPT_USER},
      {"group",no_argument,0,OPT_GROUP},
      {"perms",no_argument,0,OPT_PERMS},
      {"date",no_argument,0,OPT_DATE},
      {"linkcount",no_argument,0,OPT_LINKCOUNT},
      {"links",no_argument,0,OPT_LINKS},
      {0,0,0,0}
   };

   const char *time_style=ResMgr::Query("cmd:time-style",0);

   int opt;
   while((opt=a->getopt_long(":a1BdFhiklqsDISrt", cls_options))!=EOF)
   {
      switch(opt) {
      case OPT_SORT:
	 if(!strcasecmp(optarg, "name")) sort = FileSet::BYNAME;
	 else if(!strcasecmp(optarg, "size")) sort = FileSet::BYSIZE;
	 else if(!strcasecmp(optarg, "date")) sort = FileSet::BYDATE;
	 else if(!strcasecmp(optarg, "time")) sort = FileSet::BYDATE;
	 else return _("invalid argument for `--sort'");
	 break;
      case OPT_FILESIZE:
	 size_filesonly = true;
	 break;
      case OPT_USER:
	 mode |= USER;
	 break;
      case OPT_GROUP:
	 mode |= GROUP;
	 break;
      case OPT_PERMS:
	 mode |= PERMS;
	 break;
      case OPT_DATE:
	 mode |= DATE;
	 break;
      case OPT_LINKCOUNT:
	 mode |= NLINKS;
	 break;
      case OPT_LINKS:
	 mode |= LINKS;
	 break;
      case OPT_SI:
	 output_block_size = 1;
	 human_opts=human_autoscale|human_SI;
	 break;
      case OPT_BLOCK_SIZE:
	 output_block_size = atoi(optarg);
	 if(output_block_size == 0)
	    return _("invalid block size");
	 break;
      case('a'):
	 showdots = true;
	 break;
      case('1'):
	 single_column = true;
         break;
      case('B'):
	 basenames = true;
         break;
      case('d'):
	 list_directories = true;
         break;
      case('h'):
	 output_block_size = 1;
	 human_opts=human_autoscale|human_SI|human_base_1024;
         break;
      case('l'):
	 long_list();
         break;
      case('i'):
	 patterns_casefold = true;
         break;
      case('k'):
	 output_block_size = 1024;
         break;
      case('F'):
         classify=true;
         break;
      case('q'):
	 quiet = true;
         break;
      case('s'):
	 mode |= FileSetOutput::SIZE;
         break;
      case('D'):
	 sort_dirs_first = true;
	 break;
      case('I'):
	 sort_casefold = true;
	 break;
      case('S'):
	 sort = FileSet::BYSIZE;
	 break;
      case('t'):
	 sort = FileSet::BYDATE;
	 break;
      case('r'):
	 sort_reverse = true;
	 break;
      case OPT_TIME_STYLE:
	 time_style=optarg;
	 break;

      default:
	 return a->getopt_error_message(opt);
      }
   }

   time_fmt.set(0);
   if(time_style && time_style[0]) {
      if (mode & DATE)
	 need_exact_time=ResMgr::QueryBool("cmd:cls-exact-time",0);
      if(time_style[0]=='+')
	 time_fmt.set(time_style+1);
      else if(!strcmp(time_style,"full-iso"))
//	 time_fmt.set("%Y-%m-%d %H:%M:%S.%N %z"); // %N and %z are GNU extensions
	 time_fmt.set("%Y-%m-%d %H:%M:%S");
      else if(!strcmp(time_style,"long-iso"))
	 time_fmt.set("%Y-%m-%d %H:%M");
      else if(!strcmp(time_style,"iso"))
	 time_fmt.set("%Y-%m-%d \n%m-%d %H:%M");
      else
	 time_fmt.set(time_style);
   }

   // remove parsed options.
   while(a->getindex()>1)
      a->delarg(1);
   a->rewind();

   return NULL;
}

CMD(cls)
{
   exit_code=0;

   const char *op=args->a0();
   bool re=false;

   JobRef<OutputJob> out(new OutputJob(output.borrow(), args->a0()));
   Ref<FileSetOutput> fso(new FileSetOutput);
   fso->config(out);

   if(!strncmp(op,"re",2))
      re=true;

   fso->parse_res(ResMgr::Query("cmd:cls-default", 0));

   if(const char *err = fso->parse_argv(args)) {
      eprintf("%s: %s\n", op, err);
      eprintf(_("Try `help %s' for more information.\n"),op);
      return 0;
   }

   clsJob *j = new clsJob(session->Clone(), args.borrow(), fso.borrow(), out.borrow());
   if(re)
      j->UseCache(false);

   return j;
}

CMD(cat)
{
   const char *op=args->a0();
   int opt;
   bool ascii=false;
   bool auto_ascii=true;

   while((opt=args->getopt("+bau"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 ascii=true;
	 auto_ascii=false;
	 break;
      case('b'):
	 ascii=false;
	 auto_ascii=false;
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
   OutputJob *out=new OutputJob(output.borrow(), args->a0());
   CatJob *j=new CatJob(session->Clone(),out,args.borrow());
   if(!auto_ascii)
   {
      if(ascii)
	 j->Ascii();
      else
	 j->Binary();
   }
   return j;
}

CMD(get)
{
   static struct option get_options[] = {
      {"continue",no_argument,0,'c'},
      {"Remove-source-files",no_argument,0,'E'},
      {"remove-target",no_argument,0,'e'},
      {"ascii",no_argument,0,'a'},
      {"target-directory",required_argument,0,'O'},
      {"destination-directory",required_argument,0,'O'},
      {"quiet",no_argument,0,'q'},
      {"parallel",optional_argument,0,'P'},
      {"use-pget-n",optional_argument,0,'n'},
      {"glob",no_argument,0,256+'g'},
      {"reverse",no_argument,0,256+'R'},
      {0}
   };
   const char *opts="+cEeaO:qP";

   int opt;
   bool cont=false;
   const char *op=args->a0();
   Ref<ArgV> get_args(new ArgV(op));
   int n_conn=1;
   int parallel=0;
   bool del=false;
   bool del_target=false;
   bool ascii=false;
   bool glob=false;
   bool make_dirs=false;
   bool reverse=false;
   bool quiet=false;
   const char *output_dir=0;

   if(!strncmp(op,"re",2))
   {
      cont=true;
      opts="+EaO:qP:";
   }
   if(!strcmp(op,"pget"))
   {
      opts="+n:ceO:q";
      n_conn=0; // default, which means to take pget:default-n
   }
   else if(!strcmp(op,"put") || !strcmp(op,"reput"))
   {
      reverse=true;
   }
   else if(!strcmp(op,"mget") || !strcmp(op,"mput"))
   {
      glob=true;
      opts="cEeadO:qP:";
      reverse=(op[1]=='p');
   }
   if(!reverse)
   {
      const char *od=ResMgr::Query("xfer:destination-directory",session->GetHostName());
      if(od && *od)
	 output_dir=od;
   }
   while((opt=args->getopt_long(opts,get_options))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('n'):
	 if(optarg) {
	    if(!isdigit((unsigned char)optarg[0]))
	    {
	       eprintf(_("%s: %s: Number expected. "),"-n",op);
	       goto err;
	    }
	    n_conn=atoi(optarg);
	 } else
	    n_conn=3;
	 break;
      case('E'):
	 del=true;
	 break;
      case('e'):
	 del_target=true;
	 break;
      case('a'):
	 ascii=true;
	 break;
      case('d'):
	 make_dirs=true;
	 break;
      case('O'):
	 output_dir=optarg;
	 break;
      case('q'):
	 quiet=true;
	 break;
      case('P'):
	 if(optarg) {
	    if(!isdigit((unsigned char)optarg[0]))
	    {
	       eprintf(_("%s: %s: Number expected. "),"-P",op);
	       goto err;
	    }
	    parallel=atoi(optarg);
	 } else
	    parallel=3;
	 break;
      case(256+'R'):
	 reverse=!reverse;
	 break;
      case(256+'g'):
	 glob=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   if(cont && del_target) {
      eprintf(_("%s: --continue conflicts with --remove-target.\n"),op);
      return 0;
   }
   JobRef<GetJob> j;
   if(glob)
   {
      if(args->getcurr()==0)
      {
      file_name_missed:
	 // xgettext:c-format
	 eprintf(_("File name missed. "));
	 goto err;
      }
      mgetJob *mj=new mgetJob(session->Clone(),args,cont,make_dirs);
      if(output_dir)
	 mj->OutputDir(output_dir);
      j=mj;
   }
   else
   {
      args->back();
      const char *a=args->getnext();
      if(a==0)
	 goto file_name_missed;
      while(a)
      {
	 const char *src=a;
	 const char *dst=0;
	 a=args->getnext();
	 if(a && !strcmp(a,"-o"))
	 {
	    dst=args->getnext();
	    a=args->getnext();
	 }
	 if(reverse)
	    src=expand_home_relative(src);
	 dst=output_file_name(src,dst,!reverse,output_dir,false);
	 get_args->Append(src);
	 get_args->Append(dst);
      }
      j=new GetJob(session->Clone(),get_args.borrow(),cont);
   }
   if(reverse)
      j->Reverse();
   if(del)
      j->DeleteFiles();
   if(del_target)
      j->RemoveTargetFirst();
   if(ascii)
      j->Ascii();
   if(n_conn!=1)
      j->SetCopyJobCreator(new pCopyJobCreator(n_conn));
   if(parallel>0)
      j->SetParallel(parallel);
   j->Quiet(quiet);
   return j.borrow();
}

CMD(edit)
{
   /* Download specified remote file into a temporary local file; run an
    * editor on it and upload the file back if changed. Remove the temp file. */

   const char *op=args->a0();
   xstring temp_file;
   bool keep_temp=false;

   int opt;
   while((opt=args->getopt("ok"))!=EOF)
   {
      switch(opt)
      {
      case('o'):
	 temp_file.set(optarg);
	 break;
      case('k'):
	 keep_temp=true;
	 break;
      case('?'):
      err:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->rewind();
   if(args->count()<=1) {
      eprintf(_("File name missed. "));
      goto err;
   }
   const char *file=args->getarg(1);
   if(!temp_file) {
      ParsedURL u(file);
      temp_file.set(basename_ptr(u.proto?u.path.get():file));
      // make temp file name by substituting node name and PID after the first dot.
      xstring temp_str;
      temp_str.setf("%s-%u.",get_nodename(),(unsigned)getpid());
      int point=temp_file.instr('.');
      temp_file.set_substr(point>=0?point+1:0,0,temp_str);
      temp_file.set_substr(0,0,"/");
      xstring temp_dir(dir_file(get_lftp_cache_dir(),"edit"));
      mkdir(temp_dir,0700);
      temp_file.set_substr(0,0,temp_dir);
      if(access(temp_file,F_OK)!=-1)
	 keep_temp=true;
   }
   EditJob *j=new EditJob(session->Clone(),file,temp_file);
   j->KeepTempFile(keep_temp);
   return j;
}

CMD(shell)
{
   Job *j;
   if(args->count()<=1)
      j=new SysCmdJob(0);
   else
   {
      xstring_ca a(args->Combine(1));
      j=new SysCmdJob(a);
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
   bool silent=false;
   const char *opts="+rf";

   bool rmdir = false;
   if(!strcmp(args->a0(),"rmdir"))
   {
      rmdir = true;
      opts="+f";
   }

   while((opt=args->getopt(opts))!=EOF)
   {
      switch(opt)
      {
      case('r'):
	 recursive=true;
	 break;
      case('f'):
	 silent=true;
	 break;
      case('?'):
      print_usage:
	 eprintf(_("Usage: %s %s[-f] files...\n"),args->a0(), rmdir? "":"[-r] ");
	 return 0;
      }
   }

   if(args->getcurr()==0)
      goto print_usage;

   rmJob *j=new rmJob(session->Clone(),args.borrow());

   if(recursive)
      j->Recurse();
   if(rmdir)
      j->Rmdir();

   if(silent)
      j->BeQuiet();

   return j;
}
CMD(mkdir)
{
   return new mkdirJob(session->Clone(),args.borrow());
}

#ifndef O_ASCII
# define O_ASCII 0
#endif

CMD(source)
{
   int opt;
   bool e=false;
   while((opt=args->getopt("+e"))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 e=true;
	 break;
      case('?'):
      usage:
	 // xgettext:c-format
	 eprintf(_("Usage: %s [-e] <file|command>\n"),args->a0());
	 return 0;
      }
   }
   if(args->getindex()>=args->count())
      goto usage;
   FDStream *f=0;
   if(e)
   {
      xstring_ca cmd(args->Combine(args->getindex()));
      f=new InputFilter(cmd);
   }
   else
      f=new FileStream(args->getarg(1),O_RDONLY|O_ASCII);
   // try to open the file to return error code if failed, as FileFeeder
   // cannot feed error codes.
   if(f->getfd()==-1)
   {
      if(f->error())
      {
	 fprintf(stderr,"%s: %s\n",args->a0(),f->error_text.get());
	 delete f;
	 return 0;
      }
   }
   parent->SetCmdFeeder(new FileFeeder(f));
   exit_code=0;
   return 0;
}

CMD(jobs)
{
   int opt;
   int v=1;
   bool recursion=true;
   while((opt=args->getopt("+vr"))!=EOF)
   {
      switch(opt)
      {
      case('v'):
	 v++;
	 break;
      case('r'):
	 recursion=false;
	 break;
      case('?'):
         // xgettext:c-format
	 eprintf(_("Usage: %s [-v] [-v] ...\n"),args->a0());
	 return 0;
      }
   }
   exit_code=0;
   args->back();
   const char *op=args->a0();
   const char *arg=args->getnext();
   xstring s("");
   if(!arg) {
      parent->top->FormatJobs(s,v);
   } else {
      for(; arg; arg=args->getnext()) {
	 if(!isdigit((unsigned char)*arg)) {
	    eprintf(_("%s: %s - not a number\n"),op,arg);
	    exit_code=1;
	    continue;
	 }
	 int n=atoi(arg);
	 Job *j=parent->FindJob(n);
	 if(!j) {
	    eprintf(_("%s: %d - no such job\n"),op,n);
	    exit_code=1;
	    continue;
	 }
	 if(recursion)
	    j->FormatOneJobRecursively(s,v);
	 else
	    j->FormatOneJob(s,v);
      }
   }
   if(exit_code)
      return 0;
   OutputJob *out=new OutputJob(output.borrow(), args->a0());
   Job *j=new echoJob(s,s.length(),out);
   return j;
}

CMD(cd)
{
   return parent->builtin_cd();
}

CMD(pwd)
{
   int opt;
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
   const char *url_c=session->GetConnectURL(flags);
   char *url=alloca_strdup(url_c);
   int len=strlen(url_c);
   url[len++]='\n';  // replaces \0

   OutputJob *out=new OutputJob(output.borrow(), args->a0());
   Job *j=new echoJob(url,len,out);

   return j;
}

CMD(exit)
{
   return parent->builtin_exit();
}

CMD(debug)
{
   const char *op=args->a0();
   int	 new_dlevel=DEFAULT_DEBUG_LEVEL;
   const char *debug_file_name=0;
   bool  enabled=true;
   bool	 show_pid=false;
   bool	 show_time=false;
   bool	 show_context=false;
   int	 trunc=0;

   int opt;
   while((opt=args->getopt("To:ptc"))!=EOF)
   {
      switch(opt)
      {
      case('T'):
	 trunc=O_TRUNC;
	 break;
      case('o'):
	 debug_file_name=optarg;
	 break;
      case 'p':
	 show_pid=true;
	 break;
      case 't':
	 show_time=true;
	 break;
      case 'c':
	 show_context=true;
	 break;
      case('?'):
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }

   const char *a=args->getcurr();
   if(a)
   {
      if(!strcasecmp(a,"off"))
      {
	 enabled=false;
      }
      else
      {
	 new_dlevel=atoi(a);
	 if(new_dlevel<0)
	    new_dlevel=0;
	 enabled=true;
      }
   }

   if(debug_file_name && trunc)
      if(truncate(debug_file_name,0) < 0)
         fprintf(stderr, "truncate failed: %s\n", strerror(errno));

   const char *c="debug";
   ResMgr::Set("log:file",c,debug_file_name?debug_file_name:"");

   ResMgr::Set("log:enabled",c,enabled?"yes":"no");
   if(enabled)
      ResMgr::Set("log:level",c,xstring::format("%d",new_dlevel));

   ResMgr::Set("log:show-pid",c,show_pid?"yes":"no");
   ResMgr::Set("log:show-time",c,show_time?"yes":"no");
   ResMgr::Set("log:show-ctx",c,show_context?"yes":"no");

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
   if(args->count()<2 || args->count()>3)
   {
      eprintf(_("Usage: %s <user|URL> [<pass>]\n"),args->getarg(0));
      return 0;
   }
   const char *user=args->getarg(1);
   const char *pass=args->getarg(2);
   bool insecure=(pass!=0);

   ParsedURL u(user,true);
   if(u.proto && !u.user)
   {
      exit_code=0;
      return 0;
   }
   if(u.proto && u.user && u.pass)
   {
      pass=u.pass;
      insecure=true;
   }
   if(!pass)
      pass=GetPass(_("Password: "));
   if(!pass)
      return 0;

   if(u.proto && u.user)
   {
      FA *s=FA::New(&u,false);
      if(s)
      {
	 s->SetPasswordGlobal(pass);
	 s->InsecurePassword(insecure);
	 SessionPool::Reuse(s);
      }
      else
      {
	 eprintf("%s: %s%s\n",args->a0(),u.proto.get(),
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
   if(!strcasecmp(args->getarg(1),"all"))
   {
      parent->KillAll();
      exit_code=0;
      return 0;
   }
   args->rewind();
   exit_code=0;
   for(;;)
   {
      const char *arg=args->getnext();
      if(arg==0)
	 break;
      if(!isdigit((unsigned char)arg[0]))
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
   const char *ac=args->getnext();
   if(ac==0)
   {
      xstring_ca s(ResMgr::Format(with_defaults,only_defaults));
      OutputJob *out=new OutputJob(output.borrow(), args->a0());
      Job *j=new echoJob(s,out);
      return j;
   }

   char *a=alloca_strdup(ac);
   char *sl=strchr(a,'/');
   char *closure=0;
   if(sl)
   {
      *sl=0;
      closure=sl+1;
   }

   const ResType *type;
   // find type of given variable
   const char *msg=ResMgr::FindVar(a,&type);
   if(msg)
   {
      eprintf(_("%s: %s. Use `set -a' to look at all variables.\n"),a,msg);
      return 0;
   }

   args->getnext();
   xstring_ca val(args->getcurr()==0?0:args->Combine(args->getindex()));
   msg=ResMgr::Set(a,closure,val);

   if(msg)
   {
      eprintf("%s: %s.\n",val.get(),msg);
      return 0;
   }
   exit_code=0;
   return 0;
}

CMD(alias)
{
   if(args->count()<2)
   {
      xstring_ca list(Alias::Format());
      OutputJob *out=new OutputJob(output.borrow(), args->a0());
      Job *j=new echoJob(list,out);
      return j;
   }
   else if(args->count()==2)
   {
      Alias::Del(args->getarg(1));
   }
   else
   {
      xstring_ca val(args->Combine(2));
      Alias::Add(args->getarg(1),val);
   }
   exit_code=0;
   return 0;
}

CMD(wait)
{
   const char *op=args->a0();
   if(args->count()>2)
   {
      eprintf(_("Usage: %s [<jobno>]\n"),op);
      return 0;
   }
   int n=-1;
   const char *jn=args->getnext();
   if(jn)
   {
      if(!strcasecmp(jn,"all"))
      {
	 parent->WaitForAllChildren();
	 parent->AllWaitingFg();
	 exit_code=0;
	 return 0;
      }
      if(!isdigit((unsigned char)jn[0]))
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
   if(Job::FindWhoWaitsFor(j)!=0)
   {
      eprintf(_("%s: some other job waits for job %d\n"),op,n);
      return 0;
   }
   if(j->Job::CheckForWaitLoop(parent))
   {
      eprintf(_("%s: wait loop detected\n"),op);
      return 0;
   }
   j->SetParent(0);
   j->Bg();
   return j;
}

CMD(subsh)
{
   CmdExec *e=new CmdExec(parent);

   const char *c=args->getarg(1);
   e->FeedCmd(c);
   e->FeedCmd("\n");
   e->cmdline.vset("(",c,")",NULL);
   return e;
}

CMD(mmv)
{
   static const struct option mmv_opts[]=
   {
      {"target-directory",required_argument,0,'O'},
      {"destination-directory",required_argument,0,'O'},
      {"remove-target-first",no_argument,0,'e'},
      {0}
   };

   bool remove_target=false;
   const char *target_dir=0;
   args->rewind();
   int opt;
   while((opt=args->getopt_long("eO:t:",mmv_opts,0))!=EOF)
   {
      switch(opt)
      {
      case('e'):
	 remove_target=true;
	 break;
      case('t'):
      case('O'):
	 target_dir=optarg;
	 break;
      case('?'):
      help:
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }
   if(!target_dir && args->count()>=3) {
      target_dir=args->getarg(args->count()-1);
      target_dir=alloca_strdup(target_dir);
      args->delarg(args->count()-1);
   }
   if(!target_dir || args->getindex()>=args->count()) {
      eprintf(_("Usage: %s [OPTS] <files> <target-dir>\n"),args->a0());
      goto help;
   }
   mmvJob *j=new mmvJob(session->Clone(),args,target_dir,FA::RENAME);
   if(remove_target)
      j->RemoveTargetFirst();
   return j;
}

CMD(mv)
{
   if(args->count()!=3
   || (args->count()==3 && last_char(args->getarg(2))=='/'))
   {
      args->setarg(0,"mmv");
      return cmd_mmv(parent);
   }
   Job *j=new mvJob(session->Clone(),args->getarg(1),args->getarg(2));
   return j;
}

CMD(ln)
{
   FA::open_mode m=FA::LINK;
   const char *op=args->a0();
   int c;
   while((c=args->getopt("+s"))!=EOF)
   {
      switch(c)
      {
      case 's':
	 m=FA::SYMLINK;
	 break;
      default:
      error:
	 eprintf(_("Try `help %s' for more information.\n"),op);
	 return 0;
      }
   }
   args->back();
   const char *file1=args->getnext();
   const char *file2=args->getnext();
   if(!file1 || !file2)
      goto error;

   return new mvJob(session->Clone(),file1,file2,m);
}

const char *const cache_subcmd[]={
   "status","flush","on","off","size","expire",
   NULL
};

CMD(cache)  // cache control
{
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
   if(!op || !strcasecmp(op,"status"))
      FileAccess::cache->List();
   else if(!strcasecmp(op,"flush"))
      FileAccess::cache->Flush();
   else if(!strcasecmp(op,"on"))
      ResMgr::Set("cache:enable",0,"yes");
   else if(!strcasecmp(op,"off"))
      ResMgr::Set("cache:enable",0,"no");
   else if(!strcasecmp(op,"size"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for size\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      const char *err=ResMgr::Set("cache:size",0,op);
      if(err)
      {
	 eprintf("%s: %s: %s\n",args->a0(),op,err);
	 exit_code=1;
	 return 0;
      }
   }
   else if(!strcasecmp(op,"expire"))
   {
      op=args->getnext();
      if(!op)
      {
	 eprintf(_("%s: Operand missed for `expire'\n"),args->a0());
	 exit_code=1;
	 return 0;
      }
      const char *err=ResMgr::Set("cache:expire",0,op);
      if(err)
      {
	 eprintf("%s: %s: %s\n",args->a0(),op,err);
	 exit_code=1;
	 return 0;
      }
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
      const char *a=args->getarg(1);
      if(!isdigit((unsigned char)a[0]))
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

bool CmdExec::print_cmd_help(const char *cmd)
{
   const cmd_rec *c;
   int part=find_cmd(cmd,&c);

   if(part==1)
   {
      if(c->creator==0 || !xstrcmp(c->long_desc,HELP_IN_MODULE)) {
	 // try to load the module which can have a help text
	 if(load_cmd_module(c->name))
	    find_cmd(c->name,&c);
	 else
	    return false;
      }
      if(c->long_desc==0 && c->short_desc==0)
      {
	 printf(_("Sorry, no help for %s\n"),cmd);
	 return true;
      }
      if(c->short_desc==0 && !strchr(c->long_desc,' '))
      {
	 printf(_("%s is a built-in alias for %s\n"),cmd,c->long_desc);
	 print_cmd_help(c->long_desc);
	 return true;
      }
      if(c->short_desc)
	 printf(_("Usage: %s\n"),_(c->short_desc));
      if(c->long_desc)
	 printf("%s",_(c->long_desc));
      return true;
   }
   const char *a=Alias::Find(cmd);
   if(a)
   {
      printf(_("%s is an alias to `%s'\n"),cmd,a);
      return true;
   }
   if(part==0)
      printf(_("No such command `%s'. Use `help' to see available commands.\n"),cmd);
   else
      printf(_("Ambiguous command `%s'. Use `help' to see available commands.\n"),cmd);
   return false;
}

void CmdExec::print_cmd_index()
{
   int i=0;
   const cmd_rec *cmd_table=dyn_cmd_table?dyn_cmd_table.get():static_cmd_table;
   const int count=dyn_cmd_table?dyn_cmd_table.count():static_cmd_table_length;
   int width=fd_width(1);
   int pos=0;
   const int align=37;
   const int first_align=4;
   while(i<count)
   {
      while(i<count && !cmd_table[i].short_desc)
	 i++;
      if(i>=count)
	 break;
      const char *c1=gettext(cmd_table[i].short_desc);
      i++;
      int w1=mbswidth(c1,0);

      int pad=0;
      if(pos<first_align)
	 pad=first_align-pos;
      else if(pos>first_align)
	 pad=align-(pos-first_align)%align;
      if(pos>first_align && pos+pad+w1>=width) {
	 printf("\n");
	 pos=0;
	 pad=first_align;
      }

      printf("%*s%s",pad,"",c1);
      pos+=pad+w1;
   }
   if(pos>0)
      printf("\n");
}

CMD(help)
{
   if(args->count()>1)
   {
      exit_code=0;
      for(;;)
      {
	 const char *cmd=args->getnext();
	 if(cmd==0)
	    break;
	 if(!parent->print_cmd_help(cmd))
	    exit_code=1;
      }
      return 0;
   }

   parent->print_cmd_index();

   exit_code=0;
   return 0;
}

CMD(ver)
{
   printf(
      _("LFTP | Version %s | Copyright (c) 1996-%d Alexander V. Lukyanov\n"),VERSION,2020);
   printf("\n");
   printf(
      _("LFTP is free software: you can redistribute it and/or modify\n"
      "it under the terms of the GNU General Public License as published by\n"
      "the Free Software Foundation, either version 3 of the License, or\n"
      "(at your option) any later version.\n"
      "\n"
      "This program is distributed in the hope that it will be useful,\n"
      "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
      "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
      "GNU General Public License for more details.\n"
      "\n"
      "You should have received a copy of the GNU General Public License\n"
      "along with LFTP.  If not, see <http://www.gnu.org/licenses/>.\n"));
   printf("\n");
   printf(
      _("Send bug reports and questions to the mailing list <%s>.\n"),"lftp@uniyar.ac.ru");

#if defined(HAVE_DLOPEN) && defined(RTLD_DEFAULT)
   /* Show some of loaded libraries. Modules can load those libraries on
      demand so use dlsym to avoid linking with them just for showing version. */
   printf("\n");
   const char *msg=_("Libraries used: ");
   int mbflags=0;
   int col=mbswidth(msg,mbflags);
   int width=parent->status_line?parent->status_line->GetWidth():80;
   printf("%s",msg);

   struct VersionInfo
   {
      const char *lib_name;
      const char *symbol;
      enum type_t { STRING_OR_PTR, STRING_PTR, FUNC0, INT8_8 } type;
      const char *skip_prefix;
      typedef const char *(*func0)(void *);
      const char *query() const
	 {
	    int v;
	    void *sym_ptr=dlsym(RTLD_DEFAULT,symbol);
	    if(!sym_ptr)
	       return 0;
	    const char *str=0;
	    switch(type)
	    {
	    case STRING_OR_PTR:
	       str=(const char*)sym_ptr;
	       if(skip_prefix && !strncmp(str,skip_prefix,sizeof(char*)))
		  break;
	       // FALLTHROUGH
	    case STRING_PTR:
	       str=*(const char**)sym_ptr;
	       break;
	    case FUNC0:
	       str=((func0)sym_ptr)(NULL);
	       break;
	    case INT8_8:
	       v=*(int*)sym_ptr;
	       str=xstring::format("%d.%d",(v>>8)&255,v&255);
	    }
	    if(!str)
	       return 0;
	    if(skip_prefix && !strncmp(str,skip_prefix,strlen(skip_prefix)))
	       str+=strlen(skip_prefix);
	    return str;
	 }
   }
   static const libs[]=
   {
      {"Expat",	     "XML_ExpatVersion",     VersionInfo::FUNC0,     "expat_"},
      {"GnuTLS",     "gnutls_check_version", VersionInfo::FUNC0,     0},
      {"idn2",	     "idn2_check_version",   VersionInfo::FUNC0,     0},
      {"libiconv",   "_libiconv_version",    VersionInfo::INT8_8,    0},
      {"OpenSSL",    "SSL_version_str",      VersionInfo::STRING_OR_PTR,"OpenSSL "},
      {"Readline",   "rl_library_version",   VersionInfo::STRING_PTR,0},
      {"zlib",	     "zlibVersion",	     VersionInfo::FUNC0,     0},
      {0}
   };

   bool need_comma=false;
   for(const VersionInfo *scan=libs; scan->lib_name; scan++)
   {
      const char *v=scan->query();
      if(!v)
	 continue;
      char buf[256];
      snprintf(buf,sizeof(buf),", %s %s",scan->lib_name,v);
      int skip=need_comma?0:2;
      int w=mbswidth(buf+skip,mbflags);
      if(col+w>=width && need_comma)
      {
	 buf[1]='\n';
	 col=w-2;
      }
      else
	 col+=w;
      printf("%s",buf+skip);
      need_comma=true;
   }
   printf("\n");
#endif // HAVE_DLOPEN

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
   {"add","delete","list","list-p","edit","import",0};
static ResDecl res_save_passwords
   ("bmk:save-passwords","no",ResMgr::BoolValidate,0);

CMD(bookmark)
{
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

   if(!strcasecmp(op,"list") || !strcasecmp(op,"list-p"))
   {
      xstring_ca list(op[4]?lftp_bookmarks.Format():lftp_bookmarks.FormatHidePasswords());
      OutputJob *out=new OutputJob(output.borrow(), args->a0());
      Job *j=new echoJob(list,out);
      return j;
   }
   else if(!strcasecmp(op,"add"))
   {
      const char *key=args->getnext();
      if(key==0 || key[0]==0)
	 eprintf(_("%s: bookmark name required\n"),args->a0());
      else
      {
	 const char *value=args->getnext();
	 int flags=0;
	 if(res_save_passwords.QueryBool(session->GetHostName()))
	    flags|=session->WITH_PASSWORD;
	 if(value==0)
	 {
	    value=session->GetConnectURL(flags);
	    // encode some more characters, special to CmdExec parser.
	    value=url::encode(value,"&;|\"'\\");
	 }
	 if(value==0 || value[0]==0)
	    value="\"\"";
	 if(strchr(key,' ') || strchr(key,'\t'))
	 {
	    eprintf(_("%s: spaces in bookmark name are not allowed\n"),args->a0());
	    return 0;
	 }
	 lftp_bookmarks.Add(key,value);
	 exit_code=0;
      }
   }
   else if(!strcasecmp(op,"delete"))
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
   else if(!strcasecmp(op,"edit"))
   {
      lftp_bookmarks.Remove(""); // force bookmark file creation

      const char *bin=getenv("EDITOR");
      if (bin==NULL)
         bin="vi";
      xstring cmd(bin);
      cmd.append(" ");
      cmd.append(shell_encode(lftp_bookmarks.GetFilePath()));
      parent->PrependCmd(xstring::get_tmp("shell ").append_quoted(cmd));
   }
   else if(!strcasecmp(op,"import"))
   {
      op=args->getnext();
      if(!op)
	 eprintf(_("%s: import type required (netscape,ncftp)\n"),args->a0());
      else
      {
	 parent->PrependCmd(xstring::cat("shell " PKGDATADIR "/import-",op,"\n",NULL));
	 exit_code=0;
      }
   }
   else if(!strcasecmp(op,"load"))
   {
      lftp_bookmarks.UserLoad();
      exit_code=0;
   }
   else if(!strcasecmp(op,"save"))
   {
      lftp_bookmarks.UserSave();
      exit_code=0;
   }
   return 0;
}

CMD(echo)
{
   xstring s;
   args->CombineTo(s,1);
   if(args->count()>1 && !strcmp(args->getarg(1),"-n"))
   {
      if(s.length()<=3)
      {
	 exit_code=0;
	 return 0;
      }
      s.set_substr(0,3,"");
   }
   else
   {
      s.append('\n');
   }

   OutputJob *out=new OutputJob(output.borrow(), args->a0());
   Job *j=new echoJob(s,s.length(),out);
   return j;
}

CMD(suspend)
{
   kill(getpid(),SIGSTOP);
   return 0;
}

CMD(find)
{
   static struct option find_options[]=
   {
      {"maxdepth",required_argument,0,'d'},
      {"ls",no_argument,0,'l'},
      {0,0,0,0}
   };
   int opt;
   int maxdepth = -1;
   bool long_listing=false;
   const char *op=args->a0();

   while((opt=args->getopt_long("+d:l",find_options))!=EOF)
   {
      switch(opt)
      {
      case 'd':
	 if(!isdigit((unsigned char)*optarg))
	 {
	    eprintf(_("%s: %s - not a number\n"),op,optarg);
	    return 0;
	 }
	 maxdepth = atoi(optarg);
	 break;
      case 'l':
	 long_listing=true;
	 break;
      case '?':
	 eprintf(_("Usage: %s [-d #] dir\n"),op);
	 return 0;
      }
   }

   if(!args->getcurr())
      args->Append(".");
   FinderJob_List *j=new class FinderJob_List(session->Clone(),args.borrow(),output.borrow());
   j->set_maxdepth(maxdepth);
   j->DoLongListing(long_listing);
   return j;
}

CMD(du)
{
   enum {
      OPT_BLOCK_SIZE,
      OPT_EXCLUDE
   };
   static struct option du_options[]=
   {
      {"all",no_argument,0,'a'},
      /* alias: both GNU-like max-depth and lftp-like maxdepth;
       * only document one of them. */
      {"bytes",no_argument,0,'b'},
      {"block-size",required_argument,0,OPT_BLOCK_SIZE},
      {"maxdepth",required_argument,0,'d'},
      {"total",no_argument,0,'c'},
      {"max-depth",required_argument,0,'d'},
      {"files",no_argument,0,'F'},
      {"human-readable",no_argument,0,'h'},
      {"si",no_argument,0,'H'},
      {"kilobytes",required_argument,0,'k'},
      {"megabytes",required_argument,0,'m'},
      {"separate-dirs",no_argument,0,'S'},
      {"summarize",no_argument,0,'s'},
      {"exclude",required_argument,0,OPT_EXCLUDE},
      {0,0,0,0}
   };
   int maxdepth = -1;
   bool max_depth_specified = false;
   int blocksize = 1024;
   bool separate_dirs = false;
   bool summarize_only = false;
   bool print_totals=false;
   bool all_files=false;
   bool file_count=false;
   Ref<PatternSet> exclude;
   int human_opts=0;

   exit_code=1;

   const char *op=args->a0();

   int opt;
   while((opt=args->getopt_long("+abcd:FhHkmsS",du_options))!=EOF)
   {
      switch(opt)
      {
      case 'a':
	 all_files=true;
	 break;
      case 'b':
	 blocksize = 1;
	 human_opts = 0;
	 break;
      case 'c':
	 print_totals=true;
	 break;
      case 'd':
	 if(!isdigit((unsigned char)*optarg))
	 {
	    eprintf(_("%s: %s - not a number\n"),op,optarg);
	    return 0;
	 }
	 maxdepth = atoi(optarg);
	 max_depth_specified = true;
	 break;
      case 'F':
	 file_count=true;
	 break;
      case 'h':
	 human_opts |= human_autoscale|human_SI|human_base_1024;
	 break;
      case 'H':
	 human_opts |= human_autoscale|human_SI;
	 break;
      case 'k': /* the default; here for completeness */
	 blocksize = 1024;
	 human_opts = 0;
	 break;
      case 'm':
	 blocksize = 1024*1024;
	 human_opts = 0;
	 break;
      case 's':
	 summarize_only = true;
	 break;
      case 'S':
	 separate_dirs = true;
	 break;
      case OPT_BLOCK_SIZE:
	 blocksize = atoi(optarg);
	 if(blocksize == 0)
	 {
	    eprintf(_("%s: invalid block size `%s'\n"),op,optarg);
	    return 0;
	 }
	 break;
      case OPT_EXCLUDE:
	 if(!exclude)
	    exclude=new PatternSet();
	 exclude->Add(PatternSet::EXCLUDE,new PatternSet::Glob(optarg));
	 break;
      case '?':
      default:
	 eprintf(_("Usage: %s [options] <dirs>\n"),op);
	 return 0;
      }
   }

   if (summarize_only && max_depth_specified && maxdepth == 0)
      eprintf(_("%s: warning: summarizing is the same as using --max-depth=0\n"), op);

   if (summarize_only && max_depth_specified && maxdepth != 0)
   {
      eprintf(_("%s: summarizing conflicts with --max-depth=%i\n"), op, maxdepth);
      return 0;
   }

   /* It doesn't really make sense to show all files when doing a file count.
    * We might have -a in an alias as defaults, so let's just silently turn
    * it off.  (I'm not sure if we should do this for summarize_only and
    * max_depth_specified, too.) */
   if (file_count && all_files)
      all_files=false;
   if (file_count)
      blocksize=1;

   exit_code=0;

   if (summarize_only)
      maxdepth = 0;

   if(!args->getcurr())
      args->Append(".");
   FinderJob_Du *j=new class FinderJob_Du(session->Clone(),args.borrow(),output.borrow());
   j->PrintDepth(maxdepth);
   j->SetBlockSize(blocksize,human_opts);
   if(print_totals)
      j->PrintTotals();
   if(all_files)
      j->AllFiles();
   if(separate_dirs)
      j->SeparateDirs();
   if(file_count)
      j->FileCount();
   /* if separate_dirs is on, then there's no point in traversing past
    * max_print_depth at all */
   if(separate_dirs && maxdepth != -1)
      j->set_maxdepth(maxdepth);
   if(exclude)
      j->SetExclude(exclude.borrow());
   return j;
}

CMD(command)
{
   if(args->count()<2)
   {
      eprintf(_("Usage: %s command args...\n"),args->a0());
      return 0;
   }
   args->delarg(0);
   return parent->builtin_restart();
}

CMD(module)
{
   const char *op=args->a0();
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
      eprintf("%s: %s\n",args->a0(),_("cannot get current directory"));
      return 0;
   }
   const char *name=parent->cwd->GetName();
   const char *buf=xstring::cat(name?name:"?","\n",NULL);
   Job *j=new echoJob(buf,new OutputJob(output.borrow(), args->a0()));
   return j;
}

CMD(local)
{
   return parent->builtin_local();
}

CMD(glob)
{
   return parent->builtin_glob();
}

CMD(chmod)
{
   ChmodJob::verbosity verbose = ChmodJob::V_NONE;
   bool recurse = false, quiet = false;

   static struct option chmod_options[]=
   {
      {"verbose",no_argument,0,'v'},
      {"changes",no_argument,0,'c'},
      {"recursive",no_argument,0,'R'},
      {"silent",no_argument,0,'f'},
      {"quiet",no_argument,0,'f'},
      {0,0,0,0}
   };
   int opt;
   int modeind = 0;

   while((opt=args->getopt_long("vcRfrwxXstugoa,+-=",chmod_options))!=EOF)
   {
      switch(opt)
      {
      case 'r': case 'w': case 'x':
      case 'X': case 's': case 't':
      case 'u': case 'g': case 'o':
      case 'a':
      case ',':
      case '+': case '=':
	 modeind = optind?optind-1:1;
	 break; /* mode string that begins with - */

      case 'v':
	 verbose=ChmodJob::V_ALL;
	 break;
      case 'c':
	 verbose=ChmodJob::V_CHANGES;
	 break;
      case 'R':
	 recurse = true;
	 break;
      case 'f':
	 quiet = true;
	 break;

      case '?':
      usage:
	 eprintf(_("Usage: %s [OPTS] mode file...\n"),args->a0());
	 return 0;
      }
   }

   if(modeind == 0)
      modeind = args->getindex();

   const char *arg = args->getarg(modeind);
   if(!arg)
      goto usage;
   arg = alloca_strdup(arg);
   args->delarg(modeind);

   if(!args->getcurr())
      goto usage;

   mode_change *m = mode_compile(arg);
   if(!m)
   {
      eprintf(_("invalid mode string: %s\n"), arg);
      return 0;
   }

   ChmodJob *j=new ChmodJob(session->Clone(),args.borrow());
   j->SetVerbosity(verbose);
   j->SetMode(m);
   if(quiet)
      j->BeQuiet(); /* does not affect messages from Verbosity */
   if(recurse)
      j->Recurse();
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
      {"source-region",required_argument,0,256+'r'},
      {"target-position",required_argument,0,256+'R'},
      {"continue",no_argument,0,'c'},
      {"output",required_argument,0,'o'},
      {"remove-source-later",no_argument,0,'E'},
      {"remove-target-first",no_argument,0,'e'},
      {"make-target-dir",no_argument,0,'d'},
      {"quiet",no_argument,0,'q'},
      {0,0,0,0}
   };
   int opt;
   const char *src=0;
   const char *dst=0;
   bool cont=false;
   bool ascii=false;
   bool quiet=false;
   bool do_mkdir=false;
   long long source_region_begin=0,source_region_end=FILE_END;
   long long target_region_begin=0,target_region_end=FILE_END;
   int n,p;

   while((opt=args->getopt_long("arco:d",get1_options))!=EOF)
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
      case 256+'r':
	 source_region_end=FILE_END;
	 n=sscanf(optarg,"%lld%n-%lld",&source_region_begin,&p,&source_region_end);
	 if(n<1 || (n==1 && (optarg[p] && (optarg[p]!='-' || optarg[p+1]))))
	 {
	    eprintf("%s\n",_("Invalid range format. Format is min-max, e.g. 10-20."));
	    goto usage;
	 }
	 break;
      case 256+'R':
	 target_region_end=FILE_END;
	 n=sscanf(optarg,"%lld",&target_region_begin);
	 if(n<1)
	 {
	    eprintf("%s\n",_("Invalid range format. Format is min-max, e.g. 10-20."));
	    goto usage;
	 }
	 break;
      case('q'):
	 quiet=true;
	 break;
      case('d'):
	 do_mkdir=true;
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

   bool auto_rename=false;
   if(dst==0 || dst[0]==0)
   {
      dst=basename_ptr(src);
      auto_rename=true;
   }
   else
   {
      if(last_char(dst)=='/' && basename_ptr(dst)[0]!='/')
      {
	 const char *bn=basename_ptr(src);
	 if(bn[0]!='/')
	 {
	    dst=xstring::get_tmp(dst).append(bn);
	    auto_rename=true;
	 }
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
	    dst=xstring::cat(dst,"/",slash,NULL);
	 }
      }
   }

   dst=alloca_strdup(dst); // save tmp xstring

   FileCopyPeer *src_peer=0;
   FileCopyPeer *dst_peer=0;

   src_peer=FileCopyPeerFA::New(session->Clone(),src,FA::RETRIEVE);
   if(!cont && (source_region_begin>0 || source_region_end!=FILE_END))
      src_peer->SetRange(source_region_begin,source_region_end);

   if(dst_url.proto==0)
      dst_peer=FileCopyPeerFDStream::NewPut(dst,cont||target_region_begin>0);
   else
      dst_peer=new FileCopyPeerFA(&dst_url,FA::STORE);
   dst_peer->AutoRename(auto_rename && ResMgr::QueryBool("xfer:auto-rename",0));
   if(!cont && (target_region_begin>0 || target_region_end!=FILE_END))
      dst_peer->SetRange(target_region_begin,target_region_end);
   if(do_mkdir)
      dst_peer->MakeTargetDir();

   FileCopy *c=FileCopy::New(src_peer,dst_peer,cont);

   if(ascii)
      c->Ascii();

   CopyJob *cj=new CopyJob(c,src,args->a0());
   cj->Quiet(quiet);
   return cj;
}

CMD(slot)
{
   const char *n=args->getarg(1);
   if(n)
   {
      parent->ChangeSlot(n);
      exit_code=0;
      return 0;
   }
   else
   {
      xstring_ca slots(ConnectionSlot::Format());
      Job *j=new echoJob(slots,new OutputJob(output.borrow(),args->a0()));
      return j;
   }
}

CMD(tasks)
{
   printf("task_count=%d\n",SMTask::TaskCount());
   SMTask::PrintTasks();
   exit_code=0;
   return 0;
}

CMD(empty)
{
   exit_code=(args->count()>1 ? 1 : 0);
   return 0;
}
CMD(notempty)
{
   exit_code=(args->count()>1 ? 0 : 1);
   return 0;
}
CMD(true)
{
   exit_code=0;
   return 0;
}
CMD(false)
{
   exit_code=1;
   return 0;
}

CMD(eval)
{
   int opt;
   const char *fmt=0;
   const char *op=args->getarg(0);
   while((opt=args->getopt("+f:"))!=EOF)
   {
      switch(opt)
      {
      case 'f':
	 fmt=optarg;
	 break;
      default:
	 eprintf(_("Try `%s --help' for more information\n"),op);
	 return 0;
      }
   }
   int base=optind;
   xstring cmd;
   if(!fmt)
      args->CombineTo(cmd,optind);
   else
   {
      while(*fmt)
      {
	 if(*fmt=='\\' && (fmt[1]=='$' || fmt[1]=='\\'))
	 {
	    cmd.append(fmt[1]);
	    fmt+=2;
	    continue;
	 }
	 if(*fmt=='$' && fmt[1]>='0' && fmt[1]<='9')
	 {
	    int n=fmt[1]-'0';
	    if(n+base<args->count())
	       cmd.append(args->getarg(n+base));
	    fmt+=2;
	    continue;
	 }
	 if(*fmt=='$' && fmt[1]=='@')
	 {
	    xstring_ca c(args->CombineQuoted(base));
	    cmd.append(c);
	    fmt+=2;
	    continue;
	 }
	 if(*fmt=='$' && fmt[1]=='$')
	 {
	    cmd.appendf("%d",(int)getpid());
	    fmt+=2;
	    continue;
	 }
	 cmd.append(*fmt++);
      }
   }
   cmd.append(";\n\n");
   parent->PrependCmd(cmd);
   exit_code=parent->prev_exit_code;
   return 0;
}
