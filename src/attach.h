/*
 * lftp and utils
 *
 * Copyright (c) 2011 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#ifndef ATTACH_H
#define ATTACH_H

#include <sys/types.h>
#include <sys/stat.h> // for mkdir()
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <locale.h>
#include <ctype.h>
#include <sys/utsname.h>
#include <sys/un.h>
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#elif HAVE_WS2TCPIP_H
# include <ws2tcpip.h>
#endif

#include "SMTask.h"
#include "Error.h"
#include "SignalHook.h"
#include "misc.h"
#include "passfd.h"

class AcceptTermFD : public SMTask
{
   int sock;
   int a_sock;
   bool accepted;
   bool detached;
public:
   AcceptTermFD() : sock(-1), a_sock(-1), accepted(false), detached(false) {
      do_listen();
   }
   ~AcceptTermFD() {
      if(sock!=-1) {
	 close(sock);
	 unlink(get_sock_path());
      }
      if(a_sock!=-1)
	 close(a_sock);
   }
   int Do() {
      int m=STALL;
      if(accepted) {
	 char buf;
	 int res=read(a_sock,&buf,1);
	 if(res==-1 && E_RETRY(errno)) {
	    Block(a_sock,POLLIN);
	    return m;
	 }
	 if(res<=0) {
	    detached=true;
	    close(a_sock);
	    a_sock=-1;
	    raise(SIGHUP);
	 }
	 return m;
      }
      if(a_sock==-1) {
	 if(sock==-1)
	    do_listen();
	 if(sock==-1) {
	    TimeoutS(1);
	    return m;
	 }
	 struct sockaddr_un sun;
	 socklen_t sa_len=sizeof(sun);
	 a_sock=accept(sock,(sockaddr*)&sun,&sa_len);
	 if(a_sock==-1) {
	    Block(sock,POLLIN);
	    return m;
	 }
	 close(sock);
	 sock=-1;
	 int fl=fcntl(a_sock,F_GETFL);
	 fcntl(a_sock,F_SETFL,fl|O_NONBLOCK);
	 fcntl(a_sock,F_SETFD,FD_CLOEXEC);
	 m=MOVED;
      }
      int fd=recvfd(a_sock,0);
      if(fd==-1) {
	 Block(a_sock,POLLIN);
	 return m;
      }
      fcntl(fd,F_SETFD,FD_CLOEXEC);
      printf(_("[%lu] Attached to terminal %s. %s\n"),(unsigned long)getpid(),ttyname(fd),now.IsoDateTime());
      fflush(stdout);
      fflush(stderr);
      dup2(fd,0);
      dup2(fd,1);
      dup2(fd,2);
      if(fd>2)
	 close(fd);
      close(sock);
      sock=-1;
      unlink(get_sock_path());
      accepted=true;
      printf(_("[%lu] Attached to terminal.\n"),(unsigned long)getpid());
      return MOVED;
   }
   static const char *get_sock_path(int pid=0) {
      if(!pid)
	 pid=getpid();
      struct utsname u;
      uname(&u);
      const char *home=get_lftp_home();
      mkdir(xstring::format("%s/bg",home),0700);
      return xstring::format("%s/bg/%s-%d",home,u.nodename,pid);
   }
   void do_listen() {
      const char *path=get_sock_path();
      unlink(path);
      if(sock>=0)
	 close(sock);
      if(a_sock>=0) {
	 close(a_sock);
	 a_sock=-1;
      }
      accepted=false;
      sock=socket(AF_UNIX,SOCK_STREAM,0);
      if(sock!=-1) {
	 int fl=fcntl(sock,F_GETFL);
	 fcntl(sock,F_SETFL,fl|O_NONBLOCK);
	 fcntl(sock,F_SETFD,FD_CLOEXEC);
	 struct sockaddr_un sun;
	 memset(&sun,0,sizeof(sun));
	 sun.sun_family=AF_UNIX;
	 strncpy(sun.sun_path,path,sizeof(sun.sun_path));
	 if(bind(sock,(sockaddr*)&sun,SUN_LEN(&sun))==-1) {
	    perror("bind");
	    close(sock);
	    sock=-1;
	 }
	 listen(sock,1);
      }
   }
   bool Accepted() { return accepted; }
   void Detach() { do_listen(); }
   bool Detached() { return detached; }
};

class SendTermFD : public SMTask
{
   static pid_t pass_pid;
   static void pass_sig(int s) {
      kill(pass_pid,s);
   }
   Ref<Error> error;
   pid_t pid;
   int sock;
   bool connected;
   bool sent;
   bool detached;
public:
   SendTermFD(pid_t p) : pid(p), sock(-1), connected(false), sent(false), detached(false) {}
   ~SendTermFD() {
      if(sock>=0)
	 close(sock);
   }
   int Do() {
      int m=STALL;
      if(error || detached)
	 return m;
      if(sock==-1) {
	 sock=socket(AF_UNIX,SOCK_STREAM,0);
	 if(sock==-1) {
	    if(NonFatalError(errno))
	    {
	       TimeoutS(1);
	       return m;
	    }
	    error=Error::Fatal(xstring::format("socket(): %s",strerror(errno)));
	    return MOVED;
	 }
	 int fl=fcntl(sock,F_GETFL);
	 fcntl(sock,F_SETFL,fl|O_NONBLOCK);
	 fcntl(sock,F_SETFD,FD_CLOEXEC);
	 connected=false;
	 m=MOVED;
      }
      if(!connected) {
	 struct sockaddr_un sun;
	 memset(&sun,0,sizeof(sun));
	 sun.sun_family=AF_UNIX;
	 const char *path=AcceptTermFD::get_sock_path(pid);
	 strncpy(sun.sun_path,path,sizeof(sun.sun_path));
	 int res=connect(sock,(sockaddr*)&sun,SUN_LEN(&sun));
	 if(res==-1 && !NonFatalError(errno)) {
	    error=Error::Fatal(xstring::format("connect(%s): %s",path,strerror(errno)));
	    return MOVED;
	 }
	 if(res==-1) {
	    Block(sock,POLLOUT);
	    return m;
	 }
	 connected=true;
	 m=MOVED;
      }
      if(!sent) {
	 if(sendfd(sock,1)<0) {
	    if(NonFatalError(errno)) {
	       Block(sock,POLLOUT);
	       return m;
	    }
	    error=Error::Fatal(xstring::format("sendfd: %s",strerror(errno)));
	    close(sock);
	    sock=-1;
	    return 0;
	 }
	 sent=true;
	 m=MOVED;

	 pass_pid=pid;
	 SignalHook::Handle(SIGINT,pass_sig);
	 SignalHook::Handle(SIGQUIT,pass_sig);
	 SignalHook::Handle(SIGTSTP,pass_sig);
	 SignalHook::Handle(SIGWINCH,pass_sig);
      }
      char buf;
      int res=read(sock,&buf,1);
      if(res==-1 && E_RETRY(errno)) {
	 Block(sock,POLLIN);
	 return m;
      }
      if(res<=0) {
	 detached=true;
	 close(sock);
	 sock=-1;
	 SignalHook::DoCount(SIGINT);
	 SignalHook::Restore(SIGQUIT);
	 SignalHook::DoCount(SIGTSTP);
	 SignalHook::Restore(SIGWINCH);
      }
      return MOVED;
   }
   bool Done() { return error || detached; }
   bool Failed() { return error; }
   const char *ErrorText() { return error->Text(); }
};
#endif
