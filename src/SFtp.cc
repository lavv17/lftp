/*
 * lftp - file transfer program
 *
 * Copyright (c) 2003-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "SFtp.h"
#include "ArgV.h"
#include "log.h"
#include "ascii_ctype.h"
#include "FileGlob.h"
#include "misc.h"
#include "LsCache.h"

#include <assert.h>
#include <errno.h>

#define max_buf 0x10000

#define super NetAccess

bool SFtp::GetBetterConnection(int level,bool limit_reached)
{
   bool need_sleep=false;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      SFtp *o=(SFtp*)fo; // we are sure it is SFtp.

      if(!o->recv_buf)
	 continue;

      if(o->state!=CONNECTED || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || (o->priority>=priority && !o->IsSuspended()))
	    continue;
	 o->Disconnect();
	 return need_sleep;
      }

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      // borrow the connection
      MoveConnectionHere(o);
      break;
   }
   return need_sleep;
}

int SFtp::Do()
{
   int m=STALL;
   int count;
   const char *b;
   int s;

   // check if idle time exceeded
   if(mode==CLOSED && send_buf && idle_timer.Stopped())
   {
      LogNote(1,_("Closing idle connection"));
      Disconnect();
      return m;
   }

   if(Error())
      return m;

   if(!hostname)
      return m;

   if(send_buf && send_buf->Error())
   {
      LogError(0,"send: %s",send_buf->ErrorText());
      Disconnect();
      return MOVED;
   }
   m|=HandleReplies();

   if(Error())
      return m;

   if(send_buf)
      timeout_timer.Reset(send_buf->EventTime());
   if(recv_buf)
      timeout_timer.Reset(recv_buf->EventTime());
   if(pty_send_buf)
      timeout_timer.Reset(pty_send_buf->EventTime());
   if(pty_recv_buf)
      timeout_timer.Reset(pty_recv_buf->EventTime());

   // check for timeout only if there should be connection activity.
   if(state!=DISCONNECTED && state!=CONNECTED
   && mode!=CLOSED && CheckTimeout())
      return MOVED;

   if((state==FILE_RECV || state==FILE_SEND)
   && rate_limit==0)
      rate_limit=new RateLimit(hostname);

   switch(state)
   {
   case DISCONNECTED:
   {
      if(mode==CLOSED)
	 return m;
      if(mode==CONNECT_VERIFY)
	 return m;

      // walk through SFtp classes and try to find identical idle session
      // first try "easy" cases of session take-over.
      count=CountConnections();
      for(int i=0; i<3; i++)
      {
	 bool limit_reached=(connection_limit>0
			    && connection_limit<=CountConnections());
	 if(i>=2 && !limit_reached)
	    break;
	 bool need_sleep=GetBetterConnection(i,limit_reached);
	 if(state!=DISCONNECTED)
	    return MOVED;
	 if(need_sleep)
	    return m;
      }

      if(!ReconnectAllowed())
	 return m;

      if(!NextTry())
	 return MOVED;

      const char *init=Query("server-program",hostname);
      const char *prog=Query("connect-program",hostname);
      if(!prog || !prog[0])
	 prog="ssh -a -x";
      char *a=alloca_strdup(prog);
      ArgV *cmd=new ArgV;
      for(a=strtok(a," "); a; a=strtok(0," "))
	 cmd->Add(a);
      if(!strchr(init,'/'))
      {
	 if(init[0])
	    cmd->Add("-s");   // run ssh2 subsystem
	 // sftpd does not have a greeting
	 received_greeting=true;
      }
      else
	 init=xstring::cat("echo SFTP: >&2;",init,NULL);
      if(user)
      {
	 cmd->Add("-l");
	 cmd->Add(user);
      }
      if(portname)
      {
	 cmd->Add("-p");
	 cmd->Add(portname);
      }
      cmd->Add(hostname);
      if(init[0])
	 cmd->Add(init);
      xstring_ca cmd_str(cmd->Combine(0));
      LogNote(9,"%s (%s)",_("Running connect program"),cmd_str.get());
      ssh=new PtyShell(cmd);
      ssh->UsePipes();
      state=CONNECTING;
      timeout_timer.Reset();
      m=MOVED;
   }
   case CONNECTING:
   {
      int fd=ssh->getfd();
      if(fd==-1)
      {
	 if(ssh->error())
	 {
	    SetError(FATAL,ssh->error_text);
	    return MOVED;
	 }
	 TimeoutS(1);
	 return m;
      }
      int pipe_in =ssh->getfd_pipe_in();
      int pipe_out=ssh->getfd_pipe_out();
      ssh->Kill(SIGCONT);
      pty_send_buf=new IOBufferFDStream(ssh.borrow(),IOBuffer::PUT);
      pty_recv_buf=new IOBufferFDStream(new FDStream(fd,"pseudo-tty"),IOBuffer::GET);
      send_buf=new IOBufferFDStream(new FDStream(pipe_out,"pipe-out"),IOBuffer::PUT);
      recv_buf=new IOBufferFDStream(new FDStream(pipe_in,"pipe-in"),IOBuffer::GET);
      set_real_cwd("~");
      state=CONNECTING_1;
      m=MOVED;
   }
   case CONNECTING_1:
      if(!received_greeting)
	 return m;
      SendRequest(new Request_INIT(Query("protocol-version",hostname)),Expect::FXP_VERSION);
      state=CONNECTING_2;
      return MOVED;

   case CONNECTING_2:
      if(protocol_version==0)
	 return m;
      if(home_auto==0)
	 SendRequest(new Request_REALPATH("."),Expect::HOME_PATH);
      state=CONNECTED;
      m=MOVED;

   case CONNECTED:
      if(home.path==0 && !RespQueueIsEmpty())
	 return m;

      if(mode==CLOSED)
	 return m;

      SendRequest();
      return MOVED;

   case FILE_RECV:
      if(file_buf->Size()>=rate_limit->BytesAllowedToGet())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(file_buf->Size()>=max_buf)
      {
	 recv_buf->Suspend();
	 m=MOVED;
      }
      else if(recv_buf->IsSuspended())
      {
	 recv_buf->Resume();
	 if(recv_buf->Size()>0 || (recv_buf->Size()==0 && recv_buf->Eof()))
	    m=MOVED;
      }
      break;
   case FILE_SEND:
      // pack data from file_buf.
      file_buf->Get(&b,&s);
      if(s==0 && !eof)
	 return m;
      if(s<size_write && !eof && !flush_timer.Stopped())
	 return m;   // wait for more data before sending.
      if(RespQueueSize()>max_packets_in_flight)
	 return m;
      if(s==0)
      {
	 // no more data, set attributes and close the file.
	 Request_FSETSTAT *req=new Request_FSETSTAT(handle,protocol_version);
	 req->attrs.mtime=entity_date;
	 req->attrs.flags|=SSH_FILEXFER_ATTR_MODIFYTIME;
	 SendRequest(req,Expect::IGNORE);
	 CloseHandle(Expect::DEFAULT);
	 state=WAITING;
	 m=MOVED;
	 break;
      }
      if(s>size_write)
	 s=size_write;
      SendRequest(new Request_WRITE(handle,request_pos,b,s),Expect::WRITE_STATUS);
      file_buf->Skip(s);
      request_pos+=s;
      flush_timer.Reset();
      m=MOVED;
      break;
   case WAITING:
      if(mode==ARRAY_INFO)
	 SendArrayInfoRequests();
      break;
   case DONE:
      break;
   }
   return m;
}

void SFtp::MoveConnectionHere(SFtp *o)
{
   protocol_version=o->protocol_version;
   recv_translate=o->recv_translate.borrow();
   send_translate=o->send_translate.borrow();
   send_buf=o->send_buf.borrow();
   recv_buf=o->recv_buf.borrow();
   pty_send_buf=o->pty_send_buf.borrow();
   pty_recv_buf=o->pty_recv_buf.borrow();
   rate_limit=o->rate_limit.borrow();
   expect_queue_size=o->expect_queue_size; o->expect_queue_size=0;
   expect_chain=o->expect_chain; o->expect_chain=0;
   expect_chain_end=o->expect_chain_end;
   if(expect_chain_end==&o->expect_chain)
      expect_chain_end=&expect_chain;
   o->expect_chain_end=&o->expect_chain;
   timeout_timer.Reset(o->timeout_timer);
   ssh_id=o->ssh_id;
   state=CONNECTED;
   o->Disconnect();
   if(!home)
      set_home(home_auto);
   ResumeInternal();
}

void SFtp::Disconnect()
{
   if(send_buf)
      LogNote(9,_("Disconnecting"));
   handle.set(0);
   send_buf=0;
   recv_buf=0;
   pty_send_buf=0;
   pty_recv_buf=0;
   file_buf=0;
   ssh=0;
   EmptyRespQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED);
   received_greeting=false;
   password_sent=0;
   protocol_version=0;
   send_translate=0;
   recv_translate=0;
   ssh_id=0;
   home_auto.set(FindHomeAuto());
}

void SFtp::Init()
{
   state=DISCONNECTED;
   ssh_id=0;
   eof=false;
   received_greeting=false;
   password_sent=0;
   expect_queue_size=0;
   expect_chain=0;
   expect_chain_end=&expect_chain;
   ooo_chain=0;
   protocol_version=0;
   send_translate=0;
   recv_translate=0;
   max_packets_in_flight=16;
   max_packets_in_flight_slow_start=1;
   size_read=0x8000;
   size_write=0x8000;
   flush_timer.Set(0,500);
}

SFtp::SFtp()
{
   Init();
   Reconfig(0);
}

SFtp::~SFtp()
{
   Disconnect();
   Close();
}

SFtp::SFtp(const SFtp *o) : super(o)
{
   Init();
   Reconfig(0);
}


bool SFtp::Packet::HasID()
{
   return(type!=SSH_FXP_INIT && type!=SSH_FXP_VERSION);
}

void SFtp::Packet::PackString(Buffer *b,const char *str,int len)
{
   if(len==-1)
      len=strlen(str);
   b->PackUINT32BE(len);
   b->Put(str,len);
}
SFtp::unpack_status_t SFtp::Packet::UnpackString(const Buffer *b,int *offset,int limit,xstring *str_out)
{
   if(limit-*offset<4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;

   int len=b->UnpackUINT32BE(*offset);
   if(len>limit-*offset-4)
   {
      LogError(2,"bad string in reply (invalid length field)");
      return UNPACK_WRONG_FORMAT;
   }
   *offset+=4;

   const char *data;
   int data_len;
   b->Get(&data,&data_len);

   str_out->nset(data+*offset,len);

   *offset+=len;

   return UNPACK_SUCCESS;
}

SFtp::unpack_status_t SFtp::Packet::Unpack(const Buffer *b)
{
   unpacked=0;
   if(b->Size()<4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   length=b->UnpackUINT32BE(0);
   unpacked+=4;
   if(length<1)
      return UNPACK_WRONG_FORMAT;
   if(b->Size()<length+4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   int t=b->UnpackUINT8(4);
   unpacked++;
   if(!is_valid_reply(t))
      return UNPACK_WRONG_FORMAT;
   type=(packet_type)t;
   if(HasID())
   {
      if(length<5)
	 return UNPACK_WRONG_FORMAT;
      id=b->UnpackUINT32BE(5);
      unpacked+=4;
   }
   else
   {
      id=0;
   }
   return UNPACK_SUCCESS;
}

SFtp::unpack_status_t SFtp::UnpackPacket(Buffer *b,SFtp::Packet **p)
{
   *p=0;
   Packet *&pp=*p;

   Packet probe;
   unpack_status_t res=probe.Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;

   Log::global->Format(9,"<--- got a packet, length=%d, type=%d(%s), id=%u\n",
      probe.GetLength(),probe.GetPacketType(),probe.GetPacketTypeText(),probe.GetID());

   switch(probe.GetPacketType())
   {
   case SSH_FXP_VERSION:
      pp=new Reply_VERSION();
      break;
   case SSH_FXP_NAME:
      pp=new Reply_NAME(protocol_version);
      break;
   case SSH_FXP_ATTRS:
      pp=new Reply_ATTRS(protocol_version);
      break;
   case SSH_FXP_STATUS:
      pp=new Reply_STATUS(protocol_version);
      break;
   case SSH_FXP_HANDLE:
      pp=new Reply_HANDLE();
      break;
   case SSH_FXP_DATA:
      pp=new Reply_DATA();
      break;
   case SSH_FXP_INIT:
   case SSH_FXP_OPEN:
   case SSH_FXP_CLOSE:
   case SSH_FXP_READ:
   case SSH_FXP_WRITE:
   case SSH_FXP_LSTAT:
   case SSH_FXP_FSTAT:
   case SSH_FXP_SETSTAT:
   case SSH_FXP_FSETSTAT:
   case SSH_FXP_OPENDIR:
   case SSH_FXP_READDIR:
   case SSH_FXP_REMOVE:
   case SSH_FXP_MKDIR:
   case SSH_FXP_RMDIR:
   case SSH_FXP_REALPATH:
   case SSH_FXP_STAT:
   case SSH_FXP_RENAME:
   case SSH_FXP_READLINK:
   case SSH_FXP_SYMLINK:
   case SSH_FXP_LINK:
   case SSH_FXP_BLOCK:
   case SSH_FXP_UNBLOCK:
   case SSH_FXP_EXTENDED:
      LogError(0,"request in reply??");
      return UNPACK_WRONG_FORMAT;
   case SSH_FXP_EXTENDED_REPLY:
      LogError(0,"unexpected SSH_FXP_EXTENDED_REPLY");
      return UNPACK_WRONG_FORMAT;
   }
   res=pp->Unpack(b);
   if(res!=UNPACK_SUCCESS)
   {
      switch(res)
      {
      case UNPACK_PREMATURE_EOF:
	 LogError(0,"premature eof");
	 break;
      case UNPACK_WRONG_FORMAT:
	 LogError(0,"wrong packet format");
	 break;
      case UNPACK_NO_DATA_YET:
      case UNPACK_SUCCESS:
	 ;
      }
      probe.DropData(b);
      delete *p;
      *p=0;
   }
   return res;
}

void SFtp::SendRequest(Packet *request,Expect::expect_t tag,int i)
{
   request->SetID(ssh_id++);
   request->ComputeLength();
   Log::global->Format(9,"---> sending a packet, length=%d, type=%d(%s), id=%u\n",
      request->GetLength(),request->GetPacketType(),request->GetPacketTypeText(),request->GetID());
   request->Pack(send_buf.get_non_const());
   PushExpect(new Expect(request,tag,i));
}

const char *SFtp::SkipHome(const char *path)
{
   if(path[0]=='~' && path[1]=='/' && path[2])
      return path+2;
   if(path[0]=='~' && !path[1])
      return ".";
   if(!home)
      return path;
   int home_len=home.path.length();
   if(strncmp(home,path,home_len))
      return path;
   if(path[home_len]=='/' && path[home_len+1] && path[home_len+1]!='/')
      return path+home_len+1;
   if(!path[home_len])
      return ".";
   return path;
}
const char *SFtp::WirePath(const char *path)
{
   path=SkipHome(dir_file(cwd,path));
   LogNote(9,"path on wire is `%s'",path);
   return lc_to_utf8(path);
}

void SFtp::SendRequest()
{
   max_packets_in_flight_slow_start=1;
   ExpandTildeInCWD();
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      SendRequest(new Request_STAT(lc_to_utf8(file),0,protocol_version),Expect::CWD);
      SendRequest(new Request_STAT(lc_to_utf8(dir_file(file,".")),0,protocol_version),Expect::CWD);
      state=WAITING;
      break;
   case RETRIEVE:
      SendRequest(new Request_OPEN(WirePath(file),SSH_FXF_READ,
	 ACE4_READ_DATA|ACE4_READ_ATTRIBUTES,SSH_FXF_OPEN_EXISTING,protocol_version),Expect::HANDLE);
      state=WAITING;
      break;
   case LIST:
   case LONG_LIST:
      SendRequest(new Request_OPENDIR(WirePath(file)),Expect::HANDLE);
      state=WAITING;
      break;
   case STORE:
      SendRequest(
	 new Request_OPEN(WirePath(file),
	    SSH_FXF_WRITE|SSH_FXF_CREAT|(pos==0?SSH_FXF_TRUNC:0),
	    ACE4_WRITE_DATA|ACE4_WRITE_ATTRIBUTES,
	    pos==0?SSH_FXF_CREATE_TRUNCATE:SSH_FXF_OPEN_OR_CREATE,
	    protocol_version),
	 Expect::HANDLE);
      state=WAITING;
      break;
   case ARRAY_INFO:
      state=WAITING;
      break;
   case RENAME:
   {
      if(protocol_version<3)
      {
	 SetError(NOT_SUPP);
	 break;
      }
      char *file1_wire_path=alloca_strdup(WirePath(file1));
      SendRequest(new Request_RENAME(WirePath(file),file1_wire_path,
			SSH_FXF_RENAME_NATIVE,protocol_version),Expect::DEFAULT);
      state=WAITING;
      break;
   }
   case CHANGE_MODE:
   {
      Request_SETSTAT *req=new Request_SETSTAT(WirePath(file),protocol_version);
      req->attrs.permissions=chmod_mode;
      req->attrs.flags|=SSH_FILEXFER_ATTR_PERMISSIONS;
      SendRequest(req,Expect::DEFAULT);
      state=WAITING;
      break;
   }
   case MAKE_DIR:
      SendRequest(new Request_MKDIR(WirePath(file),protocol_version),Expect::DEFAULT);
      state=WAITING;
      break;
   case REMOVE_DIR:
      SendRequest(new Request_RMDIR(WirePath(file)),Expect::DEFAULT);
      state=WAITING;
      break;
   case REMOVE:
      SendRequest(new Request_REMOVE(WirePath(file)),Expect::DEFAULT);
      state=WAITING;
      break;
   case QUOTE_CMD:
   case MP_LIST:
      SetError(NOT_SUPP);
      break;
   case CONNECT_VERIFY:
   case CLOSED:
      abort();
   }
}

void SFtp::SendArrayInfoRequests()
{
   while(array_ptr<array_cnt && RespQueueSize()<max_packets_in_flight)
   {
      SendRequest(new Request_STAT(lc_to_utf8(dir_file(cwd,
	       array_for_info[array_ptr].file)),
	 SSH_FILEXFER_ATTR_SIZE|SSH_FILEXFER_ATTR_MODIFYTIME,
	 protocol_version),Expect::INFO,array_ptr);
      array_ptr++;
   }
   if(RespQueueIsEmpty())
      state=DONE;
}

void SFtp::CloseHandle(Expect::expect_t c)
{
   if(handle)
   {
      SendRequest(new Request_CLOSE(handle),c);
      handle.set(0);
   }
}

void SFtp::Close()
{
   switch(state)
   {
   case(DISCONNECTED):
   case(WAITING):
   case(CONNECTED):
   case(DONE):
   case(FILE_RECV):
   case(FILE_SEND):
      break;
   case(CONNECTING):
   case(CONNECTING_1):
   case(CONNECTING_2):
      Disconnect();
   }
   CloseExpectQueue();
   state=(recv_buf?CONNECTED:DISCONNECTED);
   eof=false;
   file_buf=0;
   file_set=0;
   CloseHandle(Expect::IGNORE);
   super::Close();
   // don't need these out-of-order packets anymore
   while(ooo_chain)
      DeleteExpect(&ooo_chain);
   if(recv_buf)
      recv_buf->Resume();
}

int SFtp::HandlePty()
{
   int m=STALL;
   if(pty_recv_buf==0)
      return m;

   const char *b;
   int s;
   pty_recv_buf->Get(&b,&s);
   const char *eol=(const char*)memchr(b,'\n',s);
   if(!eol)
   {
      const char *p="password:";
      const char *y="(yes/no)?";
      int p_len=strlen(p);
      int y_len=strlen(y);
      if(s>0 && b[s-1]==' ')
	 s--;
      if(s>=p_len && !strncasecmp(b+s-p_len,p,p_len)
      || s>10 && !strncmp(b+s-2,"':",2))
      {
	 if(!pass)
	 {
	    SetError(LOGIN_FAILED,_("Password required"));
	    return MOVED;
	 }
	 if(password_sent>1)
	 {
	    SetError(LOGIN_FAILED,_("Login incorrect"));
	    return MOVED;
	 }
	 pty_recv_buf->Put("XXXX");
	 pty_send_buf->Put(pass);
	 pty_send_buf->Put("\n");
	 password_sent++;
	 return m;
      }
      if(s>=y_len && !strncasecmp(b+s-y_len,y,y_len))
      {
	 pty_recv_buf->Put("yes\n");
	 pty_send_buf->Put("yes\n");
	 return m;
      }
      if(pty_recv_buf->Eof())
	 LogError(0,_("Peer closed connection"));
      if(pty_recv_buf->Error())
	 LogError(0,"pty read: %s",pty_recv_buf->ErrorText());
      if(pty_recv_buf->Eof() || pty_recv_buf->Error())
      {
	 Disconnect();
	 m=MOVED;
      }
      return m;
   }
   m=MOVED;
   s=eol-b+1;
   char *line=string_alloca(s);
   memcpy(line,b,s-1);
   line[s-1]=0;
   pty_recv_buf->Skip(s);

   LogRecv(4,line);
   if(!received_greeting && !strcmp(line,"SFTP:"))
      received_greeting=true;

   return m;
}

void SFtp::HandleExpect(Expect *e)
{
   const Packet *reply=e->reply;
   switch(e->tag)
   {
   case Expect::FXP_VERSION:
      if(reply->TypeIs(SSH_FXP_VERSION))
      {
	 protocol_version=((Reply_VERSION*)reply)->GetVersion();
	 LogNote(9,"protocol version set to %d",protocol_version);
	 const char *charset=0;
	 if(protocol_version>=4)
	    charset="UTF-8";
	 else
	    charset=ResMgr::Query("sftp:charset",hostname);
	 if(charset && *charset)
	 {
	    send_translate=new DirectedBuffer(DirectedBuffer::PUT);
	    recv_translate=new DirectedBuffer(DirectedBuffer::GET);
	    send_translate->SetTranslation(charset,false);
	    recv_translate->SetTranslation(charset,true);
	 }
      }
      else
      {
	 Disconnect();
	 SetError(FATAL,"cannot negotiate protocol version");
      }
      break;
   case Expect::HOME_PATH:
      if(reply->TypeIs(SSH_FXP_NAME))
      {
	 Reply_NAME *r=(Reply_NAME*)reply;
	 const NameAttrs *a=r->GetNameAttrs(0);
	 if(a && !home_auto)
	 {
	    home_auto.set(utf8_to_lc(a->name));
	    LogNote(9,"home set to %s",home_auto.get());
	    PropagateHomeAuto();
	    if(!home)
	       set_home(home_auto);
	    cache->SetDirectory(this, home, true);
	 }
      }
      break;
   case Expect::CWD:
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 const FileAttrs *a=((Reply_ATTRS*)reply)->GetAttrs();
	 if(a->type!=SSH_FILEXFER_TYPE_DIRECTORY)
	 {
	    cache->SetDirectory(this,cwd,false);
	    SetError(NO_FILE,strerror(ENOTDIR));
	    break;
	 }
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    cwd.Set(file);
	    eof=true;
	    cache->SetDirectory(this,cwd,true);
	 }
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case Expect::HANDLE:
      if(reply->TypeIs(SSH_FXP_HANDLE))
      {
	 handle.set(((Reply_HANDLE*)reply)->GetHandle());
	 state=(mode==STORE?FILE_SEND:FILE_RECV);
	 file_buf=new Buffer;
	 xstring handle_x("");
	 int handle_len=handle.length();
	 for(int i=0; i<handle_len; i++)
	    handle_x.appendf("%02X",(unsigned char)handle[i]);
	 LogNote(9,"got file handle %s (%d)",handle_x.get(),handle_len);
	 request_pos=real_pos=pos;
	 if(mode==RETRIEVE)
	    SendRequest(new Request_FSTAT(handle,
	       SSH_FILEXFER_ATTR_SIZE|SSH_FILEXFER_ATTR_MODIFYTIME|SSH_FILEXFER_ATTR_PERMISSIONS,
	       protocol_version),Expect::INFO);
	 else if(mode==STORE && pos>0)
	 {
	    // truncate the file at write position.
	    Request_FSETSTAT *req=new Request_FSETSTAT(handle,protocol_version);
	    req->attrs.size=pos;
	    req->attrs.flags|=SSH_FILEXFER_ATTR_SIZE;
	    SendRequest(req,Expect::IGNORE);
	 }
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case Expect::HANDLE_STALE:
      if(reply->TypeIs(SSH_FXP_HANDLE))
      {
	 // close the handle immediately.
	 const xstring &handle=((Reply_HANDLE*)reply)->GetHandle();
	 SendRequest(new Request_CLOSE(handle),Expect::IGNORE);
      }
      break;
   case Expect::DATA:
      if(max_packets_in_flight_slow_start<max_packets_in_flight)
	 max_packets_in_flight_slow_start++;
      if(reply->TypeIs(SSH_FXP_DATA))
      {
	 const Request_READ *r=e->request.Cast<Request_READ>();
	 Reply_DATA *d=(Reply_DATA*)reply;
	 if(r->pos==pos+file_buf->Size())
	 {
	    const char *b; int s;
	    d->GetData(&b,&s);
	    LogNote(9,"data packet: pos=%lld, size=%d",(long long)r->pos,s);
	    file_buf->Put(b,s);
	    if(d->Eof())
	       goto eof;
	    if(r->len > unsigned(s))   // received less than requested?
	    {
	       // if we have not yet requested next chunk of data,
	       // then adjust request position, else re-request missed data.
	       if(r->pos+r->len==request_pos)
		  request_pos=r->pos+s;
	       else
		  SendRequest(new Request_READ(handle,r->pos+s,r->len-s),Expect::DATA);
	    }
	 }
	 else
	 {
	    if(e->next!=ooo_chain)
	       LogNote(9,"put a packet with id=%d on out-of-order chain (need_pos=%lld packet_pos=%lld)",
		  reply->GetID(),(long long)(pos+file_buf->Size()),(long long)r->pos);
	    e->next=ooo_chain;
	    ooo_chain=e;
	    return;
	 }
      }
      else if(reply->TypeIs(SSH_FXP_NAME))
      {
	 Reply_NAME *r=(Reply_NAME*)reply;
	 LogNote(9,"file name count=%d",r->GetCount());
	 for(int i=0; i<r->GetCount(); i++)
	 {
	    const NameAttrs *a=r->GetNameAttrs(i);
	    if(!file_set)
	       file_set=new FileSet;
	    FileInfo *info=MakeFileInfo(a);
	    if(info)
	       file_set->Add(info);
	    if(mode==LIST)
	    {
	       file_buf->Put(a->name);
	       if(a->attrs.type==SSH_FILEXFER_TYPE_DIRECTORY)
		  file_buf->Put("/");
	       file_buf->Put("\n");
	    }
	    else if(mode==LONG_LIST)
	    {
	       if(a->longname)
	       {
		  file_buf->Put(a->longname);
		  file_buf->Put("\n");
	       }
	       else if(info)
	       {
		  info->MakeLongName();
		  file_buf->Put(info->longname);
		  file_buf->Put("\n");
	       }
	    }
	 }
	 if(r->Eof())
	    goto eof;
      }
      else
      {
	 if(reply->TypeIs(SSH_FXP_STATUS))
	 {
	    if(((Reply_STATUS*)reply)->GetCode()==SSH_FX_EOF)
	    {
	    eof:
	       if(!eof)
		  LogNote(9,"eof");
	       eof=true;
	       state=DONE;
	       if(file_buf && !ooo_chain)
		  file_buf->PutEOF();
	       break;
	    }
	 }
	 SetError(NO_FILE,reply);
      }
      break;
   case Expect::INFO:
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 const FileAttrs *a=((Reply_ATTRS*)reply)->GetAttrs();
	 entity_size=NO_SIZE;
	 entity_date=NO_DATE;
	 if(a->flags&SSH_FILEXFER_ATTR_SIZE)
	    entity_size=a->size;
	 if(a->flags&SSH_FILEXFER_ATTR_MODIFYTIME)
	    entity_date=a->mtime;
	 if(mode==ARRAY_INFO)
	 {
	    array_for_info[e->i].size=entity_size;
	    array_for_info[e->i].get_size=false;
	    array_for_info[e->i].time=entity_date;
	    array_for_info[e->i].get_time=false;
	    break;
	 }
	 LogNote(9,"file info: size=%lld, date=%s",(long long)entity_size,ctime(&entity_date));
	 if(opt_size)
	    *opt_size=entity_size;
	 if(opt_date)
	    *opt_date=entity_date;
      }
      break;
   case Expect::WRITE_STATUS:
      if(reply->TypeIs(SSH_FXP_STATUS))
      {
	 if(((Reply_STATUS*)reply)->GetCode()==SSH_FX_OK)
	    break;
      }
      SetError(NO_FILE,reply);
      break;
   case Expect::DEFAULT:
      if(reply->TypeIs(SSH_FXP_STATUS))
      {
	 if(((Reply_STATUS*)reply)->GetCode()==SSH_FX_OK)
	 {
	    state=DONE;
	    break;
	 }
      }
      SetError(NO_FILE,reply);
      break;
   case Expect::IGNORE:
      break;
   }
   delete e;
}

void SFtp::RequestMoreData()
{
   if(mode==RETRIEVE) {
      int req_len=size_read;
      SendRequest(new Request_READ(handle,request_pos,req_len),Expect::DATA);
      request_pos+=req_len;
   } else if(mode==LIST || mode==LONG_LIST) {
      SendRequest(new Request_READDIR(handle),Expect::DATA);
   }
}

int SFtp::HandleReplies()
{
   int m=HandlePty();
   if(recv_buf==0)
      return m;

   int i=0;
   Expect *ooo_scan=ooo_chain;
   while(ooo_scan)
   {
      Expect *next=ooo_scan->next;
      ooo_chain=next;
      HandleExpect(ooo_scan);
      ooo_scan=next;
      if(++i>64)
      {
	 LogError(0,"Too many out-of-order packets");
	 Disconnect();
	 return MOVED;
      }
   }
   if(!ooo_chain && eof && file_buf && !file_buf->Eof())
      file_buf->PutEOF();

   if(recv_buf->Size()<4)
   {
      if(recv_buf->Error())
      {
	 LogError(0,"receive: %s",recv_buf->ErrorText());
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Eof() && pty_recv_buf->Size()==0)
      {
	 LogError(0,_("Peer closed connection"));
	 Disconnect();
	 m=MOVED;
      }
      return m;
   }

   if(recv_buf->IsSuspended())
      return m;

   Packet *reply=0;
   unpack_status_t st=UnpackPacket(recv_buf.get_non_const(),&reply);
   if(st==UNPACK_NO_DATA_YET)
      return m;
   if(st!=UNPACK_SUCCESS)
   {
      LogError(2,_("invalid server response format"));
      Disconnect();
      return MOVED;
   }

   reply->DropData(recv_buf.get_non_const());
   Expect *e=FindExpectExclusive(reply);
   if(e==0)
   {
      LogError(3,_("extra server response"));
      delete reply;
      return MOVED;
   }
   HandleExpect(e);
   return MOVED;
}
SFtp::Expect **SFtp::FindExpect(Packet *p)
{
   unsigned id=p->GetID();
   for(Expect **scan=&expect_chain; *scan; scan=&scan[0]->next)
   {
      if(scan[0]->request->GetID()==id)
      {
	 assert(!scan[0]->reply);
	 scan[0]->reply=p;
	 return scan;
      }
   }
   return 0;
}
void SFtp::PushExpect(Expect *e)
{
   e->next=*expect_chain_end;
   *expect_chain_end=e;
   expect_chain_end=&e->next;
   expect_queue_size++;
}
void SFtp::DeleteExpect(Expect **e)
{
   if(expect_chain_end==&e[0]->next)
      expect_chain_end=e;
   Expect *d=*e;
   *e=e[0]->next;
   delete d;
   expect_queue_size--;
}
SFtp::Expect *SFtp::FindExpectExclusive(Packet *p)
{
   Expect **e=FindExpect(p);
   if(!e || !*e)
      return 0;
   Expect *res=*e;
   if(expect_chain_end==&res->next)
      expect_chain_end=e;
   *e=res->next;
   expect_queue_size--;
   return res;
}
void SFtp::CloseExpectQueue()
{
   for(Expect  *e=expect_chain; e; e=e->next)
   {
      switch(e->tag)
      {
      case Expect::IGNORE:
      case Expect::HANDLE_STALE:
      case Expect::HOME_PATH:
      case Expect::FXP_VERSION:
	 break;
      case Expect::CWD:
      case Expect::INFO:
      case Expect::DEFAULT:
      case Expect::DATA:
      case Expect::WRITE_STATUS:
	 e->tag=Expect::IGNORE;
	 break;
      case Expect::HANDLE:
	 e->tag=Expect::HANDLE_STALE;
	 break;
      }
   }
}

Glob *SFtp::MakeGlob(const char *pat)
{
   return new GenericGlob(this,pat);
}
ListInfo *SFtp::MakeListInfo(const char *dir)
{
   return new SFtpListInfo(this,dir);
}

int SFtp::Read(void *buf,int size)
{
   if(Error())
      return error_code;
   if(mode==CLOSED)
      return 0;
   if(state==DONE)
      return 0;	  // eof
   if(state==FILE_RECV)
   {
      // keep some packets in flight.
      int limit=(entity_size>=0?max_packets_in_flight:max_packets_in_flight_slow_start);
      if(RespQueueSize()<limit && !file_buf->Eof())
      {
	 // but don't request much after possible EOF.
	 if(entity_size<0 || request_pos<entity_size || RespQueueSize()<2)
	    RequestMoreData();
      }

      const char *buf1;
      int size1;
      file_buf->Get(&buf1,&size1);
      if(buf1==0)
	 return 0;

      int bytes_allowed=rate_limit->BytesAllowedToGet();
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(size1==0)
	 return DO_AGAIN;
      if(size>size1)
	 size=size1;
      memcpy(buf,buf1,size);
      file_buf->Skip(size);
      pos+=size;
      real_pos+=size;
      rate_limit->BytesGot(size);
      TrySuccess();
      return size;
   }
   return DO_AGAIN;
}

int SFtp::Write(const void *buf,int size)
{
   if(mode!=STORE)
      return(0);

   Resume();
   Do();
   if(Error())
      return(error_code);

   if(state!=FILE_SEND || rate_limit==0
   || send_buf->Size()>2*max_buf)
      return DO_AGAIN;

   {
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size+file_buf->Size()>allowed)
	 size=allowed-send_buf->Size();
   }
   if(size+file_buf->Size()>max_buf)
      size=max_buf-file_buf->Size();
   if(entity_size>=0 && pos+size>entity_size)
      size=entity_size-pos;
   if(size<=0)
      return 0;
   file_buf->Put(static_cast<const char*>(buf),size);
   TrySuccess();
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   return(size);
}
int SFtp::Buffered()
{
   if(file_buf==0)
      return 0;
   off_t b=file_buf->Size()+send_buf->Size()*size_write/(size_write+20);
   if(b<0)
      b=0;
   else if(b>real_pos)
      b=real_pos;
   return b;
}
int SFtp::StoreStatus()
{
   if(Error())
      return error_code;
   if(state==FILE_SEND && !eof)
   {
      eof=true;
      return IN_PROGRESS;
   }
   if(state==DONE)
      return OK;
   return IN_PROGRESS;
}

int SFtp::Done()
{
   if(mode==CLOSED)
      return OK;
   if(Error())
      return error_code;
   if(eof || state==DONE)
      return OK;
   if(mode==CONNECT_VERIFY)
      return OK;
   return IN_PROGRESS;
}

void SFtp::SuspendInternal()
{
   if(recv_buf)
      recv_buf->SuspendSlave();
   if(send_buf)
      send_buf->SuspendSlave();
   if(pty_send_buf)
      pty_send_buf->SuspendSlave();
   if(pty_recv_buf)
      pty_recv_buf->SuspendSlave();
}
void SFtp::ResumeInternal()
{
   if(recv_buf)
      recv_buf->ResumeSlave();
   if(send_buf)
      send_buf->ResumeSlave();
   if(pty_send_buf)
      pty_send_buf->ResumeSlave();
   if(pty_recv_buf)
      pty_recv_buf->ResumeSlave();
}

const char *SFtp::CurrentStatus()
{
   switch(state)
   {
   case DISCONNECTED:
      if(!ReconnectAllowed())
	 return DelayingMessage();
      return _("Not connected");
   case CONNECTING:
      if(ssh && ssh->status)
	 return ssh->status;
   case CONNECTING_1:
   case CONNECTING_2:
      return _("Connecting...");
   case CONNECTED:
      return _("Connected");
   case WAITING:
      return _("Waiting for response...");
   case FILE_RECV:
      return _("Receiving data");
   case FILE_SEND:
      return _("Sending data");
   case DONE:
      return _("Done");
   }
   return "";
}

bool SFtp::SameSiteAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   SFtp *o=(SFtp*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool SFtp::SameLocationAs(const FileAccess *fa) const
{
   if(!SameSiteAs(fa))
      return false;
   SFtp *o=(SFtp*)fa;
   if(xstrcmp(cwd,o->cwd))
      return false;
   if(xstrcmp(home,o->home))
      return false;
   return true;
}

void SFtp::Cleanup()
{
   if(hostname==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}
void SFtp::CleanupThis()
{
   if(mode!=CLOSED)
      return;
   Disconnect();
}

void SFtp::Reconfig(const char *name)
{
   super::Reconfig(name);
   const char *c=hostname;
   max_packets_in_flight=Query("max-packets-in-flight",c);
   if(max_packets_in_flight<1)
      max_packets_in_flight=1;
   if(max_packets_in_flight_slow_start>max_packets_in_flight)
      max_packets_in_flight_slow_start=max_packets_in_flight;
   size_read=Query("size-read",c);
   size_write=Query("size-write",c);
   if(size_read<16)
      size_read=16;
   if(size_write<16)
      size_write=16;
   if(!xstrcmp(name,"sftp:charset") && protocol_version && protocol_version<4)
   {
      if(!IsSuspended())
	 cache->TreeChanged(this,"/");
      const char *charset=ResMgr::Query("fish:charset",hostname);
      if(charset && *charset)
      {
	 if(!send_translate)
	    send_translate=new DirectedBuffer(DirectedBuffer::PUT);
	 if(!recv_translate)
	    recv_translate=new DirectedBuffer(DirectedBuffer::GET);
	 send_translate->SetTranslation(charset,false);
	 recv_translate->SetTranslation(charset,true);
      }
   }
}

void SFtp::ClassInit()
{
   // register the class
   Register("sftp",SFtp::New);
}
FileAccess *SFtp::New() { return new SFtp(); }

DirList *SFtp::MakeDirList(ArgV *args)
{
  return new SFtpDirList(this,args);
}

struct code_text { int code; const char *text; };

const char *SFtp::Packet::GetPacketTypeText()
{
   struct code_text text_table[]={
      { SSH_FXP_INIT,          "INIT"		},
      { SSH_FXP_VERSION,       "VERSION"	},
      { SSH_FXP_OPEN,          "OPEN"		},
      { SSH_FXP_CLOSE,         "CLOSE"		},
      { SSH_FXP_READ,          "READ"		},
      { SSH_FXP_WRITE,         "WRITE"		},
      { SSH_FXP_LSTAT,         "LSTAT"		},
      { SSH_FXP_FSTAT,         "FSTAT"		},
      { SSH_FXP_SETSTAT,       "SETSTAT"	},
      { SSH_FXP_FSETSTAT,      "FSETSTAT"	},
      { SSH_FXP_OPENDIR,       "OPENDIR"	},
      { SSH_FXP_READDIR,       "READDIR"	},
      { SSH_FXP_REMOVE,        "REMOVE"		},
      { SSH_FXP_MKDIR,         "MKDIR"		},
      { SSH_FXP_RMDIR,         "RMDIR"		},
      { SSH_FXP_REALPATH,      "REALPATH"	},
      { SSH_FXP_STAT,          "STAT"		},
      { SSH_FXP_RENAME,        "RENAME"		},
      { SSH_FXP_READLINK,      "READLINK"	},
      { SSH_FXP_SYMLINK,       "SYMLINK"	},
      { SSH_FXP_LINK,          "LINK"		},
      { SSH_FXP_BLOCK,         "BLOCK"		},
      { SSH_FXP_UNBLOCK,       "UNBLOCK"	},
      { SSH_FXP_STATUS,        "STATUS"		},
      { SSH_FXP_HANDLE,        "HANDLE"		},
      { SSH_FXP_DATA,          "DATA"		},
      { SSH_FXP_NAME,          "NAME"		},
      { SSH_FXP_ATTRS,         "ATTRS"		},
      { SSH_FXP_EXTENDED,      "EXTENDED"	},
      { SSH_FXP_EXTENDED_REPLY,"EXTENDED_REPLY"	},
      {0,0}
   };
   for(int i=0; text_table[i].text; i++)
      if(text_table[i].code==type)
	 return text_table[i].text;
   return "UNKNOWN";
}

const char *SFtp::Reply_STATUS::GetCodeText()
{
   static const char *text_table[]={
      "OK",
      "EOF",
      "No such file",
      "Permission denied",
      "Failure",
      "Bad message",
      "No connection",
      "Connection lost",
      "Operation not supported",
      "Invalid handle",
      "File already exists",
      "Write protect",
      "No media",
      "No space on filesystem",
      "Quota exceeded",
      "Unknown principal",
      "Lock conflict",
      "Directory not empty",
      "Not a directory",
      "Invalid file name",
      "Link loop",
      "Cannot delete",
      "Invalid parameter",
      "File is a directory",
      "Byte range lock conflict",
      "Byte range lock refused",
      "Delete pending",
      "File corrupt",
      "Owner invalid",
      "Group invalid"
   };
   if(code>=0 && code<sizeof(text_table)/sizeof(*text_table))
      return text_table[code];
   return 0;
}

void SFtp::SetError(int code,const Packet *reply)
{
   if(!reply->TypeIs(SSH_FXP_STATUS))
   {
      SetError(code);
      return;
   }
   Reply_STATUS *status=(Reply_STATUS*)reply;
   const char *message=status->GetMessage();
   if(message)
   {
      SetError(code,utf8_to_lc(message));
      return;
   }
   message=status->GetCodeText();
   if(message)
   {
      SetError(code,_(message));
      return;
   }
   SetError(code);
}


#define UNPACK_GENERIC(out,size,fun)   \
   do {				       \
      if(limit-*offset<(size))	       \
	 return UNPACK_WRONG_FORMAT;   \
      out=b->fun(*offset);	       \
      *offset+=(size);		       \
   } while(0)
#define UNPACK8(out)	UNPACK_GENERIC(out,1,UnpackUINT8)
#define UNPACK32(out)	UNPACK_GENERIC(out,4,UnpackUINT32BE)
#define UNPACK64(out)	UNPACK_GENERIC(out,8,UnpackUINT64BE)
#define UNPACK32_SIGNED(out)	UNPACK_GENERIC(out,4,UnpackINT32BE)
#define UNPACK64_SIGNED(out)	UNPACK_GENERIC(out,8,UnpackINT64BE)
#define PACK8(data)	b->PackUINT8(data)
#define PACK32(data)	b->PackUINT32BE(data)
#define PACK64(data)	b->PackUINT64BE(data)
#define PACK32_SIGNED(data)	b->PackINT32BE(data)
#define PACK64_SIGNED(data)	b->PackINT64BE(data)

SFtp::unpack_status_t SFtp::PacketSTRING::Unpack(const Buffer *b)
{
   unpack_status_t res;
   res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   res=UnpackString(b,&unpacked,length+4,&string);
   return res;
}

SFtp::unpack_status_t SFtp::Reply_NAME::Unpack(const Buffer *b)
{
   unpack_status_t res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   int *offset=&unpacked;
   int limit=length+4;
   UNPACK32(count);
   names=new NameAttrs[count];
   for(int i=0; i<count; i++)
   {
      res=names[i].Unpack(b,offset,limit,protocol_version);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   if(*offset<limit)
      UNPACK8(eof);
   return UNPACK_SUCCESS;
}
SFtp::unpack_status_t SFtp::Reply_DATA::Unpack(const Buffer *b)
{
   unpack_status_t res=PacketSTRING::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   int *offset=&unpacked;
   int limit=length+4;
   if(*offset<limit)
      UNPACK8(eof);
   return UNPACK_SUCCESS;
}
SFtp::unpack_status_t SFtp::NameAttrs::Unpack(const Buffer *b,int *offset,int limit,int protocol_version)
{
   unpack_status_t res;

   res=Packet::UnpackString(b,offset,limit,&name);
   if(res!=UNPACK_SUCCESS)
      return res;
   if(protocol_version<=3)
   {
      res=Packet::UnpackString(b,offset,limit,&longname);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   res=attrs.Unpack(b,offset,limit,protocol_version);
   if(res!=UNPACK_SUCCESS)
      return res;

   return UNPACK_SUCCESS;
}

SFtp::unpack_status_t SFtp::FileAttrs::Unpack(const Buffer *b,int *offset,int limit,int protocol_version)
{
   unpack_status_t res;

   UNPACK32(flags);
   if(protocol_version>=4)
      UNPACK8(type);
   if(flags & SSH_FILEXFER_ATTR_SIZE)
      UNPACK64(size);
   if(protocol_version<=3 && (flags & SSH_FILEXFER_ATTR_UIDGID))
   {
      UNPACK32(uid);
      UNPACK32(gid);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_OWNERGROUP))
   {
      res=Packet::UnpackString(b,offset,limit,&owner);
      if(res!=UNPACK_SUCCESS)
	 return res;
      res=Packet::UnpackString(b,offset,limit,&group);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   if(flags & SSH_FILEXFER_ATTR_PERMISSIONS)
   {
      UNPACK32(permissions);
      if(protocol_version<=3)
      {
	 switch(permissions&S_IFMT)
	 {
	 case S_IFREG: type=SSH_FILEXFER_TYPE_REGULAR;	 break;
	 case S_IFDIR: type=SSH_FILEXFER_TYPE_DIRECTORY; break;
	 case S_IFLNK: type=SSH_FILEXFER_TYPE_SYMLINK;	 break;
	 case S_IFIFO:
	 case S_IFCHR:
	 case S_IFBLK: type=SSH_FILEXFER_TYPE_SPECIAL;	 break;
	 default:      type=SSH_FILEXFER_TYPE_UNKNOWN;	 break;
	 }
      }
   }
   if(protocol_version<=3 && (flags & SSH_FILEXFER_ATTR_ACMODTIME))
   {
      UNPACK32_SIGNED(atime);
      UNPACK32_SIGNED(mtime);
      flags|=SSH_FILEXFER_ATTR_MODIFYTIME;
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_ACCESSTIME))
   {
      UNPACK64_SIGNED(atime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 UNPACK32(atime_nseconds);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_CREATETIME))
   {
      UNPACK64_SIGNED(createtime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 UNPACK32(createtime_nseconds);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_MODIFYTIME))
   {
      UNPACK64_SIGNED(mtime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 UNPACK32(mtime_nseconds);
   }
   if(protocol_version>=5 && (flags & SSH_FILEXFER_ATTR_CTIME))
   {
      UNPACK64_SIGNED(ctime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 UNPACK32(ctime_nseconds);
   }
   if(atime_nseconds>999999999 || createtime_nseconds>999999999 || mtime_nseconds>999999999 || ctime_nseconds>999999999)
      return UNPACK_WRONG_FORMAT;
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_ACL))
   {
      UNPACK32(ace_count);
      ace=new FileACE[ace_count];
      for(unsigned i=0; i<ace_count; i++)
      {
	 res=ace[i].Unpack(b,offset,limit);
	 if(res!=UNPACK_SUCCESS)
	    return res;
      }
   }
   if(protocol_version>=5 && (flags & SSH_FILEXFER_ATTR_BITS))
   {
      UNPACK32(attrib_bits);
      if(protocol_version>=6)
	 UNPACK32(attrib_bits_valid);
   }
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_TEXT_HINT))
      UNPACK8(text_hint);
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_MIME_TYPE))
   {
      res=Packet::UnpackString(b,offset,limit,&mime_type);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_LINK_COUNT))
      UNPACK32(link_count);
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_UNTRANSLATED_NAME))
   {
      res=Packet::UnpackString(b,offset,limit,&untranslated_name);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   if(flags & SSH_FILEXFER_ATTR_EXTENDED)
   {
      UNPACK32(extended_count);
      extended_attrs=new ExtFileAttr[extended_count];
      for(unsigned i=0; i<extended_count; i++)
      {
	 res=extended_attrs[i].Unpack(b,offset,limit);
	 if(res!=UNPACK_SUCCESS)
	    return res;
      }
   }
   return UNPACK_SUCCESS;
}
void SFtp::FileAttrs::Pack(Buffer *b,int protocol_version)
{
   if(protocol_version<=3 && (flags & SSH_FILEXFER_ATTR_MODIFYTIME)
   && !(flags & SSH_FILEXFER_ATTR_ACCESSTIME))
   {
      flags|=SSH_FILEXFER_ATTR_ACMODTIME;
      atime=mtime;
   }

   unsigned flags_mask=SSH_FILEXFER_ATTR_MASK_V3;
   if(protocol_version==4) flags_mask=SSH_FILEXFER_ATTR_MASK_V4;
   if(protocol_version==5) flags_mask=SSH_FILEXFER_ATTR_MASK_V5;
   if(protocol_version>=6) flags_mask=SSH_FILEXFER_ATTR_MASK_V6;
   PACK32(flags&flags_mask);

   if(protocol_version>=4)
   {
      if(type==0)
      {
	 switch(permissions&S_IFMT)
	 {
	 case S_IFREG: type=SSH_FILEXFER_TYPE_REGULAR;	 break;
	 case S_IFDIR: type=SSH_FILEXFER_TYPE_DIRECTORY; break;
	 case S_IFLNK: type=SSH_FILEXFER_TYPE_SYMLINK;	 break;
	 case S_IFIFO:
	 case S_IFCHR:
	 case S_IFBLK: type=SSH_FILEXFER_TYPE_SPECIAL;	 break;
	 default:      type=SSH_FILEXFER_TYPE_UNKNOWN;	 break;
	 }
      }
      PACK8(type);
   }
   if(flags & SSH_FILEXFER_ATTR_SIZE)
      PACK64(size);
   if(protocol_version<=3 && (flags & SSH_FILEXFER_ATTR_UIDGID))
   {
      PACK32(uid);
      PACK32(gid);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_OWNERGROUP))
   {
      Packet::PackString(b,owner);
      Packet::PackString(b,group);
   }
   if(flags & SSH_FILEXFER_ATTR_PERMISSIONS)
      PACK32(permissions);
   if(protocol_version<=3 && (flags & SSH_FILEXFER_ATTR_ACMODTIME))
   {
      PACK32_SIGNED(atime);
      PACK32_SIGNED(mtime);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_ACCESSTIME))
   {
      PACK64_SIGNED(atime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 PACK32(atime_nseconds);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_CREATETIME))
   {
      PACK64_SIGNED(createtime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 PACK32(createtime_nseconds);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_MODIFYTIME))
   {
      PACK64_SIGNED(mtime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 PACK32(mtime_nseconds);
   }
   if(protocol_version>=5 && (flags & SSH_FILEXFER_ATTR_CTIME))
   {
      PACK64_SIGNED(ctime);
      if(flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES)
	 PACK32(ctime_nseconds);
   }
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_ACL))
   {
      PACK32(ace_count);
      for(unsigned i=0; i<ace_count; i++)
	 ace[i].Pack(b);
   }
   if(protocol_version>=5 && (flags & SSH_FILEXFER_ATTR_BITS))
   {
      PACK32(attrib_bits);
      if(protocol_version>=6)
	 PACK32(attrib_bits_valid);
   }
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_TEXT_HINT))
      PACK8(text_hint);
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_MIME_TYPE))
      Packet::PackString(b,mime_type);
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_LINK_COUNT))
      PACK32(link_count);
   if(protocol_version>=6 && (flags & SSH_FILEXFER_ATTR_UNTRANSLATED_NAME))
      Packet::PackString(b,untranslated_name);
   if(flags & SSH_FILEXFER_ATTR_EXTENDED)
   {
      PACK32(extended_count);
      for(unsigned i=0; i<extended_count; i++)
	 extended_attrs[i].Pack(b);
   }
}
int SFtp::FileAttrs::ComputeLength(int protocol_version)
{
   Buffer b;
   Pack(&b,protocol_version);
   return b.Size();
}
SFtp::FileAttrs::~FileAttrs()
{
   delete[] extended_attrs;
   delete[] ace;
}

SFtp::unpack_status_t SFtp::FileAttrs::FileACE::Unpack(const Buffer *b,int *offset,int limit)
{
   UNPACK32(ace_type);
   UNPACK32(ace_flag);
   UNPACK32(ace_mask);
   return Packet::UnpackString(b,offset,limit,&who);
}
void SFtp::FileAttrs::FileACE::Pack(Buffer *b)
{
   PACK32(ace_type);
   PACK32(ace_flag);
   PACK32(ace_mask);
   Packet::PackString(b,who);
}

SFtp::unpack_status_t SFtp::FileAttrs::ExtFileAttr::Unpack(const Buffer *b,int *offset,int limit)
{
   unpack_status_t res;
   res=Packet::UnpackString(b,offset,limit,&extended_type);
   if(res!=UNPACK_SUCCESS)
      return res;
   res=Packet::UnpackString(b,offset,limit,&extended_data);
   if(res!=UNPACK_SUCCESS)
      return res;
   return UNPACK_SUCCESS;
}
void SFtp::FileAttrs::ExtFileAttr::Pack(Buffer *b)
{
   Packet::PackString(b,extended_type);
   Packet::PackString(b,extended_data);
}

SFtp::unpack_status_t SFtp::Reply_ATTRS::Unpack(const Buffer *b)
{
   unpack_status_t res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   return attrs.Unpack(b,&unpacked,length+4,protocol_version);
}

SFtp::unpack_status_t SFtp::Reply_STATUS::Unpack(const Buffer *b)
{
   unpack_status_t res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   int *offset=&unpacked;
   int limit=length+4;
   UNPACK32(code);
   if(protocol_version>=3)
   {
      res=Packet::UnpackString(b,offset,limit,&message);
      if(res!=UNPACK_SUCCESS)
	 return res;
      res=Packet::UnpackString(b,offset,limit,&language);
      if(res!=UNPACK_SUCCESS)
	 return res;
   }
   return UNPACK_SUCCESS;
}
void SFtp::Request_READ::Pack(Buffer *b)
{
   PacketSTRING::Pack(b);
   PACK64(pos);
   PACK32(len);
}
void SFtp::Request_WRITE::Pack(Buffer *b)
{
   PacketSTRING::Pack(b);
   PACK64(pos);
   int len=data.length();
   PACK32(len);
   b->Put(data,len);
}
void SFtp::Request_OPEN::Pack(Buffer *b)
{
   PacketSTRING::Pack(b);
   if(protocol_version<=4)
      PACK32(pflags);
   if(protocol_version>=5)
   {
      PACK32(desired_access);
      PACK32(flags);
   }
   attrs.Pack(b,protocol_version);
}
void SFtp::Request_RENAME::Pack(Buffer *b)
{
   Packet::Pack(b);
   Packet::PackString(b,oldpath);
   Packet::PackString(b,newpath);
   if(protocol_version>=5)
      PACK32(flags);
}

const char *SFtp::utf8_to_lc(const char *s)
{
   if(!recv_translate)
      return s;

   recv_translate->ResetTranslation();
   recv_translate->PutTranslated(s);
   recv_translate->Buffer::Put("",1);
   int len;
   recv_translate->Get(&s,&len);
   recv_translate->Skip(len);
   return s;
}
const char *SFtp::lc_to_utf8(const char *s)
{
   if(!send_translate)
      return s;

   send_translate->ResetTranslation();
   send_translate->PutTranslated(s);
   send_translate->Buffer::Put("",1);
   int len;
   send_translate->Get(&s,&len);
   send_translate->Skip(len);
   return s;
}

FileInfo *SFtp::MakeFileInfo(const NameAttrs *na)
{
   const FileAttrs *a=&na->attrs;
   const char *name=utf8_to_lc(na->name);
   if(!name || !name[0])
      return 0;
   if(strchr(name,'/'))
      return 0;
   if(name[0]=='~')
      name=dir_file(".",name);
   Ref<FileInfo> fi(new FileInfo(name));
   switch(a->type)
   {
   case SSH_FILEXFER_TYPE_REGULAR:  fi->SetType(fi->NORMAL);    break;
   case SSH_FILEXFER_TYPE_DIRECTORY:fi->SetType(fi->DIRECTORY); break;
   case SSH_FILEXFER_TYPE_SYMLINK:  fi->SetType(fi->SYMLINK);   break;
   default: return 0;
   }
   if(na->longname)
      fi->SetLongName(utf8_to_lc(na->longname));
   if(a->flags&SSH_FILEXFER_ATTR_SIZE)
      fi->SetSize(a->size);
   if(a->flags&SSH_FILEXFER_ATTR_UIDGID)
   {
      char id[12];
      sprintf(id,"%u",(unsigned)a->uid);
      fi->SetUser(id);
      sprintf(id,"%u",(unsigned)a->gid);
      fi->SetGroup(id);
   }
   if(a->flags&SSH_FILEXFER_ATTR_OWNERGROUP)
   {
      fi->SetUser (utf8_to_lc(a->owner));
      fi->SetGroup(utf8_to_lc(a->group));
   }
   else if(fi->longname)
   {
      // try to extract owner/group from long name.
      Ref<FileInfo> ls(FileInfo::parse_ls_line(fi->longname,0));
      if(ls)
      {
	 if(ls->user)
	    fi->SetUser(ls->user);
	 if(ls->group)
	    fi->SetGroup(ls->group);
	 if(ls->nlinks>0)
	    fi->SetNlink(ls->nlinks);
      }
   }
   if(a->flags&SSH_FILEXFER_ATTR_PERMISSIONS)
      fi->SetMode(a->permissions&07777);
   if(a->flags&SSH_FILEXFER_ATTR_MODIFYTIME)
      fi->SetDate(a->mtime,0);
   return fi.borrow();
}


#undef super
#define super DirList
#include "ArgV.h"

int SFtpDirList::Do()
{
   int m=STALL;

   if(done)
      return m;

   if(buf->Eof())
   {
      done=true;
      return MOVED;
   }

   if(!ubuf)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      int err;
      const FileSet *fset_c;
      if(use_cache && FileAccess::cache->Find(session,dir,FA::LONG_LIST,&err,
				    &cache_buffer,&cache_buffer_size,&fset_c))
      {
	 if(err)
	 {
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 fset=new FileSet(fset_c);
      }
      else
      {
	 session->Open(dir,FA::LONG_LIST);
	 ubuf=new IOBufferFileAccess(session);
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      if(!fset && session->IsOpen())
	 fset=session.Cast<SFtp>()->GetFileSet();
      FileAccess::cache->Add(session,dir,FA::LONG_LIST,FA::OK,ubuf,fset);
      if(use_file_set)
      {
	 fset->Sort(fset->BYNAME,false);
	 for(fset->rewind(); fset->curr(); fset->next())
	 {
	    FileInfo *fi=fset->curr();
	    buf->Put(fi->GetLongName());
	    buf->Put("\n");
	 }
	 fset=0;
      }
      ubuf=0;
      dir=args->getnext();
      if(!dir)
	 buf->PutEOF();
      else
	 buf->Format("\n%s:\n",dir);
      return MOVED;
   }

   if(len>0)
   {
      if(!use_file_set)
	 buf->Put(b,len);
      ubuf->Skip(len);
      m=MOVED;
   }

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

SFtpDirList::SFtpDirList(SFtp *s,ArgV *a)
   : DirList(s,a)
{
   use_file_set=true;
   args->rewind();
   int opt;
   while((opt=args->getopt("fCFl"))!=EOF)
   {
      switch(opt)
      {
      case('a'):
	 ls_options.show_all=true;
	 break;
      case('C'):
	 ls_options.multi_column=true;
	 break;
      case('F'):
	 ls_options.append_type=true;
	 break;
      }
   }
   while(args->getindex()>1)
      args->delarg(1);	// remove options.
   if(args->count()<2)
      args->Append("");
   args->rewind();
   dir=args->getnext();
   if(args->getindex()+1<args->count())
      buf->Format("%s:\n",dir);
}

const char *SFtpDirList::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
      return xstring::format(_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
   return "";
}

void SFtpDirList::SuspendInternal()
{
   if(ubuf)
      ubuf->SuspendSlave();
}
void SFtpDirList::ResumeInternal()
{
   if(ubuf)
      ubuf->ResumeSlave();
}


#undef super
#define super ListInfo
int SFtpListInfo::Do()
{
   int m=STALL;
   if(done)
      return m;
   if(!ubuf)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      int err;
      const FileSet *fset_c;
      if(use_cache && FileAccess::cache->Find(session,"",FA::LONG_LIST,&err,
				    &cache_buffer,&cache_buffer_size,&fset_c))
      {
	 if(err)
	 {
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 result=new FileSet(fset_c);
      }
      else
      {
	 session->Open("",FA::LONG_LIST);
	 ubuf=new IOBufferFileAccess(session);
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
      }
   }
   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      if(!result && session->IsOpen())
	 result=session.Cast<SFtp>()->GetFileSet();
      FileAccess::cache->Add(session,"",FA::LONG_LIST,FA::OK,ubuf,result);
      result->ExcludeDots();
      result->Exclude(exclude_prefix,exclude);
      done=true;
      m=MOVED;
   }
   if(len>0)
   {
      ubuf->Skip(len);
      m=MOVED;
   }
   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}
const char *SFtpListInfo::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
      return xstring::format(_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
   return "";
}

#include "modconfig.h"
#ifdef MODULE_PROTO_SFTP
void module_init()
{
   SFtp::ClassInit();
}
#endif
