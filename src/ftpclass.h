/*
 * lftp and utils
 *
 * Copyright (c) 1996-2004 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FTPCLASS_H
#define FTPCLASS_H

#include "trio.h"
#include <time.h>

#include "NetAccess.h"

#ifdef USE_SSL
# include <openssl/ssl.h>
#endif

class IOBufferTelnet : public IOBufferStacked
{
   void PutTranslated(const char *,int);
public:
   IOBufferTelnet(IOBuffer *b) : IOBufferStacked(b) {}
};

class Ftp : public NetAccess
{
   enum automate_state
   {
      EOF_STATE,	   // control connection is open, idle state
      INITIAL_STATE,	   // all connections are closed
      CONNECTING_STATE,	   // we are connecting the control socket
      HTTP_PROXY_CONNECTED,// connected to http proxy, but have no reply yet
      CONNECTED_STATE,	   // just after connect
      WAITING_STATE,	   // we're waiting for a response with data
      ACCEPTING_STATE,	   // we're waiting for an incoming data connection
      DATA_OPEN_STATE,	   // data connection is open, for read or write
      CWD_CWD_WAITING_STATE,  // waiting until 'CWD $cwd' finishes
      USER_RESP_WAITING_STATE,// waiting until 'USER $user' finishes
      DATASOCKET_CONNECTING_STATE  // waiting for data_sock to connect
   };

   enum expect_t
   {
      CHECK_NONE,	// no special check
      CHECK_IGNORE,	// ignore response
      CHECK_READY,	// check response after connect
      CHECK_REST,	// check response for REST
      CHECK_TYPE,	// check response for TYPE
      CHECK_CWD,	// check response for CWD
      CHECK_CWD_CURR,	// check response for CWD into current directory
      CHECK_CWD_STALE,	// check response for CWD when it's not critical
      CHECK_ABOR,	// check response for ABOR
      CHECK_SIZE,	// check response for SIZE
      CHECK_SIZE_OPT,	// check response for SIZE and save size to *opt_size
      CHECK_MDTM,	// check response for MDTM
      CHECK_MDTM_OPT,	// check response for MDTM and save size to *opt_date
      CHECK_PRET,	// check response for PRET
      CHECK_PASV,	// check response for PASV and save address
      CHECK_EPSV,	// check response for EPSV and save address
      CHECK_PORT,	// check response for PORT or EPRT
      CHECK_FILE_ACCESS,// generic check for file access
      CHECK_PWD,	// check response for PWD and save it to home
      CHECK_RNFR,	// check RNFR and issue RNTO
      CHECK_USER,	// check response for USER
      CHECK_USER_PROXY,	// check response for USER sent to proxy
      CHECK_PASS,	// check response for PASS
      CHECK_PASS_PROXY,	// check response for PASS sent to proxy
      CHECK_TRANSFER,	// generic check for transfer
      CHECK_TRANSFER_CLOSED, // check for transfer complete when Close()d.
      CHECK_FEAT,	// check response for FEAT
      CHECK_SITE_UTIME,	// check response for SITE UTIME
      CHECK_QUOTED	// check response for any command submitted by QUOTE_CMD
#ifdef USE_SSL
      ,CHECK_AUTH_TLS,
      CHECK_PROT
#endif
   };

   class Connection
   {
   public:
      int control_sock;
      IOBuffer *control_recv;
      IOBuffer *control_send;
      IOBufferTelnet *telnet_layer_send;
      Buffer *send_cmd_buffer; // holds unsent commands.
      int data_sock;
      IOBuffer *data_iobuf;
      int aborted_data_sock;
      sockaddr_u peer_sa;
      sockaddr_u data_sa; // address for data accepting
      bool quit_sent;
      bool fixed_pasv;	  // had to fix PASV address.
      bool translation_activated;
      bool proxy_is_http; // true when connection was established via http proxy.
      bool may_show_password;

      int multiline_code; // the code of multiline response.
      int sync_wait;	  // number of commands in flight.
      bool ignore_pass;	  // logged in just with user
      bool try_feat_after_login;

      char type;  // type of transfer: 'A'scii or 'I'mage

      bool dos_path;
      bool vms_path;
      bool mdtm_supported;
      bool size_supported;
      bool rest_supported;
      bool site_chmod_supported;
      bool site_utime_supported;
      bool pret_supported;
      bool utf8_supported;
      bool lang_supported;
      bool mlst_supported;
      off_t last_rest;	// last successful REST position.
      off_t rest_pos;	// the number sent with REST command.

#ifdef USE_SSL
      SSL *control_ssl;
      SSL *data_ssl;
      char prot;  // current data protection scheme 'C'lear or 'P'rivate
      bool auth_sent;
      bool auth_supported;
      char *auth_args_supported;
#endif

      char *mlst_attr_supported;

      Connection();
      ~Connection();

      bool data_address_ok(sockaddr_u *d,bool verify_address,bool verify_port);
      void SavePeerAddress();

      void MakeBuffers();
      void MakeSSLBuffers(const char *h);
      void InitTelnetLayer();
      void SetControlConnectionTranslation(const char *cs);

      void CloseDataConnection();

      void Send(const char *cmd,int len);
      void SendCmd(const char *cmd);
      void SendCmd2(const char *cmd,const char *f);
      void SendCmd2(const char *cmd,int v);
      void SendCmdF(const char *fmt,...) PRINTF_LIKE(2,3);
      int FlushSendQueueOneCmd(); // sends single command from send_cmd_buffer
   };

   Connection *conn;

   struct Expect
   {
      expect_t check_case;
      char *arg;
      Expect *next;

      Expect(expect_t e,const char *a=0)
	 {
	    check_case=e;
	    arg=xstrdup(a);
	 }
      Expect(expect_t e,char c)
	 {
	    check_case=e;
	    arg=(char*)xmalloc(2);
	    arg[0]=c;
	    arg[1]=0;
	 }
      ~Expect()
	 {
	    xfree(arg);
	 }
   };
   class ExpectQueue
   {
      Expect *first; // next to expect
      Expect **last;  // for appending
      int count;

   public:
      ExpectQueue();
      ~ExpectQueue();

      void Push(Expect *e);
      void Push(expect_t e) { Push(new Expect(e)); }
      Expect *Pop();
      Expect *FindLastCWD();
      int Count() { return count; }
      bool IsEmpty() { return count==0; }
      bool Has(expect_t);
      bool FirstIs(expect_t);
      void Close();
   };

   ExpectQueue *expect;

   void  CheckResp(int resp);
   int	 ReplyLogPriority(int code);

   void	 RestCheck(int);
   void  NoFileCheck(int);
   void	 TransferCheck(int);
   void	 LoginCheck(int);
   void	 NoPassReqCheck(int);
   void	 proxy_LoginCheck(int);
   void	 proxy_NoPassReqCheck(int);
   void	 CheckFEAT(char *reply);
   char *ExtractPWD();
   void  SendCWD(const char *path,expect_t c,const char *arg=0);
   void	 CatchDATE(int);
   void	 CatchDATE_opt(int);
   void	 CatchSIZE(int);
   void	 CatchSIZE_opt(int);

   enum pasv_state_t
   {
      PASV_NO_ADDRESS_YET,
      PASV_HAVE_ADDRESS,
      PASV_DATASOCKET_CONNECTING,
      PASV_HTTP_PROXY_CONNECTED
   };
   pasv_state_t pasv_state;	// state of PASV, when state==DATASOCKET_CONNECTING_STATE

   pasv_state_t Handle_PASV();
   pasv_state_t Handle_EPSV();

   bool NonError5XX(int act);
   bool Transient5XX(int act);

   void	 InitFtp();

   void	 HandleTimeout();

#ifdef USE_SSL
   void	 BlockOnSSL(SSL*);
protected:
   bool	 ftps;	  // ssl and prot='P' by default (port 990)
private:
#else
   static const bool ftps; // for convenience
#endif

   char  *line;
   int	 line_len;
   char  *all_lines;

   bool	 eof;

   time_t   nop_time;
   off_t    nop_offset;
   int	    nop_count;

   time_t   stat_time;
   time_t   retry_time;

   void	 DataAbort();
   void  DataClose();

   void  ControlClose();
   void  AbortedClose();

   void  SendUrgentCmd(const char *cmd);
   int	 FlushSendQueueOneCmd();
   int	 FlushSendQueue(bool all=false);
   void	 SendArrayInfoRequests();
   void	 SendSiteIdle();
   void	 SendAcct();
   void	 SendSiteGroup();
   void	 SendUTimeRequest();
   void SendAuth(const char *auth);

   const char *QueryStringWithUserAtHost(const char *);

   // If a response is received, it checks it for accordance with
   // response_queue and switch to a state if necessary
   int	 ReceiveResp();

   bool ProxyIsHttp()
      {
	 if(!proxy_proto)
	    return false;
	 return !strcmp(proxy_proto,"http")
	     || !strcmp(proxy_proto,"https");
      }
   int	 http_proxy_status_code;
   // Send CONNECT method to http proxy.
   void	 HttpProxySendConnect();
   void	 HttpProxySendConnectData();
   // Check if proxy returned a reply, returns true if reply is ok.
   // May disconnect.
   bool	 HttpProxyReplyCheck(IOBuffer *buf);

   bool	 AbsolutePath(const char *p);

   void MoveConnectionHere(Ftp *o);

   automate_state state;

   char	 *anon_user;
   char	 *anon_pass;

   char	 *charset;

   static const char *DefaultAnonPass();

   int	 flags;

   void	 SetError(int code,const char *mess=0);

   bool  verify_data_address;
   bool  verify_data_port;
   bool	 rest_list;
   char  *list_options;

   bool	 GetBetterConnection(int level,bool limit_reached);
   bool  SameConnection(const Ftp *o);

   int	 nop_interval;

   void set_idle_start()
      {
	 idle_start=now;
	 if(conn && idle>0)
	    TimeoutS(idle);
      }

   char *skey_pass;
   bool allow_skey;
   bool force_skey;
   const char *make_skey_reply();

   bool disconnect_on_close;

public:
   enum copy_mode_t { COPY_NONE, COPY_SOURCE, COPY_DEST };
private:
   copy_mode_t copy_mode;
   sockaddr_u copy_addr;
   bool copy_addr_valid;
   bool copy_passive;
   bool	copy_done;
   bool	copy_connection_open;
   bool copy_allow_store;
   bool copy_failed;

   bool use_mdtm;
   bool use_size;
   bool use_pret;
   bool use_feat;
   bool use_mlsd;

   bool use_telnet_iac;

   bool use_stat;
   int  stat_interval;

   const char *encode_eprt(sockaddr_u *);

   typedef FileInfo *(*FtpLineParser)(char *line,int *err,const char *tz);
   static FtpLineParser line_parsers[];

protected:
   ~Ftp();

public:
   static void ClassInit();

   Ftp();
   Ftp(const Ftp *);

   const char *GetProto() { return "ftp"; }

   FileAccess *Clone() { return new Ftp(this); }
   static FileAccess *New();

   const char *ProtocolSubstitution(const char *host);

   bool	 SameLocationAs(FileAccess *);
   bool	 SameSiteAs(FileAccess *);

   void	 ResetLocationData();

   enum ConnectLevel
   {
      CL_NOT_CONNECTED,
      CL_CONNECTING,
      CL_JUST_CONNECTED,
      CL_NOT_LOGGED_IN,
      CL_LOGGED_IN,
      CL_JUST_BEFORE_DISCONNECT
   };
   ConnectLevel GetConnectLevel();
   int IsConnected()
   {
      return GetConnectLevel()!=CL_NOT_CONNECTED;
   }

   int   Read(void *buf,int size);
   int   Write(const void *buf,int size);
   int   Buffered();
   void  Close();
   bool	 IOReady();

	 // When you are putting a file, call SendEOT to terminate
	 // transfer and then call StoreStatus until OK or error.
   int	 SendEOT();
   int	 StoreStatus();

   int	 Do();
   void  Disconnect();

   void	 SetFlag(int flag,bool val);
   int	 GetFlag(int flag) { return flags&flag; }

   static time_t ConvertFtpDate(const char *);

   const char *CurrentStatus();

   int	 Done();

   enum flag_mask
   {
      SYNC_MODE=1,
      NOREST_MODE=4,
      IO_FLAG=8,
      PASSIVE_MODE=32,

      MODES_MASK=SYNC_MODE|PASSIVE_MODE
   };

   void Reconfig(const char *name=0);
   void Cleanup();
   void CleanupThis();

   ListInfo *MakeListInfo(const char *path);
   Glob *MakeGlob(const char *pattern);
   DirList *MakeDirList(ArgV *args);
   FileSet *ParseLongList(const char *buf,int len,int *err=0);

   void SetCopyMode(copy_mode_t cm,bool rp,int rnum,time_t tt)
      {
	 copy_mode=cm;
	 copy_passive=rp;
	 retries=rnum;
	 try_time=tt;
      }
   bool SetCopyAddress(Ftp *o)
      {
	 if(copy_addr_valid || !o->copy_addr_valid)
	    return false;
	 memcpy(&copy_addr,&o->copy_addr,sizeof(copy_addr));
	 copy_addr_valid=true;
	 return true;
      }
   bool CopyFailed() { return copy_failed; }
   bool RestartFailed() { return flags&NOREST_MODE; }
   bool IsPassive() { return flags&PASSIVE_MODE; }
   bool IsCopyPassive() { return copy_passive; }
   void CopyAllowStore()
      {
	 conn->SendCmd2("STOR",file);
	 expect->Push(new Expect(CHECK_TRANSFER));
	 copy_allow_store=true;
      }
   bool CopyStoreAllowed() { return copy_allow_store; }
   bool CopyIsReadyForStore()
      {
	 if(copy_mode==COPY_SOURCE)
	    return copy_addr_valid && expect->FirstIs(CHECK_TRANSFER);
	 return state==WAITING_STATE && expect->IsEmpty();
      }
};

class FtpS : public Ftp
{
public:
   FtpS();
   FtpS(const FtpS *);
   ~FtpS();

   const char *GetProto() { return "ftps"; }

   FileAccess *Clone() { return new FtpS(this); }
   static FileAccess *New();
};

#endif /* FTPCLASS_H */
