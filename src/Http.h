/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef HTTP_H
#define HTTP_H

#include "NetAccess.h"
#include "buffer.h"
#include "lftp_ssl.h"
#include "HttpHeader.h"
#include "HttpAuth.h"

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

   void Send(const xstring& str);
   void	Send(const char *format,...) PRINTF_LIKE(2,3);
   void Send(const HttpHeader *hdr);

   class Connection
   {
      xstring_c closure;
   public:
      int sock;
      SMTaskRef<IOBuffer> send_buf;
      SMTaskRef<IOBuffer> recv_buf;
      void MakeBuffers();
#if USE_SSL
      Ref<lftp_ssl> ssl;
      void MakeSSLBuffers();
#endif

      void SuspendInternal()
      {
	 if(send_buf) send_buf->SuspendSlave();
	 if(recv_buf) recv_buf->SuspendSlave();
      }
      void ResumeInternal()
      {
	 if(send_buf) send_buf->ResumeSlave();
	 if(recv_buf) recv_buf->ResumeSlave();
      }

      Connection(int s,const char *c);
      ~Connection();
   };

   Ref<Connection> conn;

   static void AppendHostEncoded(xstring&,const char *);
   void SendMethod(const char *,const char *);
   const char *last_method;
   xstring_c last_uri;
   xstring_c last_url; // for proxy requests

   enum { HTTP_NONE=0, HTTP_POST, HTTP_MOVE, HTTP_COPY, HTTP_PROPFIND } special;
   xstring special_data;

   void DirFile(xstring& path,const xstring& ecwd,const xstring& efile) const;
   void SendAuth();
   void SendProxyAuth();
   void SendCacheControl();
   void SendBasicAuth(const char *tag,const char *auth);
   void SendBasicAuth(const char *tag,const char *u,const char *p);
   void SendRequest(const char *connection,const char *f);
   void SendRequest(const char *connection=0)
      {
	 SendRequest(connection,file);
      }
   int SendArrayInfoRequest(); // returns count of sent requests
   void ProceedArrayInfo();
   void SendPropfind(const xstring& efile,int depth);
   void SendPropfindBody();
   static const xstring& FormatLastModified(time_t);
   void SendProppatch(const xstring& efile);

   int status_code;
   void HandleHeaderLine(const char *name,const char *value);
   static const xstring& extract_quoted_header_value(const char *value,const char **end=0);
   void HandleRedirection();
   void GetBetterConnection(int level);
   void SetCookie(const char *val);
   void MakeCookie(xstring &cookie,const char *host,const char *path);
   void CookieMerge(xstring &c,const char *add);
   bool CookieClosureMatch(const char *closure,const char *host,const char *path);

   void DisconnectLL();
   void ResetRequestData();
   void MoveConnectionHere(Http *o);
   int IsConnected() const
      {
	 if(!conn)
	    return 0;
	 if(state==CONNECTING || tunnel_state==TUNNEL_WAITING)
	    return 1;
	 return 2;
      }
   void LogErrorText();

   xstring status;
   int status_consumed;
   int proto_version;
   xstring line;
   off_t body_size;
   off_t bytes_received;
   bool sent_eot;

   bool ModeSupported();
   bool ModeIs(open_mode m) const {
      if(m==STORE)
	 return mode==m && !sending_proppatch;
      return mode==m;
   }

   int  keep_alive_max;
   bool keep_alive;

   int array_send;

   bool chunked;
   bool chunked_trailer;
   long chunk_size;
   off_t chunk_pos;

   off_t request_pos;

   Ref<DirectedBuffer> inflate;
   SMTaskRef<IOBuffer> propfind;
   xstring_c content_encoding;
   static bool IsCompressed(const char *s);
   bool CompressedContentEncoding() const;
   bool CompressedContentType() const;

   bool no_ranges;
   bool seen_ranges_bytes;
   bool entity_date_set;
   bool sending_proppatch;

   bool no_cache;
   bool no_cache_this;

   // for WWW[0] and PROXY[1]
   int  auth_sent[2];
   HttpAuth::scheme_t auth_scheme[2];
   void NewAuth(const char *hdr,HttpAuth::target_t target,const char *user,const char *pass);
   void SendAuth(HttpAuth::target_t target,const char *user,const char *uri);

   xstring_c auth_user;
   xstring_c auth_pass;

   bool use_propfind_now;
   xstring allprop;

   long retry_after;

   const char *user_agent;

   int _Read(Buffer *,int);  // does not update pos, rate_limit, retries, does not check state.
   void _Skip(int to_skip); // skip in recv_buf or inflate (unless moved), update real_pos
   void _UpdatePos(int to_skip); // update real_pos, chunk_pos etc.

protected:
   bool hftp;  // ftp over http proxy.
   bool https; // secure http
   bool use_head;

public:
   static void ClassInit();

   Http();
   Http(const Http *);
   ~Http();

   const char *GetProto() const { return "http"; }

   FileAccess *Clone() const { return new Http(this); }
   static FileAccess *New();
   FileSet *ParseLongList(const char *buf,int len,int *err=0) const;

   int Do();
   int Done();
   int Read(Buffer *,int);
   int Write(const void *,int);
   int StoreStatus();
   int SendEOT();
   int Buffered();

   void	ResetLocationData();

   void Close();
   const char *CurrentStatus();

   void Reconfig(const char *name=0);

   bool SameSiteAs(const FileAccess *fa) const;
   bool SameLocationAs(const FileAccess *fa) const;

   DirList *MakeDirList(ArgV *a);
   Glob *MakeGlob(const char *pattern);
   ListInfo *MakeListInfo(const char *path);

   void UseCache(bool use) { no_cache_this=!use; }

   bool NeedSizeDateBeforehand() { return true; }

   void SuspendInternal();
   void ResumeInternal();

   static const time_t ATOTM_ERROR = -1;
   static time_t atotm (const char *time_string);
};

class HFtp : public Http
{
public:
   HFtp();
   HFtp(const HFtp *);
   ~HFtp();

   const char *GetProto() const { return "hftp"; }

   FileAccess *Clone() const { return new HFtp(this); }
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

   const char *GetProto() const { return "https"; }

   FileAccess *Clone() const { return new Https(this); }
   static FileAccess *New();
};

#endif//HTTP_H
