#include <config.h>
#include "SFtp.h"
#include "ArgV.h"
#include "log.h"
#include "ascii_ctype.h"
#include "FileGlob.h"

#include <assert.h>

#define super NetAccess

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

      // walk through Fish classes and try to find identical idle session
      // first try "easy" cases of session take-over.
      count=CountConnections();
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>count))
	    break;
	 GetBetterConnection(i,count);
	 if(state!=DISCONNECTED)
	    return MOVED;
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
	 const char *greeting="echo SFTP:";
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
      ssh->Kill(SIGCONT);
      send_buf=new IOBufferFDStream(ssh,IOBuffer::PUT);
      ssh=0;
      recv_buf=new IOBufferFDStream(new FDStream(fd,"pseudo-tty"),IOBuffer::GET);
      set_real_cwd("~");
      state=CONNECTING_1;
      m=MOVED;
   }
   case CONNECTING_1:
      if(!received_greeting)
	 return m;
      SendRequest(new Request_INIT(),EXPECT_VERSION);
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
   }
}

SFtp::unpack_status_t SFtp::Packet::DecodeString(Buffer *b,int *offset,int max_len,char **str_out,int *len_out)
{
   if(b->Size()-*offset<4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;

   int len=b->UnpackUINT32BE(*offset);
   if(max_len>b->Size()-4)
      max_len=b->Size()-4;
   if(len>max_len)
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

   switch(probe.GetPacketType())
   {
   case SSH_FXP_VERSION:
      pp=new Reply_VERSION();
      break;
   case SSH_FXP_NAME:
      pp=new Reply_NAME(protocol_version);
      res=pp->Unpack(b);
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

void SFtp::SendRequest()
{
   switch((open_mode)mode)
   {
   }
}

void SFtp::Init()
{
   state=DISCONNECTED;
   send_buf=0;
   recv_buf=0;
   recv_buf_suspended=false;
   ssh=0;
   ssh_id=0;
   eof=false;
   path_queue=0;
   path_queue_len=0;
   received_greeting=false;
   expect_chain=0;
   protocol_version=0;
}

SFtp::SFtp()
{
   Init();
   Reconfig(0);
}

SFtp::~SFtp()
{
   Disconnect();
   EmptyRespQueue();
   EmptyPathQueue();
   xfree(path_queue);
}

SFtp::SFtp(const SFtp *o) : super(o)
{
   Init();
}

void SFtp::Close()
{
   switch(state)
   {
   case(DISCONNECTED):
   case(WAITING):
   case(CONNECTED):
   case(DONE):
      break;
   case(FILE_SEND):
      if(!RespQueueIsEmpty())
	 Disconnect();
      break;
   case(FILE_RECV):
   case(CONNECTING):
   case(CONNECTING_1):
      Disconnect();
   }
//    if(!RespQueueIsEmpty())
//       Disconnect(); // play safe.
   CloseExpectQueue();
   state=(recv_buf?CONNECTED:DISCONNECTED);
   eof=false;
   super::Close();
}

int SFtp::HandleReplies()
{
   int m=STALL;
   if(recv_buf==0 || state==FILE_RECV)
      return m;
   if((reply_length!=-1 && recv_buf->Size()<reply_length)
   || (reply_length==-1 && recv_buf->Size()<4))
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
   const char *b;
   int s;
   recv_buf->Get(&b,&s);
   if(state==CONNECTING_1)
   {
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
	    recv_buf->Put("XXXX");
	    send_buf->Put(pass);
	    send_buf->Put("\n");
	    return m;
	 }
	 if(s>=y_len && !strncasecmp(b+s-y_len,y,y_len))
	 {
	    recv_buf->Put("yes\n");
	    send_buf->Put("yes\n");
	    return m;
	 }
	 if(recv_buf->Eof() || recv_buf->Error())
	    goto hup;
      	 return m;
      }
      m=MOVED;
      s=eol-b+1;
      char *line=string_alloca(s);
      memcpy(line,b,s-1);
      line[s-1]=0;
      recv_buf->Skip(s);

      DebugPrint("<--- ",line,4);
      if(!received_greeting && !strcmp(line,"SFTP:"))
	 received_greeting=true;
      return m;
   }

   Packet *reply=0;
   unpack_status_t st=UnpackPacket(recv_buf,&reply);
   if(st==UNPACK_NO_DATA_YET)
      return m;
   if(st!=UNPACK_SUCCESS)
   {
      Disconnect();
      return MOVED;
   }

   reply->DropData(recv_buf);
   Expect **e=FindExpect(reply);
   if(e==0 || *e==0)
   {
      DebugPrint("**** ",_("extra server response"),3);
      delete reply;
      return MOVED;
   }

   char *p;
   switch(e[0]->tag)
   {
   case EXPECT_VERSION:
      /* nothing yet */
      break;;
   case EXPECT_HOME_PATH:
      if(reply->TypeIs(SSH_FXP_NAME))
      {
	 Reply_NAME *r=(Reply_NAME*)reply;
	 const NameAttrs *a=r->GetNameAttrs(0);
	 if(a)
	 {
	    set_home(a->name);
	    Log::global->Format(10,"home set to %s\n",home);
	 }
      }
      break;
   case EXPECT_CWD:
      p=PopDirectory();
      if(reply->TypeIs(SSH_FXP_ATTRS))
      {
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    xfree(cwd);
	    cwd=p;
	    p=0;
	    eof=true;
	 }
      }
      else
	 SetError(NO_FILE,reply);
      xfree(p);
      break;
   case EXPECT_IGNORE:
      break;
   }

   DeleteExpect(e);
   return MOVED;

protocol_error:
   DebugPrint("**** ",_("invalid server response format"),2);
   Disconnect();
   return MOVED;
}
void SFtp::PushExpect(Expect *e)
{

}
void SFtp::CloseExpectQueue()
{
   for(Expect  *e=expect_chain; e; e=e->next)
   {
      switch(e->tag)
      {
      case EXPECT_IGNORE:
      case EXPECT_PWD:
      case EXPECT_CWD:
	 break;
      case EXPECT_RETR_INFO:
      case EXPECT_INFO:
      case EXPECT_RETR:
      case EXPECT_DIR:
      case EXPECT_QUOTE:
      case EXPECT_DEFAULT:
	 e->tag=EXPECT_IGNORE;
	 break;
      case EXPECT_STOR_PRELIMINARY:
      case EXPECT_STOR:
	 Disconnect();
	 break;
      }
   }
}

void SFtp::PushDirectory(const char *p)
{
   path_queue=(char**)xrealloc(path_queue,++path_queue_len*sizeof(*path_queue));
   path_queue[path_queue_len-1]=xstrdup(p);
}
char *SFtp::PopDirectory()
{
   assert(path_queue_len>0);
   char *p=path_queue[0];
   memmove(path_queue,path_queue+1,--path_queue_len*sizeof(*path_queue));
   return p; // caller should free it.
}

Glob *SFtp::MakeGlob(const char *pat)
{
   return new GenericGlob(this,pat);
}
ListInfo *SFtp::MakeListInfo(const char *dir)
{
   return new SFtpListInfo(this,dir);
}
