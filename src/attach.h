/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef SUN_LEN
#define SUN_LEN(su) (sizeof(*(su)) - sizeof((su)->sun_path) + strlen((su)->sun_path))
#endif

class AcceptTermFD : public SMTask
{
   int sock;
   int a_sock;
   int recv_i;
   int fds[3];
   bool accepted;
   bool detached;
public:
   AcceptTermFD() : sock(-1), a_sock(-1), recv_i(0), accepted(false), detached(false) {
      do_listen();
   }
   ~AcceptTermFD() {
      for(int i=0; i<recv_i; i++)
	 close(fds[i]);
      if(sock!=-1) {
	 close(sock);
	 unlink(get_sock_path());
      }
      if(a_sock!=-1)
	 close(a_sock);
   }
   int Do() {
      int m=STALL;
      if(detached)
	 return m;
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
	 if(!Ready(sock,POLLIN)) {
	    Block(sock,POLLIN);
	    return m;
	 }
	 struct sockaddr_un sun_addr;
	 socklen_t sa_len=sizeof(sun_addr);
	 a_sock=accept(sock,(sockaddr*)&sun_addr,&sa_len);
	 if(a_sock==-1 && E_RETRY(errno)) {
	    Block(sock,POLLIN);
	    return m;
	 }
	 if(a_sock==-1) {
	    perror("accept");
	    do_listen();
	    TimeoutS(1);
	    return m;
	 }
	 close(sock);
	 sock=-1;
	 int fl=fcntl(a_sock,F_GETFL);
	 fcntl(a_sock,F_SETFL,fl|O_NONBLOCK);
	 fcntl(a_sock,F_SETFD,FD_CLOEXEC);
	 m=MOVED;
      }
      while(recv_i<=2) {
	 int fd=recvfd(a_sock,0);
	 if(fd==-1 && E_RETRY(errno)) {
	    Block(a_sock,POLLIN);
	    return m;
	 }
	 if(fd==-1)
	 {
	    perror("recvfd");
	    do_listen();
	    TimeoutS(1);
	    return m;
	 }
	 fcntl(fd,F_SETFD,FD_CLOEXEC);
	 fds[recv_i]=fd;
	 ++recv_i;
      }
      printf(_("[%u] Attached to terminal %s. %s\n"),(unsigned)getpid(),ttyname(fds[1]),now.IsoDateTime());
      fflush(stdout);
      fflush(stderr);
      for(int i=0; i<recv_i; i++) {
	 dup2(fds[i],i);
	 if(fds[i]>=recv_i)
	    close(fds[i]);
      }
      close(sock);
      sock=-1;
      unlink(get_sock_path());
      accepted=true;
      printf(_("[%u] Attached to terminal.\n"),(unsigned)getpid());
      return MOVED;
   }
   static xstring& get_sock_path(int pid=0) {
      if(!pid)
	 pid=getpid();
      const char *home=get_lftp_data_dir();
      mkdir(xstring::format("%s/bg",home),0700);
      return xstring::format("%s/bg/%s-%d",home,get_nodename(),pid);
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
      for(int i=0; i<recv_i; i++)
	 close(fds[i]);
      recv_i=0;
      accepted=false;
      detached=false;
      sock=socket(AF_UNIX,SOCK_STREAM,0);
      if(sock!=-1) {
	 int fl=fcntl(sock,F_GETFL);
	 fcntl(sock,F_SETFL,fl|O_NONBLOCK);
	 fcntl(sock,F_SETFD,FD_CLOEXEC);
	 struct sockaddr_un sun_addr;
	 memset(&sun_addr,0,sizeof(sun_addr));
	 sun_addr.sun_family=AF_UNIX;
	 strncpy(sun_addr.sun_path,path,sizeof(sun_addr.sun_path));
	 if(bind(sock,(sockaddr*)&sun_addr,SUN_LEN(&sun_addr))==-1) {
	    perror("bind");
	    close(sock);
	    sock=-1;
	 }
	 if(sock>=0)
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
   int send_i;
   bool detached;
public:
   SendTermFD(pid_t p) : pid(p), sock(-1), connected(false), sent(false), send_i(0), detached(false) {}
   ~SendTermFD() {
      if(sock>=0)
	 close(sock);
   }
   int Do() {
      int m=STALL;
      if(error || detached)
	 return m;
      if(sent) {
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
	 struct sockaddr_un sun_addr;
	 memset(&sun_addr,0,sizeof(sun_addr));
	 sun_addr.sun_family=AF_UNIX;
	 const char *path=AcceptTermFD::get_sock_path(pid);
	 strncpy(sun_addr.sun_path,path,sizeof(sun_addr.sun_path));
	 int res=connect(sock,(sockaddr*)&sun_addr,SUN_LEN(&sun_addr));
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
      while(send_i<=2) {
	 if(sendfd(sock,send_i)<0) {
	    if(NonFatalError(errno)) {
	       Block(sock,POLLOUT);
	       return m;
	    }
	    error=Error::Fatal(xstring::format("sendfd: %s",strerror(errno)));
	    close(sock);
	    sock=-1;
	    return 0;
	 }
	 ++send_i;
	 m=MOVED;
      }
      sent=true;
      pass_pid=pid;
      if(isatty(0)) {
	 SignalHook::Handle(SIGINT,pass_sig);
	 SignalHook::Handle(SIGQUIT,pass_sig);
	 SignalHook::Handle(SIGTSTP,pass_sig);
	 SignalHook::Handle(SIGWINCH,pass_sig);
      }
      return MOVED;
   }
   bool Done() { return error || detached; }
   bool Failed() { return error; }
   const char *ErrorText() { return error->Text(); }
};
#endif
