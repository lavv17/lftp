/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "Resolver.h"
#include "SignalHook.h"
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>
#include "xstring.h"
#include "xmalloc.h"
#include "ResMgr.h"

#if !defined(HAVE_HSTRERROR_DECL)
extern "C" { const char *hstrerror(int); }
#endif

#if INET6
# define DEFAULT_ORDER "inet inet6"
#else
# define DEFAULT_ORDER "inet"
#endif

static ResDecl
   res_timeout	  ("dns:fatal-timeout","0", ResMgr::UNumberValidate,0),
   res_order	  ("dns:order",	       DEFAULT_ORDER, Resolver::OrderValidate,0);


struct address_family
{
   int number;
   const char *name;
};
static const address_family af_list[]=
{
   { AF_INET,  "inet"  },
#if INET6
   { AF_INET6, "inet6" },
#endif
   { -1, 0 }
};


Resolver::Resolver(const char *h,const char *p)
{
   hostname=xstrdup(h);
   portname=xstrdup(p);
   port_number=0;

   pipe_to_child[0]=pipe_to_child[1]=-1;
   w=0;
   err_msg=0;
   done=false;
   timeout=0;
   Reconfig();
   time(&start_time);
   addr=0;
   addr_num=0;
   buf=0;
}

Resolver::~Resolver()
{
   if(pipe_to_child[0]!=-1)
      close(pipe_to_child[0]);
   if(pipe_to_child[1]!=-1)
      close(pipe_to_child[1]);

   xfree(hostname);
   xfree(portname);
   xfree(err_msg);
   xfree(addr);
   if(w)
   {
      w->Kill(SIGKILL);
      w->Auto();
   }
   if(buf)
      delete buf;
}

int   Resolver::Do()
{
   if(done)
      return STALL;

   int m=STALL;

   if(pipe_to_child[0]==-1)
   {
      int res=pipe(pipe_to_child);
      if(res==-1)
      {
	 if(errno==ENFILE || errno==EMFILE)
	 {
	    block+=TimeOut(1000);
	    return m;
	 }
	 MakeErrMsg("pipe()");
	 return MOVED;
      }
      fcntl(pipe_to_child[0],F_SETFL,O_NONBLOCK);
      fcntl(pipe_to_child[0],F_SETFD,FD_CLOEXEC);
      fcntl(pipe_to_child[1],F_SETFD,FD_CLOEXEC);
      m=MOVED;
   }

   if(!w)
   {
      pid_t proc=fork();
      if(proc==-1)
      {
	 block+=TimeOut(1000);
	 return m;
      }
      if(proc==0)
      {	 // child
	 SignalHook::Ignore(SIGINT);
	 SignalHook::Ignore(SIGTSTP);
	 SignalHook::Ignore(SIGQUIT);
	 SignalHook::Ignore(SIGHUP);
	 close(0);
	 close(2);
	 dup2(pipe_to_child[1],1);
	 close(pipe_to_child[1]);
	 close(pipe_to_child[0]);
	 DoGethostbyname();
	 _exit(0);
      }
      // parent
      close(pipe_to_child[1]);
      pipe_to_child[1]=-1;

      w=new ProcWait(proc);
      m=MOVED;
   }

   if(!buf)
   {
      buf=new FileInputBuffer(new FDStream(pipe_to_child[0],"<pipe>"));
      while(buf->Do()==MOVED);
      m=MOVED;
   }

   if(buf->Error())
   {
      err_msg=xstrdup(buf->ErrorText());
      done=true;
      return MOVED;
   }

   if(!buf->Eof())   // wait for all data to arrive (not too much)
   {
      if(timeout>0)
      {
	 if(now-start_time > timeout)
	 {
	    err_msg=xstrdup(_("host name resolve timeout"));
	    done=true;
	    return MOVED;
	 }
	 block+=TimeOut((timeout-(now-start_time))*1000);
      }
      return m;
   }

   const char *s;
   char c;
   int n;

   buf->Get(&s,&n);
   if(n<1)
      goto proto_error;
   c=*s;
   buf->Skip(1);
   buf->Get(&s,&n);
   if(c=='E' || c=='P') // error
   {
      err_msg=(char*)xmalloc(strlen(hostname)+strlen(portname)+n+3);
      sprintf(err_msg,"%s: ",(c=='E'?hostname:portname));
      char *e=err_msg+strlen(err_msg);
      memcpy(e,s,n);
      e[n]=0;
      done=true;
      return MOVED;
   }
   if((unsigned)n<sizeof(sockaddr_u))
   {
   proto_error:
      // protocol error
      err_msg=xstrdup(_("child returned invalid data"));
      done=true;
      return MOVED;
   }
   addr_num=n/sizeof(*addr);
   addr=(sockaddr_u*)xmalloc(n);
   memcpy(addr,s,n);
   done=true;
   return MOVED;
}

void Resolver::MakeErrMsg(const char *f)
{
   const char *e=strerror(errno);
   err_msg=(char*)xmalloc(strlen(e)+strlen(f)+3);
   sprintf(err_msg,"%s: %s",f,e);
   done=true;
}

void Resolver::AddAddress(int family,const char *address,int len)
{
   addr=(sockaddr_u*)xrealloc(addr,(addr_num+1)*sizeof(*addr));
   sockaddr_u *add=addr+addr_num;
   addr_num++;

   memset(add,0,sizeof(*add));

   add->sa.sa_family=family;
   switch(family)
   {
   case AF_INET:
      memcpy(&add->in.sin_addr,address,len);
      add->in.sin_port=port_number;
      break;

#if INET6
   case AF_INET6:
      memcpy(&add->in6.sin6_addr,address,len);
      add->in6.sin6_port=port_number;
      break;
#endif

   default:
      addr_num--;
      return;
   }
}

const char *Resolver::ParseOrder(const char *s,int *o)
{
   static char *error=0;

   const char * const delim="\t ";
   char *s1=alloca_strdup(s);
   int idx=0;

   for(s1=strtok(s1,delim); s1; s1=strtok(0,delim))
   {
      const address_family *f;
      for(f=af_list; f->name; f++)
      {
	 if(!strcasecmp(s1,f->name))
	    break;
      }
      if(!f->name)
      {
	 const char * const format=_("unknown address family `%s'");
	 error=(char*)xrealloc(error,strlen(format)+strlen(s1));
	 sprintf(error,format,s1);
	 return error;
      }
      if(idx<15)
      {
	 if(o) o[idx]=f->number;
	 idx++;
      }
   }
   if(o) o[idx]=-1;
   return 0;
}

const char *Resolver::OrderValidate(char **s)
{
   return ParseOrder(*s,0);
}

void Resolver::DoGethostbyname()
{
   time_t try_time;
   const char *error=0;
#ifndef HAVE_H_ERRNO_DECL
   extern int h_errno;
#endif

   if(port_number==0)
   {
      if(isdigit((unsigned char)portname[0]))
	 port_number=htons(atoi(portname));
      else
      {
	 struct servent *se=getservbyname(portname,"tcp");
	 if(se)
	    port_number=se->s_port;
	 else
	 {
	    write(1,"P",1);
	    error="no such tcp service";
	    write(1,error,strlen(error));
	    return;
	 }
      }
   }

   int af_index=0;
   int af_order[16];

   ParseOrder(res_order.Query(hostname),af_order);

   for(;;)
   {
      time(&try_time);

      int af=af_order[af_index];
      if(af==-1)
	 break;

      struct hostent *ha;
#ifdef HAVE_GETHOSTBYNAME2
      ha=gethostbyname2(hostname,af);
#else
      if(af==AF_INET)
	 ha=gethostbyname(hostname);
      else
      {
	 af_index++;
	 continue;
      }
#endif

      if(ha)
      {
	 const char * const *a;
	 for(a=ha->h_addr_list; *a; a++)
	    AddAddress(ha->h_addrtype, *a, ha->h_length);
	 af_index++;
	 continue;
      }
#ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN)
#endif
      {
	 if(error==0)
	 {
#ifdef HAVE_H_ERRNO
	    error=hstrerror(h_errno);
#else
	    error="Host name lookup failure";
#endif
	 }
	 af_index++;
	 continue; // try other address families
      }
      time_t t=time(0);
      if(t-try_time<5)
	 sleep(5-(t-try_time));
   }

   if(addr_num==0)
   {
      write(1,"E",1);
      if(error==0)
	 error="No address found";
      write(1,error,strlen(error));
      return;
   }
   write(1,"O",1);
   write(1,addr,addr_num*sizeof(*addr));
   return;
}

void Resolver::Reconfig()
{
   timeout = res_timeout.Query(0);
}

void Resolver::ClassInit()
{
}
