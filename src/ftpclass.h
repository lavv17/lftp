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

   enum response
   {
      // only first digit of these responses should be used (RFC1123)
      RESP_READY=220,
      RESP_PASS_REQ=331,
      RESP_LOGGED_IN=230,
      RESP_CWD_RMD_DELE_OK=250,
      RESP_TYPE_OK=200,
      RESP_PORT_OK=200,
      RESP_REST_OK=350,
      RESP_TRANSFER_OK=226,
      RESP_RESULT_HERE=213,
      RESP_PWD_MKD_OK=257,
      RESP_NOT_IMPLEMENTED=502,
      RESP_NOT_UNDERSTOOD=500,
      RESP_NO_FILE=550,
      RESP_PERM_DENIED=553,
      RESP_BROKEN_PIPE=426,
      RESP_LOGIN_FAILED=530
   };

   enum check_case_t
   {
      CHECK_NONE,	// no special check
      CHECK_IGNORE,	// ignore response
      CHECK_READY,	// check response after connect
      CHECK_REST,	// check response for REST
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
      bool quit_sent;
      bool fixed_pasv; // had to fix PASV address.
      bool translation_activated;

      int multiline_code; // the code of multiline response.
      int sync_wait; // number of commands in flight.

#ifdef USE_SSL
      SSL *control_ssl;
      SSL *data_ssl;
      char prot;  // current data protection scheme 'C'lear or 'P'rivate
      bool auth_sent;
#endif

      Connection();
      ~Connection();

      bool data_address_ok(sockaddr_u *d,bool verify_address,bool verify_port);
      void SavePeerAddress();

      void MakeBuffers();
      void MakeSSLBuffers(const char *h);
      void InitTelnetLayer();
      void SetControlConnectionTranslation(const char *cs);

      void CloseDataConnection();
   };

   Connection *conn;

   struct expected_response;
   friend struct Ftp::expected_response;
   struct expected_response
   {
      int   expect;
      check_case_t check_case;
      char  *path;
   };
   expected_response *RespQueue;
   int	 RQ_alloc;   // memory allocated

   int	 RQ_head;
   int	 RQ_tail;

   void  AddResp(int exp,check_case_t ck=CHECK_NONE);
   void  SetRespPath(const char *p);
   void  CheckResp(int resp);
   int	 ReplyLogPriority(int code);
   void  PopResp();
   void	 EmptyRespQueue();
   void	 CloseRespQueue(); // treat responses on Close()
   int   RespQueueIsEmpty() { return RQ_head==RQ_tail; }
   int	 RespQueueSize() { return RQ_tail-RQ_head; }
   expected_response *FindLastCWD();
   bool	 RespQueueHas(check_case_t cc);

   void	 RestCheck(int);
   void  NoFileCheck(int);
   void	 TransferCheck(int);
   void	 LoginCheck(int);
   void	 NoPassReqCheck(int);
   void	 proxy_LoginCheck(int);
   void	 proxy_NoPassReqCheck(int);
   void	 CheckFEAT(char *reply);
   char *ExtractPWD();
   void  SendCWD(const char *path,check_case_t c);
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

   /* type of transfer: TYPE_A or TYPE_I */
   int   type;

   /* address for data accepting */
   sockaddr_u   data_sa;

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

   void	 SwitchToState(automate_state);

   void	 Send(const char *cmd,int len);
   void  SendCmd(const char *cmd);
   void  SendCmd2(const char *cmd,const char *f);
   void  SendCmd2(const char *cmd,int v);
   void  SendUrgentCmd(const char *cmd);
   int	 FlushSendQueueOneCmd();
   int	 FlushSendQueue(bool all=false);
   void	 SendArrayInfoRequests();
   void	 SendSiteIdle();
   void	 SendAcct();
   void	 SendSiteGroup();
   void	 SendUTimeRequest();

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
   bool	 proxy_is_http;	// true when connection was established via http proxy.
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

   bool  dos_path;
   bool  vms_path;
   bool  mdtm_supported;
   bool  size_supported;
   bool  rest_supported;
   bool  site_chmod_supported;
   bool  site_utime_supported;
   bool	 pret_supported;
   bool	 utf8_supported;
   bool	 lang_supported;
#ifdef USE_SSL
   bool	 auth_supported;
   char	 *auth_args_supported;
#endif
   off_t last_rest;	// last successful REST position.
   off_t rest_pos;	// the number sent with REST command.

   void	 SetError(int code,const char *mess=0);

   bool	 wait_flush;	// wait until all responces come
   bool	 ignore_pass;	// logged in just with user
   bool	 try_feat_after_login;
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
      DOSISH_PATH=16,
      PASSIVE_MODE=32,

      MODES_MASK=SYNC_MODE|PASSIVE_MODE|DOSISH_PATH
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
	 SendCmd2("STOR",file);
	 AddResp(RESP_TRANSFER_OK,CHECK_TRANSFER);
	 copy_allow_store=true;
      }
   bool CopyStoreAllowed() { return copy_allow_store; }
   bool CopyIsReadyForStore()
      {
	 if(copy_mode==COPY_SOURCE)
	    return copy_addr_valid && RespQueueSize()==1;
	 return state==WAITING_STATE && RespQueueSize()==0;
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
