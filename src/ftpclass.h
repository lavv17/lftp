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

#ifndef FTPCLASS_H
#define FTPCLASS_H

#include <stdio.h>
#include <time.h>

#include "Resolver.h"
#include "FileAccess.h"
#include "ResMgr.h"

class Ftp : public FileAccess
{
   static Ftp *ftp_chain;
   Ftp *ftp_next;

   enum automate_state
   {
      EOF_STATE,	   // control connection is open, idle state
      INITIAL_STATE,	   // all connections are closed
      CONNECTING_STATE,	   // we are connecting the control socket
      WAITING_STATE,	   // we're waiting for a response with data
      ACCEPTING_STATE,	   // we're waiting for an incoming data connection
      DATA_OPEN_STATE,	   // data connection is open, for read or write
      FATAL_STATE,	   // fatal error occured (e.g. REST not implemented)
      NO_FILE_STATE,	   // file access is impossible - no such file
      NO_HOST_STATE,	   // host not found or not specified
      STORE_FAILED_STATE,  // STOR failed - you have to reput
      LOGIN_FAILED_STATE,  // login failed due to invalid password
      SYSTEM_ERROR_STATE,  // system error occured, errno saved to saved_errno
      LOOKUP_ERROR_STATE,  // unsuccessful host name lookup
      CWD_CWD_WAITING_STATE,  // waiting until 'CWD $cwd' finishes
      USER_RESP_WAITING_STATE,// waiting until 'USER $user' finishes
      DATASOCKET_CONNECTING_STATE   // waiting for data_sock to connect
   };

   enum response
   {
      RESP_READY=220,
      RESP_PASS_REQ=331,
      RESP_LOGGED_IN=230,
      RESP_CWD_RMD_DELE_OK=250,
      RESP_TYPE_OK=200,
      RESP_PORT_OK=200,
      RESP_REST_OK=350,
      //RESP_DATA_OPEN=150,
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

   struct expected_response
   {
      int   expect;
      int   fail_state;
      int   (Ftp::*check_resp)(int actual,int expect);
      bool  log_resp;
   }
      *RespQueue;
   int	 RQ_alloc;   // memory allocated

   int	 RQ_head;
   int	 RQ_tail;

   int	 multiline_code;

   void	 LogResp(char *line);

   void  AddResp(int exp,int fail,int (Ftp::*ck)(int,int)=0,bool log=false);
   int   CheckResp(int resp);
   void  PopResp();
   void	 EmptyRespQueue();
   int   RespQueueIsEmpty() { return RQ_head==RQ_tail; }
   int	 RespQueueSize() { return RQ_tail-RQ_head; }

   int	 IgnoreCheck(int act,int exp);
   int	 RestCheck(int act,int exp);
   int   NoFileCheck(int act,int exp);
   int	 CWD_Check(int,int);
   int	 CwdCwd_Check(int,int);
   int	 TransferCheck(int,int);
   int	 LoginCheck(int,int);
   int	 NoPassReqCheck(int,int);
   int	 proxy_LoginCheck(int,int);
   int	 proxy_NoPassReqCheck(int,int);
   int	 CatchPWDResponse(int,int);
   int	 CatchHomePWD(int,int);
   char  *ExtractPWD();
   int	 RNFR_Check(int,int);
   int	 CatchDATE(int,int);
   int	 CatchDATE_opt(int,int);
   int	 CatchSIZE(int,int);
   int	 CatchSIZE_opt(int,int);
   int	 PASV_Catch(int,int);

   void	 InitFtp();

   int   control_sock;
   int   data_sock;

   /* type of transfer: TYPE_A or TYPE_I */
   int   type;

   Resolver *resolver;

   /* address to connect */
   struct sockaddr_in   peer_sa;
   /* address for data accepting */
   struct sockaddr_in   data_sa;

   char  *resp;
   int   resp_size;
   int	 resp_alloc;
   char  *line;

   time_t   nop_time;
   long	    nop_offset;
   int	    nop_count;

   int	 CheckTimeout();

   char	 *result;
   int	 result_size;

   void  DataClose();

   void	 SwitchToState(automate_state);

   void  SendCmd(const char *cmd);
   void	 FlushSendQueue();

   void	 ReceiveResp();
	 // If a response is received, it checks it for accordance with
	 // response_queue and switch to a state if necessary

   int	 Poll(int fd,int ev) { return FileAccess::Poll(fd,ev); }
   int	 Poll();

   int	 AbsolutePath(const char *p);

   char	 *send_cmd_buffer;
   int   send_cmd_count;   // in buffer
   int	 send_cmd_alloc;   // total allocated
   char  *send_cmd_ptr;	   // start

   automate_state state;

   char	 *anon_user;
   char	 *anon_pass;
   char	 *proxy;
   int	 proxy_port;
   char  *proxy_user;
   char  *proxy_pass;

   static const char *DefaultAnonPass();
   void	 SetProxy(const char *px);

   int	 flags;

   int	 StateToError();

   int	 addr_received;	// state of PASV

   bool	 lookup_done:1;
   bool	 relookup_always:1;
   bool	 wait_flush:1;	// wait until all responces come
   bool	 ignore_pass:1;	// logged in just with user
   bool  verify_data_address:1;

   void	 GetBetterConnection(int level);
   bool  SameConnection(const Ftp *o);

   int	 nop_interval;

   int	 max_retries;
   int	 retries;

   int	 idle;
   time_t idle_start;
   void set_idle_start()
      {
	 time(&idle_start);
	 if(control_sock!=-1 && idle>0)
	    block+=TimeOut(idle*1000);
      }

   char *target_cwd;

   char *skey_pass;
   bool allow_skey;
   bool force_skey;
   const char *make_skey_reply();

   int	 socket_buffer;
   void	 SetSocketBuffer(int sock);
   static void SetKeepAlive(int sock);

   bool data_address_ok();

public:
   static void ClassInit();

   Ftp();
   Ftp(const Ftp *);
   ~Ftp();

   const char *GetProto() { return "ftp"; }

   FileAccess *Clone() { return new Ftp(this); }
   static FileAccess *New() { return new Ftp(); }

   bool	 SameLocationAs(FileAccess *);

   bool	 IsConnected()
   {
      if(control_sock==-1)
	 return false;
      Do();
      return(control_sock!=-1 && state!=CONNECTING_STATE);
   }
   bool	 IsBetterThan(FileAccess *fa)
   {
      return(SameProtoAs(fa)
	     && this->IsConnected() && !((Ftp*)fa)->IsConnected());
   }

   void	 Connect(const char *,int);
   void	 ConnectVerify();

   void	 Login(const char*,const char*);

   int   Read(void *buf,int size);
   int   Write(const void *buf,int size);
   void  Close();

	 // When you are putting a file, call SendEOT to terminate
	 // transfer and then call StoreStatus until OK or error.
   int	 SendEOT();
   int	 StoreStatus();

   int	 Block();
   int	 DataReady();
   int	 Do();
   void  Disconnect();

   void	 SetFlag(int flag,bool val);
   int	 GetFlag(int flag) { return flags&flag; }

   static time_t ConvertFtpDate(const char *);

   const char *CurrentStatus();

   int	 ChdirStatus();

   int	 Done();

   enum flag_mask
   {
      SYNC_MODE=1,
      SYNC_WAIT=2,
      NOREST_MODE=4,
      IO_FLAG=8,
      DOSISH_PATH=16,
      PASSIVE_MODE=32,

      MODES_MASK=SYNC_MODE|PASSIVE_MODE|DOSISH_PATH
   };

   void CopyOptions(FileAccess *fa)
   {
      if(SameProtoAs(fa))
      {
	 flags=(flags&~MODES_MASK)|(((Ftp*)fa)->flags&MODES_MASK);
	 if(((Ftp*)fa)->try_time==0)
	    try_time=0;
      }
   }

   void Reconfig();
   void Cleanup(bool all);
   class ListInfo *MakeListInfo();
};

#endif /* FTPCLASS_H */
