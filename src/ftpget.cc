/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdlib.h>
#include "xstring.h"
#include <unistd.h>
#include <errno.h>
#include "getopt.h"
#include "GetJob.h"
#include "LsJob.h"
#include "GetPass.h"
#include "log.h"
#include "FileAccess.h"
#include "Resolver.h"
#include "SignalHook.h"
#include "ResMgr.h"
#include "modconfig.h"
#ifndef MODULE_PROTO_FTP
# include "ftpclass.h"
#endif
#ifdef HAVE_LOCALE_H
# include <locale.h>
#endif

#include "confpaths.h"

char  *program;

const char version_string[]="FtpGet version " VERSION;

void  PrintUsage(int p)
{
   if(p)
   {
      printf(_("FtpGet | Version %s | Copyright (C) 1996-1999 Alexander V. Lukyanov\n"),VERSION);
      // xgettext:c-format
      printf(_("This is free software with ABSOLUTELY NO WARRANTY. See COPYING for details.\n"));
   }
   printf(_("Usage: ftpget [OPTIONS] host filename [-o local] [filename...]\n"
	  "\n"
	  "-p  --port         set port number\n"
	  "-u  --user         login as user using pass as password\n"
	  "-l  --list         get listing of specified directory(ies)\n"
	  "-c  --continue     reget specified file(s)\n"
	  "-q  --quiet        quiet (no output)\n"
	  "-v  --verbose      verbose (lots of output)\n"
	  "    --async-mode   use asynchronous mode (faster)\n"
	  "    --sync-mode    use synchronous mode (compatible with bugs)\n"
	  "\n"
	  "-o  output to local file `local' (default - base name of filename)\n")
      );
   exit(1);
}

int   main(int argc,char **argv)
{
#ifdef SOCKS
   SOCKSinit(argv[0]);
#endif

   char  *port=0;
   int   c;
   char  *user=NULL;
   char  *pass=NULL;
   extern char *optarg;
   extern int  optind;
   int   list=0;
   int   quiet=0;
   int   verbose=0;
   int   cont=0;
   char  *host_name;

   enum
   {
      NOREST_MODE_OPT=256,
      SYNC_MODE_OPT,
      ASYNC_MODE_OPT,
      HELP_OPT,
      VERSION_OPT
   };

   static struct option ftpget_options[]=
   {
      {"help",no_argument,0,HELP_OPT},
      {"version",no_argument,0,VERSION_OPT},
      {"sync-mode",no_argument,0,SYNC_MODE_OPT},
      {"async-mode",no_argument,0,ASYNC_MODE_OPT},
      {"continue",no_argument,0,'c'},
      {"user",required_argument,0,'u'},
      {"port",required_argument,0,'p'},
      {"list",no_argument,0,'l'},
      {"quiet",no_argument,0,'q'},
      {"verbose",no_argument,0,'v'},
      {0,0,0,0}
   };

   setlocale (LC_ALL, "");
   bindtextdomain (PACKAGE, LOCALEDIR);
   textdomain (PACKAGE);

   ResMgr::ClassInit(); // resources must be inited before other classes
   SignalHook::ClassInit();
   Resolver::ClassInit();

#ifndef MODULE_PROTO_FTP
   Ftp::ClassInit();
#endif

   program=argv[0];

   FileAccess *f=FileAccess::New("ftp");

   while((c=getopt_long(argc,argv,"+p:u:lqvc",ftpget_options,0))!=-1)
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
	 {
	    pass=GetPass(_("Password: "));
	 }
	 else
	 {
	    *pass=0;
	    pass++;
         }
	 break;
      case('l'):
         list=1;
         break;
      case ('q'):
	 quiet=1;
	 break;
      case ('v'):
	 verbose=1;
	 break;
      case('c'):
         cont=1;
         break;
      case(SYNC_MODE_OPT):
	 ResMgr::Set("ftp:sync-mode","yes",0);
	 break;
      case(ASYNC_MODE_OPT):
	 ResMgr::Set("ftp:sync-mode","no",0);
	 break;
      case(HELP_OPT):
	 PrintUsage(0);
	 return 0;
      case(VERSION_OPT):
	 puts(version_string);
	 return 0;
      case('?'):
         fprintf(stderr,_("Try `%s --help' for more information\n"),program);
	 exit(1);
      }
   }

   if(optind+2>argc)
      PrintUsage(1);

   Log::global=new Log();
   if(verbose)
   {
      Log::global->SetOutput(2,false);
      Log::global->SetLevel(5);
      Log::global->Enable();
   }

   f->Connect(host_name=argv[optind++],port);
   if(user)
      f->Login(user,pass);

   ArgV *args=new ArgV;
   Job *j;

   if(list)
   {
      ArgV *rest=new ArgV(argc-optind+1,argv+optind-1);
      rest->setarg(0,"ls");
      j=new LsJob(f,0,rest);
   }
   else
   {
      args->Append(cont?"reget":"get");
      for( ; optind<argc; optind++)
      {
	 args->Append(argv[optind]);
	 char *local=0;
	 if(optind+2<argc && !strcmp(argv[optind+1],"-o"))
	 {
	    optind+=2;
	    local=argv[optind];
	 }
	 else
	 {
	    local=strrchr(argv[optind],'/');
	    if(!local)
	       local=argv[optind];
	    else
	       local++;
	 }
	 args->Append(local);
      }
      j=new GetJob(f,args,cont);
   }
   StatusLine *s=new StatusLine(1);
   for(;;)
   {
      SMTask::Schedule();
      if(!quiet)
	j->ShowRunStatus(s);
      if(j->Done())
	 break;
      SMTask::Block();
   }
   if(!quiet)
     j->SayFinal();
   return(j->ExitCode());
}
