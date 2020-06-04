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

#ifndef FTPCLASS_H
#define FTPCLASS_H

#include "trio.h"
#include <time.h>

#include "NetAccess.h"

#if USE_SSL
# include "lftp_ssl.h"
#endif

class TelnetEncode : public DataTranslator {
   void PutTranslated(Buffer *target,const char *buf,int size);
};
class TelnetDecode : public DataTranslator {
   void PutTranslated(Buffer *target,const char *buf,int size);
};
class IOBufferTelnet : public IOBufferStacked
{
public:
   IOBufferTelnet(IOBuffer *b) : IOBufferStacked(b) {
      if(mode==PUT)
	 SetTranslator(new TelnetEncode());
      else
	 SetTranslator(new TelnetDecode());
   }
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
      DATASOCKET_CONNECTING_STATE, // waiting for data_sock to connect
      WAITING_150_STATE,   // waiting for 150 message
      WAITING_CCC_SHUTDOWN // waiting for the server to shutdown SSL connection
   };

   class Connection
   {
      xstring_c closure;
   public:
      int control_sock;
      SMTaskRef<IOBuffer> control_recv;
      SMTaskRef<IOBuffer> control_send;
      IOBufferTelnet *telnet_layer_send;
      DirectedBuffer send_cmd_buffer; // holds unsent commands.
      int data_sock;
      SMTaskRef<IOBuffer> data_iobuf;
      int aborted_data_sock;
      sockaddr_u peer_sa;
      sockaddr_u data_sa; // address for data accepting
      bool quit_sent;
      bool fixed_pasv;	  // had to fix PASV address.
      bool translation_activated;
      bool proxy_is_http; // true when connection was established via http proxy.
      bool may_show_password;
      bool can_do_pasv;

      int multiline_code; // the code of multiline response.
      int sync_wait;	  // number of commands in flight.
      bool ignore_pass;	  // logged in just with user
      bool try_feat_after_login;
      bool tune_after_login;
      bool utf8_activated; // server is switched to UTF8 mode.
      bool received_150;

      char type;  // type of transfer: 'A'scii or 'I'mage
      char t_mode; // transfer mode: 'S'tream, 'Z'ipped

      bool dos_path;
      bool vms_path;

      bool have_feat_info;
      bool mdtm_supported;
      bool size_supported;
      bool rest_supported;
      bool site_chmod_supported;
      bool site_utime_supported;
      bool site_utime2_supported;   // two-argument SITE UTIME
      bool site_symlink_supported;
      bool site_mkdir_supported;
      bool pret_supported;
      bool utf8_supported;
      bool lang_supported;
      bool mlst_supported;
      bool clnt_supported;
      bool host_supported;
      bool mfmt_supported;
      bool mff_supported;
      bool epsv_supported;
      bool tvfs_supported;
      bool mode_z_supported;
      bool cepr_supported;

      bool ssl_after_proxy;

      off_t last_rest;	// last successful REST position.
      off_t rest_pos;	// the number sent with REST command.

      Timer abor_close_timer;	 // timer for closing aborted connection.
      Timer stat_timer;		 // timer for sending periodic STAT commands.
      Timer waiting_150_timer;   // limit time to wait for 150 reply.
      Timer waiting_ssl_timer;	 // limit time to wait for ssl shutdown

      time_t nop_time;
      off_t  nop_offset;
      int    nop_count;

#if USE_SSL
      Ref<lftp_ssl> control_ssl;
      char prot;  // current data protection scheme 'C'lear or 'P'rivate
      bool auth_sent;
      bool auth_supported;
      bool cpsv_supported;
      bool sscn_supported;
      bool sscn_on;
      xstring auth_args_supported;
      bool ssl_is_activated() { return control_ssl!=0; }
      Timer waiting_ssl_shutdown;
#else
      bool ssl_is_activated() { return false; }
#endif

      xstring_c mlst_attr_supported;
      xstring_c mode_z_opts_supported;

      Connection(const char *c);
      ~Connection();

      bool data_address_ok(const sockaddr_u *d,bool verify_address,bool verify_port);

      void MakeBuffers();
      void MakeSSLBuffers(const char *h);
      void InitTelnetLayer();
      void SetControlConnectionTranslation(const char *cs);

      void CloseDataSocket(); // only closes socket, does not delete iobuf.
      void CloseDataConnection();
      void AbortDataConnection();
      void CloseAbortedDataConnection();

      void Send(const char *cmd);
      void SendURI(const char *u,const char *home);
      void SendCRNL();
      void SendEncoded(const char *url);
      void SendCmd(const char *cmd);
      void SendCmd2(const char *cmd,const char *f,const char *u=0,const char *home=0);
      void SendCmd2(const char *cmd,int v);
      void SendCmdF(const char *fmt,...) PRINTF_LIKE(2,3);
      int FlushSendQueueOneCmd(); // sends single command from send_cmd_buffer

      void AddDataTranslator(DataTranslator *t);
      void AddDataTranslation(const char *charset,bool translit);

      void SuspendInternal()
      {
	 if(control_send) control_send->SuspendSlave();
	 if(control_recv && !data_iobuf) control_recv->SuspendSlave();
	 if(data_iobuf)	data_iobuf->SuspendSlave();
      }
      void ResumeInternal()
      {
	 if(control_send) control_send->ResumeSlave();
	 if(control_recv) control_recv->ResumeSlave();
	 if(data_iobuf)	data_iobuf->ResumeSlave();
      }

      void CheckFEAT(char *reply,const char *line,bool trust);
   };

   Ref<Connection> conn;
   bool last_connection_failed;

   struct Expect
   {
      enum expect_t
      {
	 NONE,		// no special check, reconnect if reply>=400.
	 IGNORE,	// ignore response
	 READY,		// check response after connect
	 REST,		// check response for REST
	 TYPE,		// check response for TYPE
	 MODE,
	 CWD,		// check response for CWD
	 CWD_CURR,	// check response for CWD into current directory
	 CWD_STALE,	// check response for CWD when it's not critical
	 ABOR,		// check response for ABOR
	 SIZE,		// check response for SIZE
	 SIZE_OPT,	// check response for SIZE and save size to *opt_size
	 MDTM,		// check response for MDTM
	 MDTM_OPT,	// check response for MDTM and save size to *opt_date
	 PRET,
	 PASV,		// check response for PASV and save address
	 EPSV,		// check response for EPSV and save address
	 PORT,		// check response for PORT or EPRT
	 FILE_ACCESS,	// generic check for file access
	 PWD,		// check response for PWD and save it to home
	 RNFR,
	 USER,		// check response for USER
	 USER_PROXY,	// check response for USER sent to proxy
	 PASS,		// check response for PASS
	 PASS_PROXY,	// check response for PASS sent to proxy
	 OPEN_PROXY,	// special UserGate proxy command
	 ACCT_PROXY,	// special ACCT with proxy password
	 TRANSFER,	// generic check for transfer
	 TRANSFER_CLOSED, // check for transfer complete when Close()d.
	 FEAT,
	 OPTS_UTF8,
	 LANG,
	 SITE_UTIME,
	 SITE_UTIME2,
	 ALLO,
	 QUOTED		// check response for any command submitted by QUOTE_CMD
#if USE_SSL
	 ,AUTH_TLS,PROT,SSCN,CCC
#endif
      };

      expect_t check_case;
      xstring_c cmd;
      xstring_c arg;
      Expect *next;

      Expect(expect_t e,const char *a=0,const char *c=0) : check_case(e), cmd(c), arg(a) {}
      Expect(expect_t e,char c) : check_case(e)
	 {
	    char cc[2]={c,0};
	    arg.set(cc);
	 }
   };
   class ExpectQueue;
   friend class Ftp::ExpectQueue; // grant access to Expect
   class ExpectQueue
   {
      Expect *first; // next to expect
      Expect **last;  // for appending
      int count;

   public:
      ExpectQueue();
      ~ExpectQueue();

      void Push(Expect *e);
      void Push(Expect::expect_t e);
      Expect *Pop();
      Expect *FindLastCWD() const;
      int Count() const { return count; }
      bool IsEmpty() const { return count==0; }
      bool Has(Expect::expect_t) const;
      bool FirstIs(Expect::expect_t) const;
      void Close();
   };

   Ref<ExpectQueue> expect;

   void  CheckResp(int resp);
   int	 ReplyLogPriority(int code) const;

   void	 RestCheck(int);
   void  NoFileCheck(int);
   void	 TransferCheck(int);
   bool	 Retry530() const;
   void	 LoginCheck(int);
   void	 NoPassReqCheck(int);
   void	 proxy_LoginCheck(int);
   void	 proxy_NoPassReqCheck(int);
   char *ExtractPWD();
   int   SendCWD(const char *path,const char *path_url,Expect::expect_t c);
   void	 CatchDATE(int);
   void	 CatchDATE_opt(int);
   void	 CatchSIZE(int);
   void	 CatchSIZE_opt(int);
   void	 TurnOffStatForList();

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
   pasv_state_t Handle_EPSV_CEPR();

   bool ServerSaid(const char *) const;
   bool NonError5XX(int act) const;
   bool Transient5XX(int act) const;

   void	 InitFtp();

   void	 HandleTimeout();

#if USE_SSL
protected:
   bool	 ftps;	  // ssl and prot='P' by default (port 990)
private:
#else
   static const bool ftps; // for convenience
#endif

   void	DataAbort();
   void DataClose();
   void ControlClose();

   void SendUrgentCmd(const char *cmd);
   int	FlushSendQueueOneCmd();
   int	FlushSendQueue(bool all=false);
   void	SendArrayInfoRequests();
   void	SendSiteIdle();
   void	SendAcct();
   void	SendSiteGroup();
   void	SendSiteCommands();
   void	SendUTimeRequest();
   void SendAuth(const char *auth);
   void TuneConnectionAfterFEAT();
   void SendOPTS_MLST();
   void SendPROT(char want_prot);

   const char *QueryStringWithUserAtHost(const char *);

   int	 ReceiveOneLine();
   // If a response is received, it checks it for accordance with
   // response_queue and switch to a state if necessary
   int	 ReceiveResp();

   bool ProxyIsHttp();
   int	 http_proxy_status_code;
   // Send CONNECT method to http proxy.
   void	 HttpProxySendConnect();
   void	 HttpProxySendConnectData();
   // Send http proxy auth.
   void	 HttpProxySendAuth(const SMTaskRef<IOBuffer>&);
   // Check if proxy returned a reply, returns true if reply is ok.
   // May disconnect.
   bool	 HttpProxyReplyCheck(const SMTaskRef<IOBuffer>&);

   bool	 AbsolutePath(const char *p) const;

   void MoveConnectionHere(Ftp *o);
   bool GetBetterConnection(int level,bool limit_reached);
   bool SameConnection(const Ftp *o) const;

   // state
   automate_state state;
   int flags;
   bool eof;
   Timer retry_timer;

   xstring line;	// last line of last server reply
   xstring all_lines;   // all lines of last server reply

   void	 SetError(int code,const char *mess=0);

   // settings
   xstring_c anon_user;
   xstring_c anon_pass;
   xstring_c charset;
   xstring_c list_options;
   int nop_interval;
   bool verify_data_address;
   bool verify_data_port;
   bool	rest_list;

   xstring_c skey_pass;
   bool allow_skey;
   bool force_skey;
   const char *make_skey_reply();

   xstring netkey_pass;
   bool allow_netkey;
   const char *make_netkey_reply();

   bool disconnect_on_close;

public:
   enum copy_mode_t { COPY_NONE, COPY_SOURCE, COPY_DEST };
private:
   copy_mode_t copy_mode;
   sockaddr_u copy_addr;
   bool copy_addr_valid;
   bool copy_passive;
   bool copy_protect;
   bool copy_ssl_connect;
   bool	copy_done;
   bool	copy_connection_open;
   bool copy_allow_store;
   bool copy_failed;

   bool use_stat;
   bool use_stat_for_list;
   bool use_mdtm;
   bool use_size;
   bool use_feat;
   bool use_mlsd;

   bool use_telnet_iac;

   int max_buf;

   const char *get_protect_res();
   const char *encode_eprt(const sockaddr_u *);

   typedef FileInfo *(*FtpLineParser)(char *line,int *err,const char *tz);
   static FtpLineParser line_parsers[];

   int CanRead();

   const char *path_to_send();

protected:
   void PrepareToDie();

public:
   static void ClassInit();

   Ftp();
   Ftp(const Ftp *);

   const char *GetProto() const { return "ftp"; }

   FileAccess *Clone() const { return new Ftp(this); }
   static FileAccess *New();

   const char *ProtocolSubstitution(const char *host);

   bool	 SameLocationAs(const FileAccess *) const;
   bool	 SameSiteAs(const FileAccess *) const;

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
   ConnectLevel GetConnectLevel() const;
   int IsConnected() const
   {
      return GetConnectLevel()!=CL_NOT_CONNECTED;
   }

   int   Read(Buffer *buf,int size);
   int   Write(const void *buf,int size);
   int   Buffered();
   void  Close();
   bool	 IOReady();

	 // When you are putting a file, call SendEOT to terminate
	 // transfer and then call StoreStatus until OK or error.
   int	 SendEOT();
   int	 StoreStatus();

   int	 Do();
   void  DisconnectLL();
   void  DisconnectNow();

   void	 SetFlag(int flag,bool val);
   int	 GetFlag(int flag) const { return flags&flag; }

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

   ListInfo *MakeListInfo(const char *path);
   Glob *MakeGlob(const char *pattern);
   DirList *MakeDirList(ArgV *args);
   FileSet *ParseLongList(const char *buf,int len,int *err=0) const;

   void SetCopyMode(copy_mode_t cm,bool rp,bool prot,bool sscn,int rnum,time_t tt)
      {
	 copy_mode=cm;
	 copy_passive=rp;
	 copy_protect=prot;
	 copy_ssl_connect=sscn;
	 retries=rnum;
	 SetTryTime(tt);
      }
   bool SetCopyAddress(const Ftp *o)
      {
	 if(copy_addr_valid || !o->copy_addr_valid)
	    return false;
	 memcpy(&copy_addr,&o->copy_addr,sizeof(copy_addr));
	 copy_addr_valid=true;
	 return true;
      }
   bool CopyFailed() const { return copy_failed; }
   bool RestartFailed() const { return flags&NOREST_MODE; }
   bool IsPassive() const { return flags&PASSIVE_MODE; }
   bool IsCopyPassive() const { return copy_passive; }
   void CopyAllowStore()
      {
	 conn->SendCmd2("STOR",file);
	 expect->Push(new Expect(Expect::TRANSFER));
	 copy_allow_store=true;
      }
   bool CopyStoreAllowed() const { return copy_allow_store; }
   bool CopyIsReadyForStore()
      {
	 if(!expect)
	    return false;
	 if(copy_mode==COPY_SOURCE)
	    return copy_addr_valid && expect->FirstIs(Expect::TRANSFER);
	 return state==WAITING_STATE && expect->IsEmpty();
      }
   void CopyCheckTimeout(const Ftp *o)
      {
	 timeout_timer.Reset(o->timeout_timer);
	 CheckTimeout();
      }
   bool AnonymousQuietMode();

   void SuspendInternal();
   void ResumeInternal();
};

class FtpS : public Ftp
{
public:
   FtpS();
   FtpS(const FtpS *);
   ~FtpS();

   const char *GetProto() const { return "ftps"; }

   FileAccess *Clone() const { return new FtpS(this); }
   static FileAccess *New();
};

#endif /* FTPCLASS_H */
