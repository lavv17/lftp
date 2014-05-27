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

#ifndef NETACCESS_H
#define NETACCESS_H

#include "FileAccess.h"
#include "Resolver.h"
#include "LsCache.h"
#include "RateLimit.h"

class NetAccess : public FileAccess, public Networker
{
protected:
   SMTaskRef<Resolver> resolver;

   xarray<sockaddr_u> peer;
   int peer_curr;
   void	 ClearPeer();
   void	 NextPeer();

   int	 max_retries;
   int	 max_persist_retries;
   int	 persist_retries;

   Timer idle_timer;
   Timer timeout_timer;
   bool	 CheckTimeout();

   int	 reconnect_interval;
   float reconnect_interval_current;
   float reconnect_interval_multiplier;
   int   reconnect_interval_max;

   int	 connection_limit;
   bool	 connection_takeover;

   Ref<RateLimit> rate_limit;

   int	 socket_buffer;
   int	 socket_maxseg;
   void	 SetSocketBuffer(int sock) { Networker::SetSocketBuffer(sock,socket_buffer); }
   void	 SetSocketMaxseg(int sock) { Networker::SetSocketMaxseg(sock,socket_maxseg); }

   int SocketCreate(int af,int type,int proto) { return Networker::SocketCreate(af,type,proto,hostname); }
   int SocketCreateTCP(int af) { return Networker::SocketCreateTCP(af,hostname); }

   int Poll(int fd,int ev,const char **err);
   const char *CheckHangup(const struct pollfd *pfd,int num);

   xstring_c proxy;
   xstring_c proxy_port;
   xstring_c proxy_user;
   xstring_c proxy_pass;
   xstring_c proxy_proto;

   xstring_c home_auto;
   void	 PropagateHomeAuto();
   const char *FindHomeAuto();

   void SayConnectingTo();

   void SetProxy(const char *);
   static bool NoProxy(const char *);

   int Resolve(const char *defp,const char *ser,const char *pr);

   const char *DelayingMessage();
   bool ReconnectAllowed();
   bool CheckRetries();	// returns false if max-retries exceeded.
   bool NextTry();	// increments retries; does CheckRetries().
   void TrySuccess();	// reset retry counters.

   virtual void HandleTimeout();

public:
   void Init();

   NetAccess();
   NetAccess(const NetAccess *);
   ~NetAccess();

   void Reconfig(const char *name=0);

   void Open(const char *fn,int mode,off_t offs);
   void ResetLocationData();

   void Close();

   int CountConnections();

   static void ClassInit();
};

class GenericParseListInfo : public ListInfo
{
protected:
   int mode;
   SMTaskRef<IOBuffer> ubuf;

   bool get_time_for_dirs;
   bool can_get_prec_time;

   virtual FileSet *Parse(const char *buf,int len)
      { return session->ParseLongList(buf,len); }

public:
   GenericParseListInfo(FileAccess *session,const char *path);
   int Do();
   const char *Status();
};

#endif//NETACCESS_H
