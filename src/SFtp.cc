#include <config.h>
#include "SFtp.h"
#include "ArgV.h"
#include "log.h"
#include "ascii_ctype.h"
#include "FileGlob.h"
#include "misc.h"

#include <assert.h>

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
	 if(!connection_takeover || o->priority>=priority)
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

      const char *init="sftp";
      ArgV *cmd=new ArgV("ssh");
      cmd->Add("-x");	// don't forward X11
      cmd->Add("-a");	// don't forward AuthAgent.
      if(!strchr(init,'/'))
      {
	 cmd->Add("-s");   // run ssh2 subsystem
	 // unfortunately, sftpd does not have a greeting
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
      cmd->Add(init);
      char *cmd_str=cmd->Combine(0);
      Log::global->Format(9,"---- %s (%s)\n",_("Running ssh..."),cmd_str);
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
      SendRequest(new Request_INIT(4),EXPECT_VERSION);
      state=CONNECTING_2;
      return MOVED;

   case CONNECTING_2:
      if(protocol_version==0)
	 return m;
      if(home==0)
	 SendRequest(new Request_REALPATH("."),EXPECT_HOME_PATH);
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
   case WAITING:
   case DONE:
      return m;
   }
}

void SFtp::MoveConnectionHere(SFtp *o)
{
   send_buf=o->send_buf; o->send_buf=0;
   recv_buf=o->recv_buf; o->recv_buf=0;
   pty_send_buf=o->pty_send_buf; o->pty_send_buf=0;
   pty_recv_buf=o->pty_recv_buf; o->pty_recv_buf=0;
   rate_limit=o->rate_limit; o->rate_limit=0;
   expect_chain=o->expect_chain; o->expect_chain=0;
   expect_chain_end=o->expect_chain_end;
   if(expect_chain_end==&o->expect_chain)
      expect_chain_end=&expect_chain;
   o->expect_chain_end=&o->expect_chain;
   event_time=o->event_time;
   ssh_id=o->ssh_id;
   state=CONNECTED;
   o->Disconnect();
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
   delete ssh; ssh=0;
   EmptyRespQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED);
   received_greeting=false;
   protocol_version=0;
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
   ssh=0;
   ssh_id=0;
   eof=false;
   received_greeting=false;
   expect_chain=0;
   expect_chain_end=&expect_chain;
   ooo_chain=0;
   protocol_version=0;
   handle=0;
   handle_len=0;
}

SFtp::SFtp()
{
   Init();
   Reconfig(0);
}

SFtp::~SFtp()
{
   Disconnect();
}

SFtp::SFtp(const SFtp *o) : super(o)
{
   Init();
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
   }
   res=pp->Unpack(b);
   if(res!=UNPACK_SUCCESS)
   {
      // FIXME: log error
      probe.DropData(b);
      delete *p;
      *p=0;
   }
   return res;
}

void SFtp::SendRequest(Packet *request,expect_t tag)
{
   request->SetID(ssh_id++);
   request->ComputeLength();
   Log::global->Format(9,"---> sending a packet, length=%d, type=%d(%s), id=%u\n",
      request->GetLength(),request->GetPacketType(),request->GetPacketTypeText(),request->GetID());
   request->Pack(send_buf);
   PushExpect(new Expect(request,tag));
}

void SFtp::SendRequest()
{
   unsigned pflags;
   ExpandTildeInCWD();
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      SendRequest(new Request_STAT(dir_file(lc_to_utf8(file),".")),EXPECT_CWD);
      state=WAITING;
      break;
   case RETRIEVE:
      SendRequest(new Request_OPEN(lc_to_utf8(dir_file(cwd,file)),
			SSH_FXF_READ,protocol_version),EXPECT_HANDLE);
      state=WAITING;
      break;
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
   Delete(file_buf); file_buf=0;
   if(handle)
   {
      SendRequest(new Request_CLOSE(handle,handle_len),EXPECT_IGNORE);
      xfree(handle); handle=0; handle_len=0;
   }
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
   case EXPECT_VERSION:
      if(reply->TypeIs(SSH_FXP_VERSION))
      {
	 protocol_version=((Reply_VERSION*)reply)->GetVersion();
	 Log::global->Format(9,"---- protocol version set to %d\n",protocol_version);
      }
      else
      {
	 Disconnect();
	 SetError(FATAL,"cannot negotiate protocol version");
      }
      break;
   case EXPECT_HOME_PATH:
      if(reply->TypeIs(SSH_FXP_NAME))
      {
	 Reply_NAME *r=(Reply_NAME*)reply;
	 const NameAttrs *a=r->GetNameAttrs(0);
	 if(a)
	 {
	    set_home(utf8_to_lc(a->name));
	    Log::global->Format(9,"---- home set to %s\n",home);
	    ExpandTildeInCWD();
	 }
      }
      break;
   case EXPECT_CWD:
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    xfree(cwd);
	    cwd=xstrdup(file);
	    eof=true;
	 }
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case EXPECT_HANDLE:
      if(reply->TypeIs(SSH_FXP_HANDLE))
      {
	 handle=((Reply_HANDLE*)reply)->GetHandle(&handle_len);
	 state=(mode==STORE?FILE_SEND:FILE_RECV);
	 file_buf=new Buffer;
	 Log::global->Write(9,"---- got file handle ");
	 for(int i=0; i<handle_len; i++)
	    Log::global->Format(9,"%02X",handle[i]);
	 Log::global->Format(9," (%d)\n",handle_len);
	 real_pos=pos;
	 if(mode==RETRIEVE)
	    SendRequest(new Request_FSTAT(handle,handle_len),EXPECT_INFO);
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case EXPECT_HANDLE_STALE:
      if(reply->TypeIs(SSH_FXP_HANDLE))
      {
	 // close the handle immediately.
	 int h_len;
	 char *handle=((Reply_HANDLE*)reply)->GetHandle(&h_len);
	 SendRequest(new Request_CLOSE(handle,h_len),EXPECT_IGNORE);
	 xfree(handle);
      }
      break;
   case EXPECT_DATA:
      if(reply->TypeIs(SSH_FXP_DATA))
      {
	 Request_READ *r=(Request_READ*)e->request;
	 Reply_DATA *d=(Reply_DATA*)reply;
	 if(r->pos==pos+file_buf->Size())
	 {
	    const char *b; int s;
	    Log::global->Format(9,"---- data packet: pos=%lld, size=%d\n",(long long)r->pos,s);
	    d->GetData(&b,&s);
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
      else
      {
	 if(reply->TypeIs(SSH_FXP_STATUS))
	 {
	    if(((Reply_STATUS*)reply)->GetCode()==SSH_FX_EOF)
	    {
	       Log::global->Write(9,"---- eof\n");
	       eof=true;
	       state=DONE;
	       if(file_buf)
		  file_buf->PutEOF();
	       break;
	    }
	 }
	 SetError(NO_FILE,reply);
      }
      break;
   case EXPECT_INFO:
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 const FileAttrs *a=((Reply_ATTRS*)reply)->GetAttrs();
	 if(a->flags&SSH_FILEXFER_ATTR_SIZE)
	    entity_size=a->size;
	 if(a->flags&SSH_FILEXFER_ATTR_MODIFYTIME)
	    entity_date=a->mtime;
	 Log::global->Format(9,"---- file info: size=%lld, date=%s",(long long)entity_size,ctime(&entity_date));
	 if(opt_size)
	    *opt_size=entity_size;
	 if(opt_date)
	    *opt_date=entity_date;
      }
      else
	 SetError(NO_FILE,reply);
      break;
   case EXPECT_IGNORE:
      break;
   }
   delete e;
}

int SFtp::HandleReplies()
{
   int m=HandlePty();
   if(recv_buf==0)
      return m;

   Expect *ooo_scan=ooo_chain;
   while(ooo_scan)
   {
      Expect *next=ooo_scan->next;
      ooo_chain=next;
      HandleExpect(ooo_scan);
      ooo_scan=next;
   }

   if(recv_buf->Size()<4)
   {
   hup:
      if(recv_buf->Error())
      {
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Eof())
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
}
void SFtp::DeleteExpect(Expect **e)
{
   if(expect_chain_end==&e[0]->next)
      expect_chain_end=e;
   Expect *d=*e;
   *e=e[0]->next;
   delete d;
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
   return res;
}
void SFtp::CloseExpectQueue()
{
   for(Expect  *e=expect_chain; e; e=e->next)
   {
      switch(e->tag)
      {
      case EXPECT_IGNORE:
      case EXPECT_HANDLE_STALE:
      case EXPECT_HOME_PATH:
      case EXPECT_VERSION:
	 break;
      case EXPECT_CWD:
      case EXPECT_INFO:
      case EXPECT_RETR:
      case EXPECT_DEFAULT:
      case EXPECT_DATA:
	 e->tag=EXPECT_IGNORE;
	 break;
      case EXPECT_STOR_PRELIMINARY:
      case EXPECT_STOR:
	 Disconnect();
	 break;
      case EXPECT_HANDLE:
	 e->tag=EXPECT_HANDLE_STALE;
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
      if((!expect_chain || !expect_chain->next)
      && !file_buf->Eof())
      {
	 int req_len=max_buf/2;
	 // request more data.
	 SendRequest(new Request_READ(handle,handle_len,real_pos,req_len),EXPECT_DATA);
	 real_pos+=req_len;
      }

      const char *buf1;
      int size1;
   get_again:

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

   if(state!=FILE_SEND || rate_limit==0)
      return DO_AGAIN;

   {
      int allowed=rate_limit->BytesAllowedToPut();
      if(allowed==0)
	 return DO_AGAIN;
      if(size+send_buf->Size()>allowed)
	 size=allowed-send_buf->Size();
   }
   if(size+send_buf->Size()>0x4000)
      size=0x4000-send_buf->Size();
   if(pos+size>entity_size)
      size=entity_size-pos;
   if(size<=0)
      return 0;
   // FIXME: pack packets
   send_buf->Put((char*)buf,size);
   retries=0;
   rate_limit->BytesPut(size);
   pos+=size;
   return(size);
}
int SFtp::Buffered()
{
   if(send_buf==0)
      return 0;
   return send_buf->Size();
}
int SFtp::StoreStatus()
{
   if(Error())
      return error_code;
   if(state!=FILE_SEND)
      return IN_PROGRESS;
   if(real_pos!=entity_size)
   {
      Disconnect();
      return IN_PROGRESS;
   }
   if(RespQueueIsEmpty())
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
}

void SFtp::ClassInit()
{
   // register the class
   Register("sftp",SFtp::New);
}
FileAccess *SFtp::New() { return new SFtp(); }

DirList *SFtp::MakeDirList(ArgV *args)
{
//   return new SFtpDirList(args,this);
// FIXME
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
      UNPACK32(permissions);
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
   PACK32(flags);
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
      PACK32(permissions&S_IAMB);
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

SFtp::unpack_status_t SFtp::FileACE::Unpack(Buffer *b,int *offset,int limit)
{
   UNPACK32(ace_type);
   UNPACK32(ace_flag);
   UNPACK32(ace_mask);
   return Packet::UnpackString(b,offset,limit,&who);
}
void SFtp::FileACE::Pack(Buffer *b)
{
   PACK32(ace_type);
   PACK32(ace_flag);
   PACK32(ace_mask);
   Packet::PackString(b,who);
}

SFtp::unpack_status_t SFtp::ExtFileAttr::Unpack(Buffer *b,int *offset,int limit)
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
void SFtp::ExtFileAttr::Pack(Buffer *b)
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
   if(protocol_version>=4)
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

const char *SFtp::utf8_to_lc(const char *s)
{
   static char *buf=0;
   static int buf_size=0;

   if(protocol_version<4)
      return s;

   //FIXME
   return s;
}
const char *SFtp::lc_to_utf8(const char *s)
{
   static char *buf=0;
   static int buf_size=0;

   if(protocol_version<4)
      return s;

   //FIXME
   return s;
}
