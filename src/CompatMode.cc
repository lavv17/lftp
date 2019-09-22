/*
 * compat mode for lftp
 *
 * Copyright (c) 2005 by Petr Ostadal (postadal@suse.cz)
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


#include <config.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <pwd.h>
#include "MirrorJob.h"
#include "CmdExec.h"
#include "rmJob.h"
#include "mkdirJob.h"
#include "ChmodJob.h"
#include "misc.h"
#include "plural.h"
#include "getopt.h"
#include "FindJob.h"
#include "url.h"
#include "CopyJob.h"
#include "pgetJob.h"

#include "modconfig.h"

#include <fcntl.h>
#include <termios.h>

#include "xmalloc.h"
#include "GetPass.h"
#include "CharReader.h"
#include "SignalHook.h"
#include "Job.h"


int ascii_mode;

char* GetText(const char *prompt) {
   static char *oldtext=0;
   static int tty_fd=-2;
   static FILE *f=0;

   xfree(oldtext);
   oldtext=0;

   if(tty_fd==-2)
   {
      if(isatty(0))
	 tty_fd=0;
      else
      {
	 tty_fd=open("/dev/tty",O_RDONLY);
   	 if(tty_fd!=-1)
	    fcntl(tty_fd,F_SETFD,FD_CLOEXEC);
      }
   }
   if(tty_fd==-1)
      return 0;

   if(f==0)
      f=fdopen(tty_fd,"r");
   if(f==0)
      return 0;

   write(tty_fd,prompt,strlen(prompt));
   oldtext=readline_from_file(fileno(f));
   return oldtext;
}

CMD(ascii)
{
    ascii_mode = 1;
    return NULL;
}

CMD(bin)
{
    ascii_mode = 0;
    return NULL;
}

CMD(type) 
{
    if (parent->args->count() == 2) {
        if (strcmp(parent->args->getarg(1), "binary") == 0)
            ascii_mode = 0;
        else if (strcmp(parent->args->getarg(1), "ascii") == 0)
            ascii_mode = 1;
        else
            parent->eprintf(_("Try `help %s' for more information.\n"), parent->args->a0());
    } else if (parent->args->count() == 1) {
        if (ascii_mode)
            parent->printf("Using ascii mode to transfer files.\n");
        else
            parent->printf("Using binary mode to transfer files.\n");
    }
    else
        parent->eprintf(_("Try `help %s' for more information.\n"), parent->args->a0());
    return NULL;
}

CMD(user);

CMD(compat_user)
{
    char *user;

    if(parent->args->count() == 1) {
        user = GetText("(username) ");
        
        if (!user || strlen(user) == 0) {
            parent->eprintf(_("Try `help %s' for more information.\n"), parent->args->a0());
            return NULL;
        }
        user = strdup(user);
        parent->args->Add(user);
    }
    
    return cmd_user(parent);
}

CMD(compat_open)
{
    const char *myname = getlogin();
    struct passwd *pw;
    char *name = NULL;
    char *prompt = NULL;
    char *cmd;
    int len = 0;
    ascii_mode = 0;
    Job *job;
    int n;
    
    if (parent->args->count() == 3)
        parent->args->insarg(2, "-p");
    else if (parent->args->count() != 2) {
        parent->eprintf(_("Try `help %s' for more information.\n"), parent->args->a0());
        return NULL;
    }
    
    if (myname == NULL && (pw = getpwuid(getuid())) != NULL)
        myname = pw->pw_name;
    if (myname) {
        len = strlen(myname) + 10;
        prompt = (char*)malloc(len);
        if (len <= snprintf(prompt, len, "Name (%s): ", myname))
            prompt[len - 1] = '\0';
        name = GetText(prompt);
        name = strdup(name && strlen(name) > 0 ? name : myname);
        free(prompt);
    }
    else {
        name = GetText("Name: ");
        if (name) strdup (name);
    }
    
            
    if (name != NULL && strlen(name) == 0) {
        free(name);
        name = NULL;
    }

    if (name) {
        len = strlen(name) + 11;
        cmd = (char*)malloc(len);

        if (len <= snprintf(cmd, len, "lftp-user %s", name))
            cmd[len - 1] = '\0';

        parent->PrependCmd(cmd);

        free(name);
        free(cmd);
    }
    job = parent->builtin_open();
    n = job->Do();
    
    return job;
}

CMD(get);

CMD(compat_get)
{
    if (ascii_mode && parent->args->count() > 1)
        parent->args->insarg(1, "-a");
    
    return cmd_get(parent);
}

CMD(get1);

CMD(compat_get1)
{
    if (ascii_mode && parent->args->count() > 1)
        parent->args->insarg(1, "-a");
    
    return cmd_get1(parent);
}

void module_init()
{
    ascii_mode = 0;

    CmdExec::RegisterCompatCommand("user", cmd_compat_user, "user <username> [<pass>]", "send new user information (only for backward compatibility, use lftp-user instead)\n");
    CmdExec::RegisterCompatCommand("open", cmd_compat_open, "open <site> [<port]", "connect to remote ftp server (only for backward compatibility, use lftp-open instead)\n");
    
    CmdExec::RegisterCompatCommand("get", cmd_compat_get);
    CmdExec::RegisterCompatCommand("mget", cmd_compat_get);
    CmdExec::RegisterCompatCommand("put", cmd_compat_get);
    CmdExec::RegisterCompatCommand("mput", cmd_compat_get);
    CmdExec::RegisterCompatCommand("get1", cmd_compat_get1);
    
    
    CmdExec::RegisterCommand("ascii", cmd_ascii, "ascii", "set ascii transfer type\n");
    CmdExec::RegisterCommand("binary", cmd_bin, "binary", "set binary transfer type\n");
    CmdExec::RegisterCommand("type", cmd_type, "type [ascii|binary]", "set file transfer type\n");
}
