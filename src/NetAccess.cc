/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <errno.h>
#include <assert.h>
#include "NetAccess.h"
#include "log.h"
#include "url.h"

#define super FileAccess

void NetAccess::Init()
{
   resolver=0;
   idle=0;
   idle_start=now;
   max_retries=0;
   retries=0;
   socket_buffer=0;
   socket_maxseg=0;

   peer=0;
   peer_num=0;
   peer_curr=0;

   reconnect_interval=30;  // retry with 30 second interval
   timeout=600;		   // 10 minutes with no events = reconnect

   proxy=0;
   proxy_port=0;
   proxy_user=proxy_pass=0;

   rate_limit=0;

   Reconfig(0);
}

NetAccess::NetAccess()
{
   Init();
}
NetAccess::NetAccess(const NetAccess *o) : super(o)
{
   Init();
}
NetAccess::~NetAccess()
{
   if(resolver)
      delete resolver;
   if(rate_limit)
      delete rate_limit;
   ClearPeer();

   xfree(proxy); proxy=0;
   xfree(proxy_port); proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;
}

void NetAccess::Reconfig(const char *name)
{
   super::Reconfig(name);

   const char *c=hostname;

   timeout = ResMgr::Query("net:timeout",c);
   reconnect_interval = ResMgr::Query("net:reconnect-interval",c);
   idle = ResMgr::Query("net:idle",c);
   max_retries = ResMgr::Query("net:max-retries",c);
   relookup_always = ResMgr::Query("net:relookup-always",c);
   socket_buffer = ResMgr::Query("net:socket-buffer",c);
   socket_maxseg = ResMgr::Query("net:socket-maxseg",c);

   if(rate_limit)
      rate_limit->Reconfig(name,c);
}

void NetAccess::KeepAlive(int sock)
{
   static int one=1;
   setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&one,sizeof(one));
}
void NetAccess::SetSocketBuffer(int sock,int socket_buffer)
{
   if(socket_buffer==0)
      return;
   if(-1==setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_SNDBUF,%d): %s\n",socket_buffer,strerror(errno));
   if(-1==setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_RCVBUF,%d): %s\n",socket_buffer,strerror(errno));
}
void NetAccess::SetSocketMaxseg(int sock,int socket_maxseg)
{
#ifndef SOL_TCP
# define SOL_TCP IPPROTO_TCP
#endif
#ifdef TCP_MAXSEG
   if(socket_maxseg==0)
      return;
   if(-1==setsockopt(sock,SOL_TCP,TCP_MAXSEG,(char*)&socket_maxseg,sizeof(socket_maxseg)))
      Log::global->Format(1,"setsockopt(TCP_MAXSEG,%d): %s\n",socket_maxseg,strerror(errno));
#endif
}

void  NetAccess::SetSocketBuffer(int sock)
{
   SetSocketBuffer(sock,socket_buffer);
}

void  NetAccess::SetSocketMaxseg(int sock)
{
   SetSocketBuffer(sock,socket_maxseg);
}

const char *NetAccess::SocketNumericAddress(const sockaddr_u *u)
{
#ifdef HAVE_GETNAMEINFO
   static char buf[NI_MAXHOST];
   if(getnameinfo(&u->sa,sizeof(*u),buf,sizeof(buf),0,0,NI_NUMERICHOST)<0)
      return "????";
   return buf;
#else
   static char buf[256];
   if(u->sa.sa_family!=AF_INET)
      return "????";
   unsigned char *a=(unsigned char *)&u->in.sin_addr;
   sprintf(buf,"%u.%u.%u.%u",a[0],a[1],a[2],a[3]);
   return buf;
#endif
}
int NetAccess::SocketPort(const sockaddr_u *u)
{
   if(u->sa.sa_family==AF_INET)
      return ntohs(u->in.sin_port);
#if INET6
   if(u->sa.sa_family==AF_INET6)
      return ntohs(u->in6.sin6_port);
#endif
   return 0;
}

void NetAccess::SayConnectingTo()
{
   assert(peer_curr<peer_num);
   const char *h=(proxy?proxy:hostname);
   char *str=string_alloca(256+strlen(h));
   sprintf(str,_("Connecting to %s%s (%s) port %u"),proxy?"proxy ":"",
      h,SocketNumericAddress(&peer[peer_curr]),SocketPort(&peer[peer_curr]));
   DebugPrint("---- ",str,0);
}

void NetAccess::SetProxy(const char *px)
{
   bool was_proxied=(proxy!=0);

   xfree(proxy); proxy=0;
   xfree(proxy_port); proxy_port=0;
   xfree(proxy_user); proxy_user=0;
   xfree(proxy_pass); proxy_pass=0;

   if(!px)
   {
   no_proxy:
      if(was_proxied)
	 ClearPeer();
      return;
   }

   ParsedURL url(px);
   if(!url.host || url.host[0]==0)
      goto no_proxy;

   proxy=xstrdup(url.host);
   proxy_port=xstrdup(url.port);
   proxy_user=xstrdup(url.user);
   proxy_pass=xstrdup(url.pass);
   ClearPeer();
}

int NetAccess::CheckTimeout()
{
   if(now-event_time>=timeout)
   {
      DebugPrint("**** ",_("Timeout - reconnecting"));
      Disconnect();
      event_time=now;
      return(1);
   }
   block+=TimeOut((timeout-(now-event_time))*1000);
   return(0);
}

void NetAccess::ClearPeer()
{
   xfree(peer);
   peer=0;
   peer_curr=peer_num=0;
}

void NetAccess::NextPeer()
{
   peer_curr++;
   if(peer_curr>peer_num)
      peer_curr=0;
}

void NetAccess::Connect(const char *h,const char *p)
{
   super::Connect(h,p);
   ClearPeer();
}

int NetAccess::Resolve(const char *defp,const char *ser,const char *pr)
{
   int m=STALL;

   if(peer)
   {
      if(relookup_always)
	 ClearPeer();
      else
	 return m;
   }

   if(!resolver)
   {
      if(proxy)
	 resolver=new Resolver(proxy,proxy_port,defp);
      else
	 resolver=new Resolver(hostname,portname,defp,ser,pr);
      m=MOVED;
   }
   if(relookup_always)
      resolver->NoCache();

   resolver->Do();

   if(!resolver->Done())
      return m;

   if(resolver->Error())
   {
      SetError(LOOKUP_ERROR,resolver->ErrorMsg());
      xfree(hostname);
      hostname=0;
      xfree(portname);
      portname=0;
      return(MOVED);
   }

   xfree(peer);
   peer=(sockaddr_u*)xmalloc(resolver->GetResultSize());
   peer_num=resolver->GetResultNum();
   resolver->GetResult(peer);
   peer_curr=0;

   delete resolver;
   resolver=0;
   return MOVED;
}


// RateLimit class implementation.
int RateLimit::total_xfer_number;
RateLimit::BytesPool RateLimit::total;
bool RateLimit::total_reconfig_needed=true;

RateLimit::RateLimit()
{
   if(total_xfer_number==0)
      total.Reset();
   total_xfer_number++;
   Reconfig(0,0);
}
RateLimit::~RateLimit()
{
   total_xfer_number--;
}

#define LARGE 0x10000000
void RateLimit::BytesPool::AdjustTime()
{
   if(SMTask::now>t)
   {
      time_t dif=SMTask::now-t;

      // prevent overflow
      if((LARGE-pool)/dif < rate)
	 pool = pool_max>0 ? pool_max : LARGE;
      else
	 pool += dif*rate;

      if(pool>pool_max && pool_max>0)
	 pool=pool_max;

      t=SMTask::now;
   }
}

int RateLimit::BytesAllowed()
{
   if(total_reconfig_needed)
      ReconfigTotal();

   if(one.rate==0 && total.rate==0) // unlimited
      return LARGE;

   one  .AdjustTime();
   total.AdjustTime();

   int ret=LARGE;
   if(total.rate>0)
      ret=total.pool/total_xfer_number;
   if(one.rate>0 && ret>one.pool)
      ret=one.pool;
   return ret;
}

void RateLimit::BytesPool::Used(int bytes)
{
   if(pool<bytes)
      pool=0;
   else
      pool-=bytes;
}

void RateLimit::BytesUsed(int bytes)
{
   total.Used(bytes);
   one  .Used(bytes);
}

void RateLimit::BytesPool::Reset()
{
   pool=rate;
   t=SMTask::now;
}
void RateLimit::Reconfig(const char *name,const char *c)
{
   if(name && strncmp(name,"net:limit-",10))
      return;
   one.rate     = ResMgr::Query("net:limit-rate",c);
   one.pool_max = ResMgr::Query("net:limit-max",c);
   one.Reset(); // to cut bytes_pool.

   if(name && !strncmp(name,"net:limit-total-",16))
      total_reconfig_needed=true;
}
void RateLimit::ReconfigTotal()
{
   total.rate     = ResMgr::Query("net:limit-total-rate",0);
   total.pool_max = ResMgr::Query("net:limit-total-max",0);
   total.Reset();
   total_reconfig_needed = false;
}
