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

/* Based on sshpty.c from openssh written by Tatu Ylonen <ylo@cs.hut.fi> */

#include <config.h>

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef HAVE_TERMIOS_H
# include <termios.h>
#endif
#ifdef HAVE_UTIL_H
# include <util.h>
#endif
#ifdef HAVE_PTY_H
# include <pty.h>
#endif
#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h>
#endif

#include "lftp_pty.h"

const char *open_pty(int *ptyfd, int *ttyfd)
{
   const char *name=0;
   void *old_sigchld=signal(SIGCHLD,SIG_DFL);
   *ptyfd=*ttyfd=-1;

#if defined(HAVE_OPENPTY) || defined(BSD4_4)
   /* openpty(3) exists in OSF/1 and some other os'es */
   openpty(ptyfd, ttyfd, NULL, NULL, NULL);
#else /* HAVE_OPENPTY */
#ifdef HAVE__GETPTY
   /*
    * _getpty(3) exists in SGI Irix 4.x, 5.x & 6.x -- it generates more
    * pty's automagically when needed
    */
   name=_getpty(ptyfd,O_RDWR|O_NOCTTY,0600,0);
#else /* HAVE__GETPTY */
#if defined(HAVE_DEV_PTMX)
   /*
    * This code is used e.g. on Solaris 2.x.  (Note that Solaris 2.3
    * also has bsd-style ptys, but they simply do not work.)
    */
   *ptyfd=open("/dev/ptmx",O_RDWR|O_NOCTTY);
   if(*ptyfd<0)
      goto fail;
   if(grantpt(*ptyfd)<0)
      goto fail;
   if(unlockpt(*ptyfd)<0)
      goto fail;
   name=ptsname(*ptyfd);
#else /* HAVE_DEV_PTMX */
#ifdef HAVE_DEV_PTS_AND_PTC
{
   /* AIX-style pty code. */
   *ptyfd=open("/dev/ptc",O_RDWR|O_NOCTTY);
   if(*ptyfd<0)
      goto fail;
   name=ttyname(*ptyfd);
}
#else /* HAVE_DEV_PTS_AND_PTC */
{
   /* BSD-style pty code. */
   char master[64];
   static char slave[64];
   int i;
   const char *ptymajors="pqrstuvwxyzabcdefghijklmnoABCDEFGHIJKLMNOPQRSTUVWXYZ";
   const char *ptyminors="0123456789abcdef";
   int num_minors=strlen(ptyminors);
   int num_ptys=strlen(ptymajors)*num_minors;

   for(i=0; i<num_ptys; i++)
   {
      sprintf(master,"/dev/pty%c%c",ptymajors[i/num_minors],ptyminors[i%num_minors]);
      sprintf(slave, "/dev/tty%c%c",ptymajors[i/num_minors],ptyminors[i%num_minors]);
      *ptyfd=open(master,O_RDWR|O_NOCTTY);
      if(*ptyfd>=0)
	 break;
      /* Try SCO style naming */
      sprintf(master,"/dev/ptyp%d",i);
      sprintf(slave, "/dev/ttyp%d",i);
      *ptyfd=open(master,O_RDWR|O_NOCTTY);
      if(*ptyfd>=0)
	 break;
   }
   name=slave;
}
#endif /* HAVE_DEV_PTS_AND_PTC */
#endif /* HAVE_DEV_PTMX */
#endif /* HAVE__GETPTY */
#endif /* HAVE_OPENPTY */

   if(*ptyfd<0)
      goto fail;
   if(name && *ttyfd<0)
      *ttyfd=open(name,O_RDWR|O_NOCTTY);
   if(!name && *ttyfd>=0)
      name=ttyname(*ttyfd);

   if(!name || *ttyfd<0)
      goto fail;

#if defined(HAVE_DEV_PTMX) && defined(I_PUSH) \
 && !defined(HAVE__GETPTY) && !defined(HAVE_OPENPTY)
   ioctl(*ttyfd, I_PUSH, "ptem");
   ioctl(*ttyfd, I_PUSH, "ldterm");
   ioctl(*ttyfd, I_PUSH, "ttcompat");
#endif
#ifdef HAVE_FCHMOD
   fchmod(*ttyfd,0600);
#else
   chmod(name,0600);
#endif
   signal(SIGCHLD,old_sigchld);
   return name;

fail:
   if(*ttyfd>=0)
      close(*ttyfd);
   if(*ptyfd>=0)
      close(*ptyfd);
   signal(SIGCHLD,old_sigchld);
   return 0;
}
