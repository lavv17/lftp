/*
 * lftp and utils
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

#ifndef HTTP_H
#define HTTP_H

#include "FileAccess.h"
#include "Resolver.h"
#include "buffer.h"
#include "StatusLine.h"

class Http : public FileAccess
{
   enum state_t
   {
      DISCONNECTED,
      CONNECTING,
      RECEIVING_HEADER,
      RECEIVING_BODY,
      DONE,
      ERROR
   };

   state_t state;

   void Init();

   Resolver *resolver;

   sockaddr_u *peer;
   int	 peer_num;
   int	 peer_curr;
   void	 ClearPeer();
   void	 NextPeer();

   bool  relookup_always;

   int 	 max_send;
   void	 Send(const char *format,...) PRINTF_LIKE(2,3);

   FileOutputBuffer *send_buf;
   FileInputBuffer *recv_buf;
   void SendMethod(const char *,const char *);
   void SendAuth();
   void SendRequest(const char *connection,const char *f);
   void SendRequest(const char *connection=0)
      {
	 SendRequest(connection,file);
      }
   void SendArrayInfoRequest();
   int status_code;
   void HandleHeaderLine(const char *name,const char *value);

   int sock;
   void Disconnect();

   char *status;
   int   status_consumed;
   int proto_version;
   char *line;
   long body_size;
   long bytes_received;
   char *location;
   bool sent_eot;

   void SetError(int code,const char *mess=0);
   void Fatal(const char *mess);
   int error_code;

   bool CheckTimeout();

   bool ModeSupported();

   int	 idle;
   time_t idle_start;
   int	 retries;
   int	 max_retries;
   int	 socket_buffer;
   int	 socket_maxseg;

   char *proxy;
   char *proxy_port;
   char *proxy_user;
   char *proxy_pass;

   void SetProxy(const char *);

   void BumpEventTime(time_t t);

   int  keep_alive_max;
   bool keep_alive;

   int array_send;

   bool chunked;
   long chunk_size;
   long chunk_pos;

   bool no_cache;
   bool no_cache_this;

protected:
   bool hftp;  // ftp over http proxy.

public:
   static void ClassInit();

   Http();
   Http(const Http *);
   ~Http();

   const char *GetProto() { return "http"; }

   FileAccess *Clone() { return new Http(this); }
   static FileAccess *New() { return new Http(); }

   int Do();
   int Done();
   int Read(void *,int);
   int Write(const void *,int);
   int StoreStatus();
   int SendEOT();

   void	 Connect(const char *h,const char *p);

   void Close();
   const char *CurrentStatus();

   void Reconfig();

   bool SameSiteAs(FileAccess *fa);
   bool SameLocationAs(FileAccess *fa);

   DirList *MakeDirList(ArgV *a);
   Glob *MakeGlob(const char *pattern);
   ListInfo *MakeListInfo();

   void UseCache(bool use) { no_cache_this=!use; }
};

class HFtp : public Http
{
public:
   HFtp();
   HFtp(const HFtp *);
   ~HFtp();

   const char *GetProto() { return "hftp"; }

   FileAccess *Clone() { return new HFtp(this); }
   static FileAccess *New() { return new HFtp(); }
};

#endif//HTTP_H
