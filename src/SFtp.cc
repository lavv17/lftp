/*
 * lftp - file transfer program
 *
 * Copyright (c) 2003 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <iconv.h>

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
	 if(!connection_takeover || (o->priority>=priority && !o->suspended))
	    continue;
	 o->Disconnect();
	 return need_sleep;
      }

      if(home && !o->home)
	 o->home=xstrdup(home);
      else if(!home && o->home)
	 home=xstrdup(o->home);

      o->ExpandTildeInCWD();
      ExpandTildeInCWD();

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
   if(mode==CLOSED && send_buf && idle>0)
   {
      if(now-idle_start>=idle)
      {
	 DebugPrint("---- ",_("Closing idle connection"),1);
	 Disconnect();
	 return m;
      }
      TimeoutS(idle_start+idle-now);
   }

   if(Error())
      return m;

   if(!hostname)
      return m;

   if(send_buf && send_buf->Error())
   {
      Disconnect();
      return MOVED;
   }
   m|=HandleReplies();

   if(Error())
      return m;

   if(send_buf)
      BumpEventTime(send_buf->EventTime());
   if(recv_buf)
      BumpEventTime(recv_buf->EventTime());
   if(pty_send_buf)
      BumpEventTime(pty_send_buf->EventTime());
   if(pty_recv_buf)
      BumpEventTime(pty_recv_buf->EventTime());

   // check for timeout only if there should be connection activity.
   if(state!=DISCONNECTED && mode!=CLOSED && CheckTimeout())
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
	 prog="ssh -ax";
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
      {
	 const char *greeting="echo SFTP: >&2";
	 char *init1=string_alloca(strlen(init)+strlen(greeting)+2);
	 sprintf(init1,"%s;%s",greeting,init);
	 init=init1;
      }
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
      char *cmd_str=cmd->Combine(0);
      Log::global->Format(9,"---- %s (%s)\n",_("Running connect program"),cmd_str);
      xfree(cmd_str);
      ssh=new PtyShell(cmd);
      ssh->UsePipes();
      state=CONNECTING;
      event_time=now;
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
      pty_send_buf=new IOBufferFDStream(ssh,IOBuffer::PUT);
      ssh=0;
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
      if(home==0 && !RespQueueIsEmpty())
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
      else
      {
	 recv_buf->Resume();
	 if(recv_buf->Size()>0 || (recv_buf->Size()==0 && recv_buf->Eof()))
	    m=MOVED;
      }
      break;
   case FILE_SEND:
      // pack data from file_buf.
      file_buf->Get(&b,&s);
      if(s<size_write && !eof)
	 return m;   // wait for more data before sending.
      if(s==0)
      {
	 // no more data, set attributes and close the file.
	 Request_FSETSTAT *req=new Request_FSETSTAT(handle,handle_len,protocol_version);
	 req->attrs.mtime=entity_date;
	 req->attrs.flags|=SSH_FILEXFER_ATTR_MODIFYTIME;
	 SendRequest(req,Expect::IGNORE);
	 CloseHandle(Expect::DEFAULT);
	 state=WAITING;
	 m=MOVED;
	 break;
      }
      if(RespQueueSize()>max_packets_in_flight)
	 return m;
      SendRequest(new Request_WRITE(handle,handle_len,request_pos,b,s),Expect::WRITE_STATUS);
      file_buf->Skip(s);
      request_pos+=s;
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
   recv_translate=o->recv_translate; o->recv_translate=0;
   send_translate=o->send_translate; o->send_translate=0;
   send_buf=o->send_buf; o->send_buf=0;
   recv_buf=o->recv_buf; o->recv_buf=0;
   pty_send_buf=o->pty_send_buf; o->pty_send_buf=0;
   pty_recv_buf=o->pty_recv_buf; o->pty_recv_buf=0;
   rate_limit=o->rate_limit; o->rate_limit=0;
   expect_queue_size=o->expect_queue_size; o->expect_queue_size=0;
   expect_chain=o->expect_chain; o->expect_chain=0;
   expect_chain_end=o->expect_chain_end;
   if(expect_chain_end==&o->expect_chain)
      expect_chain_end=&expect_chain;
   o->expect_chain_end=&o->expect_chain;
   event_time=o->event_time;
   ssh_id=o->ssh_id;
   state=CONNECTED;
   o->Disconnect();
   if(!home)
      set_home(home_auto);
}

void SFtp::Disconnect()
{
   if(send_buf)
      DebugPrint("---- ",_("Disconnecting"),9);
   xfree(handle); handle=0; handle_len=0;
   Delete(send_buf); send_buf=0;
   Delete(recv_buf); recv_buf=0;
   Delete(pty_send_buf); pty_send_buf=0;
   Delete(pty_recv_buf); pty_recv_buf=0;
   delete file_buf; file_buf=0;
   delete ssh; ssh=0;
   EmptyRespQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED);
   received_greeting=false;
   protocol_version=0;
   delete send_translate; send_translate=0;
   delete recv_translate; recv_translate=0;
   ssh_id=0;
   xfree(home_auto); home_auto=0;
   home_auto=xstrdup(FindHomeAuto());
}

void SFtp::Init()
{
   state=DISCONNECTED;
   send_buf=0;
   recv_buf=0;
   recv_buf_suspended=false;
   pty_send_buf=0;
   pty_recv_buf=0;
   file_buf=0;
   file_set=0;
   ssh=0;
   ssh_id=0;
   eof=false;
   received_greeting=false;
   expect_queue_size=0;
   expect_chain=0;
   expect_chain_end=&expect_chain;
   ooo_chain=0;
   protocol_version=0;
   send_translate=0;
   recv_translate=0;
   handle=0;
   handle_len=0;
   max_packets_in_flight=3;
   size_read=0x8000;
   size_write=0x8000;
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
SFtp::unpack_status_t SFtp::Packet::UnpackString(Buffer *b,int *offset,int limit,char **str_out,int *len_out)
{
   assert(str_out && *str_out==0);

   if(limit-*offset<4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;

   int len=b->UnpackUINT32BE(*offset);
   if(len>limit-*offset-4)
   {
      Log::global->Write(2,"**** bad string in reply (invalid length field)");
      return UNPACK_WRONG_FORMAT;
   }
   *offset+=4;

   const char *data;
   int data_len;
   b->Get(&data,&data_len);

   char *string=(char*)xmalloc(len+1);
   memcpy(string,data+*offset,len);
   string[len]=0;

   *offset+=len;
   *str_out=string;
   if(len_out)
      *len_out=len;

   return UNPACK_SUCCESS;
}

SFtp::unpack_status_t SFtp::Packet::Unpack(Buffer *b)
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
   case SSH_FXP_EXTENDED:
      DebugPrint("**** ","request in reply??",0);
      return UNPACK_WRONG_FORMAT;
   case SSH_FXP_EXTENDED_REPLY:
      DebugPrint("**** ","unexpected SSH_FXP_EXTENDED_REPLY",0);
      return UNPACK_WRONG_FORMAT;
   }
   res=pp->Unpack(b);
   if(res!=UNPACK_SUCCESS)
   {
      switch(res)
      {
      case UNPACK_PREMATURE_EOF:
	 DebugPrint("**** ","premature eof",0);
	 break;
      case UNPACK_WRONG_FORMAT:
	 DebugPrint("**** ","wrong packet format",0);
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
   request->Pack(send_buf);
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
   int home_len=strlen(home);
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
   return lc_to_utf8(SkipHome(dir_file(cwd,path)));
}

void SFtp::SendRequest()
{
   ExpandTildeInCWD();
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      SendRequest(new Request_STAT(lc_to_utf8(file),0,protocol_version),Expect::CWD);
      SendRequest(new Request_STAT(lc_to_utf8(dir_file(file,".")),0,protocol_version),Expect::CWD);
      state=WAITING;
      break;
   case RETRIEVE:
      SendRequest(new Request_OPEN(WirePath(file),
			SSH_FXF_READ,protocol_version),Expect::HANDLE);
      state=WAITING;
      break;
   case LIST:
   case LONG_LIST:
      SendRequest(new Request_OPENDIR(WirePath(file)),Expect::HANDLE);
      state=WAITING;
      break;
   case STORE:
      SendRequest(new Request_OPEN(WirePath(file),
			SSH_FXF_WRITE|SSH_FXF_CREAT,protocol_version),Expect::HANDLE);
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
      SendRequest(new Request_RENAME(WirePath(file),
				     file1_wire_path),Expect::DEFAULT);
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
      SendRequest(new Request_CLOSE(handle,handle_len),c);
      xfree(handle); handle=0; handle_len=0;
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
   delete file_buf; file_buf=0;
   delete file_set; file_set=0;
   CloseHandle(Expect::IGNORE);
   super::Close();
   // don't need these out-of-order packets anymore
   while(ooo_chain)
      DeleteExpect(&ooo_chain);
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
      const char *p="password: ";
      const char *y="(yes/no)? ";
      int p_len=strlen(p);
      int y_len=strlen(y);
      if(s>=p_len && !strncasecmp(b+s-p_len,p,p_len))
      {
	 if(!pass)
	 {
	    SetError(LOGIN_FAILED,"Password required");
	    return MOVED;
	 }
	 pty_recv_buf->Put("XXXX");
	 pty_send_buf->Put(pass);
	 pty_send_buf->Put("\n");
	 return m;
      }
      if(s>=y_len && !strncasecmp(b+s-y_len,y,y_len))
      {
	 pty_recv_buf->Put("yes\n");
	 pty_send_buf->Put("yes\n");
	 return m;
      }
      if(pty_recv_buf->Eof())
	 DebugPrint("**** ",_("Peer closed connection"),0);
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

   DebugPrint("<--- ",line,4);
   if(!received_greeting && !strcmp(line,"SFTP:"))
      received_greeting=true;

   return m;
}

void SFtp::HandleExpect(Expect *e)
{
   Packet *reply=e->reply;
   switch(e->tag)
   {
   case Expect::FXP_VERSION:
      if(reply->TypeIs(SSH_FXP_VERSION))
      {
	 protocol_version=((Reply_VERSION*)reply)->GetVersion();
	 Log::global->Format(9,"---- protocol version set to %d\n",protocol_version);
	 if(protocol_version>=4)
	 {
	    send_translate=new DirectedBuffer(DirectedBuffer::PUT);
	    recv_translate=new DirectedBuffer(DirectedBuffer::GET);
	    send_translate->SetTranslation("UTF-8",false);
	    recv_translate->SetTranslation("UTF-8",true);
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
	    home_auto=xstrdup(utf8_to_lc(a->name));
	    Log::global->Format(9,"---- home set to %s\n",home_auto);
	    PropagateHomeAuto();
	    if(!home)
	       set_home(home_auto);
	    LsCache::SetDirectory(this, home, true);
	 }
      }
      break;
   case Expect::CWD:
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 const FileAttrs *a=((Reply_ATTRS*)reply)->GetAttrs();
	 if(a->type!=SSH_FILEXFER_TYPE_DIRECTORY)
	 {
	    LsCache::SetDirectory(this,cwd,false);
	    SetError(NO_FILE,strerror(ENOTDIR));
	    break;
	 }
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    xfree(cwd);
	    cwd=xstrdup(file);
	    eof=true;
	    LsCache::SetDirectory(this,cwd,true);
	 }
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case Expect::HANDLE:
      if(reply->TypeIs(SSH_FXP_HANDLE))
      {
	 handle=((Reply_HANDLE*)reply)->GetHandle(&handle_len);
	 state=(mode==STORE?FILE_SEND:FILE_RECV);
	 assert(!file_buf);
	 file_buf=new Buffer;
	 Log::global->Write(9,"---- got file handle ");
	 for(int i=0; i<handle_len; i++)
	    Log::global->Format(9,"%02X",handle[i]);
	 Log::global->Format(9," (%d)\n",handle_len);
	 request_pos=real_pos=pos;
	 if(mode==RETRIEVE)
	    SendRequest(new Request_FSTAT(handle,handle_len,
	       SSH_FILEXFER_ATTR_SIZE|SSH_FILEXFER_ATTR_MODIFYTIME|SSH_FILEXFER_ATTR_PERMISSIONS,
	       protocol_version),Expect::INFO);
	 else if(mode==STORE)
	 {
	    // truncate the file at write position.
	    Request_FSETSTAT *req=new Request_FSETSTAT(handle,handle_len,protocol_version);
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
	 int h_len;
	 char *handle=((Reply_HANDLE*)reply)->GetHandle(&h_len);
	 SendRequest(new Request_CLOSE(handle,h_len),Expect::IGNORE);
	 xfree(handle);
      }
      break;
   case Expect::DATA:
      if(reply->TypeIs(SSH_FXP_DATA))
      {
	 Request_READ *r=(Request_READ*)e->request;
	 Reply_DATA *d=(Reply_DATA*)reply;
	 if(r->pos==pos+file_buf->Size())
	 {
	    const char *b; int s;
	    d->GetData(&b,&s);
	    Log::global->Format(9,"---- data packet: pos=%lld, size=%d\n",(long long)r->pos,s);
	    file_buf->Put(b,s);
	 }
	 else
	 {
	    if(e->next!=ooo_chain)
	       Log::global->Format(9,"---- put a packet with id=%d on out-of-order chain (need_pos=%lld packet_pos=%lld)\n",
		  reply->GetID(),pos+file_buf->Size(),r->pos);
	    e->next=ooo_chain;
	    ooo_chain=e;
	    return;
	 }
      }
      else if(reply->TypeIs(SSH_FXP_NAME))
      {
	 Reply_NAME *r=(Reply_NAME*)reply;
	 Log::global->Format(9,"---- file name count=%d\n",r->GetCount());
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
      }
      else
      {
	 if(reply->TypeIs(SSH_FXP_STATUS))
	 {
	    if(((Reply_STATUS*)reply)->GetCode()==SSH_FX_EOF)
	    {
	       if(!eof)
		  Log::global->Write(9,"---- eof\n");
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
	 Log::global->Format(9,"---- file info: size=%lld, date=%s",(long long)entity_size,ctime(&entity_date));
	 if(opt_size)
	    *opt_size=entity_size;
	 if(opt_date)
	    *opt_date=entity_date;
      }
      else
	 SetError(NO_FILE,reply);
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
      SendRequest(new Request_READ(handle,handle_len,request_pos,req_len),Expect::DATA);
      request_pos+=req_len;
   } else if(mode==LIST || mode==LONG_LIST) {
      SendRequest(new Request_READDIR(handle,handle_len),Expect::DATA);
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
	 DebugPrint("**** ","Too many out-of-order packets");
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
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Eof() && pty_recv_buf->Size()==0)
      {
	 DebugPrint("**** ",_("Peer closed connection"),0);
	 Disconnect();
	 m=MOVED;
      }
      return m;
   }

   if(recv_buf->IsSuspended())
      return m;

   Packet *reply=0;
   unpack_status_t st=UnpackPacket(recv_buf,&reply);
   if(st==UNPACK_NO_DATA_YET)
      return m;
   if(st!=UNPACK_SUCCESS)
   {
      DebugPrint("**** ",_("invalid server response format"),2);
      Disconnect();
      return MOVED;
   }

   reply->DropData(recv_buf);
   Expect *e=FindExpectExclusive(reply);
   if(e==0)
   {
      DebugPrint("**** ",_("extra server response"),3);
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
      if(RespQueueSize()<max_packets_in_flight && !file_buf->Eof())
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
      retries=0;
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
   file_buf->Put((char*)buf,size);
   retries=0;
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   return(size);
}
int SFtp::Buffered()
{
   if(file_buf==0)
      return 0;
   return file_buf->Size()+send_buf->Size()*size_write/(size_write+20);
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

void SFtp::Suspend()
{
   if(suspended)
      return;
   if(recv_buf)
   {
      recv_buf_suspended=recv_buf->IsSuspended();
      recv_buf->Suspend();
   }
   if(send_buf)
      send_buf->Suspend();
   super::Suspend();
}
void SFtp::Resume()
{
   if(!suspended)
      return;
   super::Resume();
   if(recv_buf && !recv_buf_suspended)
      recv_buf->Resume();
   if(send_buf)
      send_buf->Resume();
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

bool SFtp::SameSiteAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   SFtp *o=(SFtp*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool SFtp::SameLocationAs(FileAccess *fa)
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
   size_read=Query("size-read",c);
   size_write=Query("size-write",c);
   if(size_read<16)
      size_read=16;
   if(size_write<16)
      size_write=16;
}

void SFtp::ClassInit()
{
   // register the class
   Register("sftp",SFtp::New);
}
FileAccess *SFtp::New() { return new SFtp(); }

DirList *SFtp::MakeDirList(ArgV *args)
{
  return new SFtpDirList(args,this);
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
      "No media"
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

SFtp::unpack_status_t SFtp::Reply_NAME::Unpack(Buffer *b)
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
   return UNPACK_SUCCESS;
}
SFtp::unpack_status_t SFtp::NameAttrs::Unpack(Buffer *b,int *offset,int limit,int protocol_version)
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

SFtp::unpack_status_t SFtp::FileAttrs::Unpack(Buffer *b,int *offset,int limit,int protocol_version)
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
      UNPACK64_SIGNED(atime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      UNPACK32(atime_nseconds);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_CREATETIME))
      UNPACK64_SIGNED(createtime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      UNPACK32(createtime_nseconds);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_MODIFYTIME))
      UNPACK64_SIGNED(mtime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      UNPACK32(mtime_nseconds);
   if(atime_nseconds>999999999 || createtime_nseconds>999999999 || mtime_nseconds>999999999)
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
   if(protocol_version<=3)
      PACK32(flags&SSH_FILEXFER_ATTR_MASK_V3);
   else
      PACK32(flags&SSH_FILEXFER_ATTR_MASK_V4);
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
      PACK64_SIGNED(atime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      PACK32(atime_nseconds);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_CREATETIME))
      PACK64_SIGNED(createtime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      PACK32(createtime_nseconds);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_MODIFYTIME))
      PACK64_SIGNED(mtime);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_SUBSECOND_TIMES))
      PACK32(mtime_nseconds);
   if(protocol_version>=4 && (flags & SSH_FILEXFER_ATTR_ACL))
   {
      PACK32(ace_count);
      for(unsigned i=0; i<ace_count; i++)
	 ace[i].Pack(b);
   }
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

SFtp::unpack_status_t SFtp::FileAttrs::FileACE::Unpack(Buffer *b,int *offset,int limit)
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

SFtp::unpack_status_t SFtp::FileAttrs::ExtFileAttr::Unpack(Buffer *b,int *offset,int limit)
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

SFtp::unpack_status_t SFtp::Reply_ATTRS::Unpack(Buffer *b)
{
   unpack_status_t res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   return attrs.Unpack(b,&unpacked,length+4,protocol_version);
}

SFtp::unpack_status_t SFtp::Reply_STATUS::Unpack(Buffer *b)
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
   PACK32(len);
   b->Put(data,len);
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
   FileInfo *fi=new FileInfo(name);
   switch(a->type)
   {
   case SSH_FILEXFER_TYPE_REGULAR:  fi->SetType(fi->NORMAL);    break;
   case SSH_FILEXFER_TYPE_DIRECTORY:fi->SetType(fi->DIRECTORY); break;
   case SSH_FILEXFER_TYPE_SYMLINK:  fi->SetType(fi->SYMLINK);   break;
   default: delete fi; return 0;
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
      FileInfo *ls=FileInfo::parse_ls_line(fi->longname,0);
      if(ls)
      {
	 if(ls->user)
	    fi->SetUser(ls->user);
	 if(ls->group)
	    fi->SetGroup(ls->group);
	 if(ls->nlinks>0)
	    fi->SetNlink(ls->nlinks);
      }
      delete ls;
   }
   if(a->flags&SSH_FILEXFER_ATTR_PERMISSIONS)
      fi->SetMode(a->permissions&07777);
   if(a->flags&SSH_FILEXFER_ATTR_MODIFYTIME)
      fi->SetDate(a->mtime,0);
   return fi;
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
      if(use_cache && LsCache::Find(session,dir,FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size,&fset))
      {
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 fset=new FileSet(fset);
      }
      else
      {
	 session->Open(dir,FA::LONG_LIST);
	 ubuf=new IOBufferFileAccess(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      if(!fset && session->IsOpen())
	 fset=((SFtp*)session)->GetFileSet();
      LsCache::Add(session,dir,FA::LONG_LIST, ubuf, fset);
      if(use_file_set)
      {
	 fset->Sort(fset->BYNAME,false);
	 for(fset->rewind(); fset->curr(); fset->next())
	 {
	    FileInfo *fi=fset->curr();
	    buf->Put(fi->GetLongName());
	    buf->Put("\n");
	 }
	 delete fset;
	 fset=0;
      }
      Delete(ubuf); ubuf=0;
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

SFtpDirList::SFtpDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   use_file_set=true;
   fset=0;
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

SFtpDirList::~SFtpDirList()
{
   Delete(ubuf);
   delete fset;
}

const char *SFtpDirList::Status()
{
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
}

void SFtpDirList::Suspend()
{
   if(ubuf)
      ubuf->Suspend();
   super::Suspend();
}
void SFtpDirList::Resume()
{
   super::Resume();
   if(ubuf)
      ubuf->Resume();
}


#undef super
#define super ListInfo
int SFtpListInfo::Do()
{
   int m=STALL;
   if(!ubuf)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      if(use_cache && LsCache::Find(session,"",FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size,&result))
      {
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
	 result=new FileSet(result);
      }
      else
      {
	 session->Open("",FA::LONG_LIST);
	 ubuf=new IOBufferFileAccess(session);
	 if(LsCache::IsEnabled())
	    ubuf->Save(LsCache::SizeLimit());
      }
   }
   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      if(!result && session->IsOpen())
	 result=((SFtp*)session)->GetFileSet();
      LsCache::Add(session,"",FA::LONG_LIST, ubuf, result);
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
   static char s[256];
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      sprintf(s,_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
      return s;
   }
   return "";
}

#include "modconfig.h"
#ifdef MODULE_PROTO_SFTP
void module_init()
{
   SFtp::ClassInit();
}
#endif
