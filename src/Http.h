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

#include "NetAccess.h"
#include "buffer.h"

class Http : public NetAccess
{
   enum state_t
   {
      DISCONNECTED,
      CONNECTING,
      CONNECTED,
      RECEIVING_HEADER,
      RECEIVING_BODY,
      DONE
   };

   enum tunnel_state_t {
      NO_TUNNEL,
      TUNNEL_WAITING,
      TUNNEL_ESTABLISHED
   };

   state_t state;
   tunnel_state_t tunnel_state;

   void Init();

   void	Send(const char *format,...) PRINTF_LIKE(2,3);

   IOBuffer *send_buf;
   IOBuffer *recv_buf;
   bool recv_buf_suspended;
   void SendMethod(const char *,const char *);
   const char *last_method;
   bool post;
   char *post_data;
   void SendAuth();
   void SendCacheControl();
   void SendBasicAuth(const char *tag,const char *auth);
   void SendBasicAuth(const char *tag,const char *u,const char *p);
   void SendRequest(const char *connection,const char *f);
   void SendRequest(const char *connection=0)
      {
	 SendRequest(connection,file);
      }
   void SendArrayInfoRequest();
   int status_code;
   void HandleHeaderLine(const char *name,const char *value);
   void GetBetterConnection(int level);
   void SetCookie(const char *val);
   char *MakeCookie(const char *host,const char *path);
   void CookieMerge(char **c,const char *add);
   bool CookieClosureMatch(const char *closure,const char *host,const char *path);

   int sock;
   void Disconnect();
   void ResetRequestData();
   void MoveConnectionHere(Http *o);
   int IsConnected()
      {
	 if(sock==-1)
	    return 0;
	 if(state==CONNECTING)
	    return 1;
	 return 2;
      }
   void MakeSSLBuffers();

   char *status;
   int   status_consumed;
   int proto_version;
   char *line;
   off_t body_size;
   off_t bytes_received;
   bool sent_eot;

   bool ModeSupported();

   int  keep_alive_max;
   bool keep_alive;

   int array_send;

   bool chunked;
   long chunk_size;
   off_t chunk_pos;
   bool chunked_trailer;

   bool no_ranges;
   bool seen_ranges_bytes;

   bool no_cache;
   bool no_cache_this;

   const char *user_agent;

   char *entity_content_type;

protected:
   bool hftp;  // ftp over http proxy.
   bool https; // secure http
   bool use_head;

public:
   static void ClassInit();

   Http();
   Http(const Http *);
   ~Http();

   const char *GetProto() { return "http"; }

   FileAccess *Clone() { return new Http(this); }
   static FileAccess *New();
   FileSet *ParseLongList(const char *buf,int len,int *err=0);

   int Do();
   int Done();
   int Read(void *,int);
   int Write(const void *,int);
   int StoreStatus();
   int SendEOT();
   int Buffered();

   void	ResetLocationData();

   void Close();
   const char *CurrentStatus();

   void Reconfig(const char *name=0);

   bool SameSiteAs(FileAccess *fa);
   bool SameLocationAs(FileAccess *fa);

   DirList *MakeDirList(ArgV *a);
   Glob *MakeGlob(const char *pattern);
   ListInfo *MakeListInfo(const char *path);

   void UseCache(bool use) { no_cache_this=!use; }

   bool NeedSizeDateBeforehand() { return true; }

   void Suspend();
   void Resume();

   void Cleanup();
   void CleanupThis();
};

class HFtp : public Http
{
public:
   HFtp();
   HFtp(const HFtp *);
   ~HFtp();

   const char *GetProto() { return "hftp"; }

   FileAccess *Clone() { return new HFtp(this); }
   static FileAccess *New();

   virtual void Login(const char *,const char *);
   virtual void Reconfig(const char *);
};

class Https : public Http
{
public:
   Https();
   Https(const Https *);
   ~Https();

   const char *GetProto() { return "https"; }

   FileAccess *Clone() { return new Https(this); }
   static FileAccess *New();
};

#endif//HTTP_H
