/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include <stddef.h>
#include "Fish.h"
#include "trio.h"
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include "ascii_ctype.h"
#include "LsCache.h"
#include "misc.h"
#include "log.h"
#include "ArgV.h"

#define super SSH_Access

#define max_buf 0x10000

void Fish::GetBetterConnection(int level)
{
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      Fish *o=(Fish*)fo; // we are sure it is Fish.

      if(!o->recv_buf)
	 continue;

      if(o->state!=CONNECTED || o->mode!=CLOSED)
      {
	 if(level<2)
	    continue;
	 if(!connection_takeover || (o->priority>=priority && !o->IsSuspended()))
	    continue;
	 o->Disconnect();
	 return;
      }

      if(level==0 && xstrcmp(real_cwd,o->real_cwd))
	 continue;

      // borrow the connection
      MoveConnectionHere(o);
      return;
   }
}

int Fish::Do()
{
   int m=STALL;
   int fd;

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
      Disconnect();
      return MOVED;
   }
   if(state!=CONNECTING_1)
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
   if(state!=DISCONNECTED && (state!=CONNECTED || !RespQueueIsEmpty())
   && mode!=CLOSED && CheckTimeout())
      return MOVED;

   if((state==FILE_RECV || state==FILE_SEND)
   && rate_limit==0)
      rate_limit=new RateLimit(hostname);

   const char *charset;
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
      for(int i=0; i<3; i++)
      {
	 if(i>=2 && (connection_limit==0 || connection_limit>CountConnections()))
	    break;
	 GetBetterConnection(i);
	 if(state!=DISCONNECTED)
	    return MOVED;
      }

      if(!ReconnectAllowed())
	 return m;

      if(!NextTry())
	 return MOVED;

      const char *shell=Query("shell",hostname);
      const char *init=xstring::cat("echo FISH:;",shell,NULL);
      const char *prog=Query("connect-program",hostname);
      if(!prog || !prog[0])
	 prog="ssh -a -x";
      ArgV args;
      if(user)
      {
	 args.Add("-l");
	 args.Add(user);
      }
      if(portname)
      {
	 args.Add("-p");
	 args.Add(portname);
      }
      args.Add(hostname);
      args.Add(init);
      xstring_ca cmd_q(args.CombineShellQuoted(0));
      xstring& cmd_str=xstring::cat(prog," ",cmd_q.get(),NULL);
      LogNote(9,"%s (%s)\n",_("Running connect program"),cmd_str.get());
      ssh=new PtyShell(cmd_str);
      ssh->UsePipes();
      state=CONNECTING;
      timeout_timer.Reset();
      m=MOVED;
   }
   case CONNECTING:
      fd=ssh->getfd();
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
      MakePtyBuffers();
      set_real_cwd("~");
      state=CONNECTING_1;
      m=MOVED;

   case CONNECTING_1:
      m|=HandleSSHMessage();
      if(state!=CONNECTING_1)
	 return MOVED;
      if(!received_greeting)
	 return m;

      charset=ResMgr::Query("fish:charset",hostname);
      if(charset && *charset)
      {
	 send_buf->SetTranslation(charset,false);
	 recv_buf->SetTranslation(charset,true);
      }

      Send("#FISH\n"
	   "TZ=GMT;export TZ;LC_ALL=C;export LC_ALL;"
	   "exec 2>&1;echo;start_fish_server;"
	   "echo '### 200'\n");
      PushExpect(EXPECT_FISH);
      Send("#VER 0.0.2\n"
	   "echo '### 000'\n");
      PushExpect(EXPECT_VER);
      if(home_auto==0)
      {
	 Send("#PWD\n"
	      "pwd; echo '### 200'\n");
	 PushExpect(EXPECT_PWD);
      }
      state=CONNECTED;
      m=MOVED;

   case CONNECTED:
      if(mode==CLOSED)
	 return m;
      if(home.path==0 && !RespQueueIsEmpty())
	 break;
      ExpandTildeInCWD();
      if(mode!=CHANGE_DIR && xstrcmp(cwd,real_cwd))
      {
	 if(xstrcmp(path_queue.LastString(),cwd))
	 {
	    Send("#CWD %s\n"
		 "cd %s; echo '### 000'\n",cwd.path.get(),shell_encode(cwd).get());
	    PushExpect(EXPECT_CWD);
	    PushDirectory(cwd);
	 }
	 if(!RespQueueIsEmpty())
	    break;
      }
      SendMethod();
      if(mode==LONG_LIST || mode==LIST || mode==QUOTE_CMD)
      {
	 state=FILE_RECV;
	 m=MOVED;
	 break;
      }
      state=WAITING;
      m=MOVED;
   case WAITING:
      if(RespQueueSize()==1 && mode==RETRIEVE)
      {
	 state=FILE_RECV;
	 m=MOVED;
      }
      if(RespQueueSize()==1 && mode==STORE)
      {
	 state=FILE_SEND;
	 real_pos=0;
	 pos=0;
	 m=MOVED;
      }
      if(RespQueueSize()==0)
      {
	 state=DONE;
	 m=MOVED;
      }
      break;
   case FILE_RECV:
      if(recv_buf->Size()>=rate_limit->BytesAllowedToGet())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(recv_buf->Size()>=max_buf)
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
   case DONE:
      break;
   }
   return m;
}

void Fish::MoveConnectionHere(Fish *o)
{
   super::MoveConnectionHere(o);
   rate_limit=o->rate_limit.borrow();
   path_queue.MoveHere(o->path_queue);
   RespQueue.move_here(o->RespQueue);
   timeout_timer.Reset(o->timeout_timer);
   set_real_cwd(o->real_cwd);
   state=CONNECTED;
   o->Disconnect();
   if(!home)
      set_home(home_auto);
   ResumeInternal();
}

void Fish::DisconnectLL()
{
   super::DisconnectLL();
   EmptyRespQueue();
   EmptyPathQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED,0);
   home_auto.set(FindHomeAuto());
}

void Fish::Init()
{
   state=DISCONNECTED;
   max_send=0;
   eof=false;
}

Fish::Fish() : SSH_Access("FISH:")
{
   Init();
   Reconfig(0);
}

Fish::~Fish()
{
   Disconnect();
}

Fish::Fish(const Fish *o) : super(o)
{
   Init();
   Reconfig(0);
}

void Fish::Close()
{
   switch(state)
   {
   case(DISCONNECTED):
   case(CONNECTED):
   case(DONE):
      break;
   case(WAITING):
      if(mode==STORE || mode==RETRIEVE)
	 Disconnect();
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
   encode_file=true;
   super::Close();
}

void Fish::Send(const char *format,...)
{
   va_list va;
   va_start(va,format);
   xstring& str=xstring::vformat(format,va);
   va_end(va);
   LogSend(5,str);
   send_buf->Put(str);
}

void Fish::SendArrayInfoRequests()
{
   for(int i=fileset_for_info->curr_index(); i<fileset_for_info->count(); i++)
   {
      FileInfo *fi=(*fileset_for_info)[i];
      if(fi->need)
      {
	 const char *e=shell_encode(fi->name);
	 Send("#INFO %s\n"
	      "ls -lLd %s; echo '### 200'\n",fi->name.get(),e);
	 PushExpect(EXPECT_INFO);
      }
   }
}

void Fish::SendMethod()
{
   const char *e=file?alloca_strdup(shell_encode(file)):0;
   const char *e1=shell_encode(file1);
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      Send("#CWD %s\n"
	   "cd %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_CWD);
      PushDirectory(file);
      break;
   case LONG_LIST:
      if(!encode_file)
	 e=file;
      Send("#LIST %s\n"
	   "ls -la %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case LIST:
      if(!encode_file)
	 e=file;
      Send("#LIST %s\n"
	   "ls -a %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case RETRIEVE:
      if(pos>0)
      {
	 int bs=0x1000;
	 real_pos=pos-pos%bs;
	 // non-standard extension
	 Send("#RETRP %lld %s\n"
	      "ls -lLd %s; "
	      "echo '### 100'; "
	      "dd ibs=%d skip=%lld if=%s 2>/dev/null; "
	      "echo '### 200'\n",
	    (long long)real_pos,e,e,bs,(long long)real_pos/bs,e);
      }
      else
      {
	 Send("#RETR %s\n"
	   "ls -lLd %s; "
	   "echo '### 100'; cat %s; echo '### 200'\n",e,e,e);
	 real_pos=0;
      }
      PushExpect(EXPECT_RETR_INFO);
      PushExpect(EXPECT_RETR);
      break;
   case STORE:
      if(entity_size<0)
      {
	 SetError(NO_FILE,"Have to know file size before upload");
	 break;
      }
      if(entity_size>0)
      {
	 Send("#STOR %lld %s\n"
	      "rest=%lld;file=%s;:>$file;echo '### 001';"
	      "if echo 1|head -c 1 -q ->/dev/null 2>&1;then "
		  "head -c $rest -q -|(cat>$file;cat>/dev/null);"
	      "else while [ $rest -gt 0 ];do "
		  "bs=4096;cnt=`expr $rest / $bs`;"
		  "[ $cnt -eq 0 ] && { cnt=1;bs=$rest; }; "
		  "n=`dd ibs=$bs count=$cnt 2>/dev/null|tee -a $file|wc -c`;"
		  "[ \"$n\" -le 0 ] && exit;"
		  "rest=`expr $rest - $n`; "
	      "done;fi;echo '### 200'\n",
	    (long long)entity_size,e,(long long)entity_size,e);
#if 0
	 // dd pays attension to read boundaries and reads wrong number
	 // of bytes when ibs>1. Have to use the inefficient ibs=1.
	 Send("#STOR %lld %s\n"
	      ">%s;echo '### 001';"
	      "dd ibs=1 count=%lld 2>/dev/null"
	      "|(cat>%s;cat>/dev/null);echo '### 200'\n",
	      (long long)entity_size,e,
	      e,
	      (long long)entity_size,
	      e);
#endif
      }
      else
      {
	 Send("#STOR %lld %s\n"
	      ">%s;echo '### 001';echo '### 200'\n",
	      (long long)entity_size,e,e);
      }
      PushExpect(EXPECT_STOR_PRELIMINARY);
      PushExpect(EXPECT_STOR);
      real_pos=0;
      pos=0;
      break;
   case ARRAY_INFO:
      SendArrayInfoRequests();
      break;
   case REMOVE:
      Send("#DELE %s\n"
	   "rm -f %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case REMOVE_DIR:
      Send("#RMD %s\n"
	   "rmdir %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case MAKE_DIR:
      Send("#MKD %s\n"
	   "mkdir %s; echo '### 000'\n",e,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case RENAME:
      Send("#RENAME %s %s\n"
	   "mv %s %s; echo '### 000'\n",e,e1,e,e1);
      PushExpect(EXPECT_DEFAULT);
      break;
   case CHANGE_MODE:
      Send("#CHMOD %04o %s\n"
	   "chmod %04o %s; echo '### 000'\n",chmod_mode,e,chmod_mode,e);
      PushExpect(EXPECT_DEFAULT);
      break;
   case LINK:
      Send("#LINK %s %s\n"
	   "ln %s %s; echo '### 000'\n",e,e1,e,e1);
      PushExpect(EXPECT_DEFAULT);
      break;
   case SYMLINK:
      Send("#SYMLINK %s %s\n"
	   "ln -s %s %s; echo '### 000'\n",e,e1,e,e1);
      PushExpect(EXPECT_DEFAULT);
      break;
   case QUOTE_CMD:
      // non-standard extension
      Send("#EXEC %s\n"
	   "%s; echo '### 200'\n",e,file.get());
      PushExpect(EXPECT_QUOTE);
      real_pos=0;
      break;
   case MP_LIST:
      SetError(NOT_SUPP);
      break;
   case CONNECT_VERIFY:
   case CLOSED:
      abort();
   }
}

int Fish::ReplyLogPriority(int code)
{
   if(code==-1)
      return 3;
   return 4;
}

int Fish::HandleReplies()
{
   int m=STALL;
   if(recv_buf==0)
      return m;
   if(state==FILE_RECV) {
      const char *err=pty_recv_buf->Get();
      if(err && err[0]) {
	 const char *eol=strchr(err,'\n');
	 if(eol) {
	    xstring &e=xstring::get_tmp(err,eol-err);
	    LogError(0,"%s",e.get());
	    SetError(NO_FILE,e);
	    if(pty_recv_buf)
	       pty_recv_buf->Skip(eol-err+1);
	    return MOVED;
	 }
      }
      if(pty_recv_buf->Eof()) {
	 Disconnect();
	 return MOVED;
      }
      if(entity_size!=NO_SIZE && real_pos<entity_size)
	 return m;
      if(entity_size==NO_SIZE)
	 return m;
   }
   recv_buf->Put(pty_recv_buf->Get(),pty_recv_buf->Size()); // join the messages.
   pty_recv_buf->Skip(pty_recv_buf->Size());
   if(recv_buf->Size()<5)
   {
   hup:
      if(recv_buf->Error())
      {
	 Disconnect();
	 return MOVED;
      }
      if(recv_buf->Eof())
      {
	 LogError(0,_("Peer closed connection"));
	 // Solaris' shell exists when is given with wrong directory
	 if(!RespQueueIsEmpty() && RespQueue[0]==EXPECT_CWD && message)
	    SetError(NO_FILE,message);
	 Disconnect();
	 m=MOVED;
      }
      return m;
   }

   const char *b;
   int s;
   recv_buf->Get(&b,&s);
   const char *eol=(const char*)memchr(b,'\n',s);
   if(!eol)
   {
      if(recv_buf->Eof() || recv_buf->Error())
	 goto hup;
      return m;
   }

   m=MOVED;
   s=eol-b+1;
   line.nset(b,s-1);
   recv_buf->Skip(s);

   int code=-1;
   if(s>7 && !memcmp(line,"### ",4)) {
      if(sscanf(line+4,"%3d",&code)!=1)
	 code=-1;
   }

   LogRecv(ReplyLogPriority(code),line);
   if(code==-1)
   {
      if(message==0)
	 message.set(line);
      else {
	 message.append('\n');
	 message.append(line);
      }
      return m;
   }

   if(RespQueueIsEmpty())
   {
      LogError(3,_("extra server response"));
      message.set(0);
      return m;
   }
   expect_t e=RespQueue.next();

   bool keep_message=false;
   switch(e)
   {
   case EXPECT_FISH:
   case EXPECT_VER:
      /* nothing yet */
      break;;
   case EXPECT_PWD:
      if(!message)
	 break;
      home_auto.set(message);
      LogNote(9,"home set to %s\n",home_auto.get());
      PropagateHomeAuto();
      if(!home)
	 set_home(home_auto);
      cache->SetDirectory(this, home, true);
      break;
   case EXPECT_CWD: {
      xstring p;
      PopDirectory(&p);
      if(message==0)
      {
	 set_real_cwd(p);
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    cwd.Set(p);
	    eof=true;
	 }
	 cache->SetDirectory(this,p,true);
      }
      else
	 SetError(NO_FILE,message);
      break;
   }
   case EXPECT_RETR_INFO:
      if(message && is_ascii_digit(message[0]) && !strchr(message,':'))
      {
	 long long size_ll;
	 if(1==sscanf(message,"%lld",&size_ll))
	 {
	    entity_size=size_ll;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
      }
      else if(message && message[0]!='#')
      {
	 FileInfo *fi=FileInfo::parse_ls_line(message,"GMT");
	 if(!fi || !strncmp(message,"ls: ",4))
	 {
	    SetError(NO_FILE,message);
	    break;
	 }
	 if(fi->defined&fi->SIZE)
	 {
	    entity_size=fi->size;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
	 if(fi->defined&fi->DATE)
	 {
	    entity_date=fi->date;
	    if(opt_date)
	       *opt_date=entity_date;
	 }
      }
      state=FILE_RECV;
      break;
   case EXPECT_INFO:
   {
      Ref<FileInfo> new_info(FileInfo::parse_ls_line(message,"GMT"));
      FileInfo *fi=fileset_for_info->curr();
      while(!fi->need)
	 fi=fileset_for_info->next();
      fi->Merge(*new_info);
      fi->need=0;
      break;
   }
   case EXPECT_RETR:
   case EXPECT_DIR:
   case EXPECT_QUOTE:
      eof=true;
      state=DONE;
      break;
   case EXPECT_DEFAULT:
      if(message)
	 SetError(NO_FILE,message);
      break;
   case EXPECT_STOR_PRELIMINARY:
      if(message)
      {
	 Disconnect();
	 SetError(NO_FILE,message);
      }
      break;
   case EXPECT_STOR:
      if(message)
      {
	 Disconnect();
	 SetError(NO_FILE,message);
      }
      break;
   case EXPECT_IGNORE:
      break;
   }

   if(!keep_message)
      message.set(0);

   return m;
}
void Fish::PushExpect(expect_t e)
{
   RespQueue.push(e);
}
void Fish::CloseExpectQueue()
{
   int count=RespQueue.count();
   for(int i=0; i<count; i++)
   {
      switch(RespQueue[i])
      {
      case EXPECT_IGNORE:
      case EXPECT_FISH:
      case EXPECT_VER:
      case EXPECT_PWD:
      case EXPECT_CWD:
	 break;
      case EXPECT_INFO:
      case EXPECT_DIR:
      case EXPECT_DEFAULT:
	 RespQueue[i]=EXPECT_IGNORE;
	 break;
      case EXPECT_QUOTE:
      case EXPECT_RETR_INFO:
      case EXPECT_RETR:
      case EXPECT_STOR_PRELIMINARY:
      case EXPECT_STOR:
	 Disconnect();
	 break;
      }
   }
}

const char *memstr(const char *mem,size_t len,const char *str)
{
   size_t str_len=strlen(str);
   while(len>=str_len)
   {
      if(!memcmp(mem,str,str_len))
	 return mem;
      mem++;
      len--;
   }
   return 0;
}

int Fish::Read(Buffer *buf,int size)
{
   if(Error())
      return error_code;
   if(mode==CLOSED)
      return 0;
   if(state==DONE)
      return 0;	  // eof
   if(state==FILE_RECV && real_pos>=0)
   {
      const char *buf1;
      int size1;
   get_again:
      if(recv_buf->Size()==0 && recv_buf->Error())
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      recv_buf->Get(&buf1,&size1);
      if(buf1==0) // eof
      {
	 Disconnect();
	 return DO_AGAIN;
      }
      if(size1==0)
	 return DO_AGAIN;
      if(entity_size!=NO_SIZE && real_pos<entity_size)
      {
	 if(real_pos+size1>entity_size)
	    size1=entity_size-real_pos;
      }
      else
      {
	 const char *end=memstr(buf1,size1,"### ");
	 if(end)
	 {
	    size1=end-buf1;
	    if(size1==0)
	    {
	       state=WAITING;
	       if(HandleReplies()==MOVED)
		  current->Timeout(0);
	       return DO_AGAIN;
	    }
	 }
	 else
	 {
	    for(int j=0; j<3; j++)
	       if(size1>0 && buf1[size1-1]=='#')
		  size1--;
	    if(size1==0 && recv_buf->Eof())
	    {
	       Disconnect();
	       return DO_AGAIN;
	    }
	 }
      }

      int bytes_allowed=rate_limit->BytesAllowedToGet();
      if(size1>bytes_allowed)
	 size1=bytes_allowed;
      if(size1==0)
	 return DO_AGAIN;
      if(norest_manual && real_pos==0 && pos>0)
	 return DO_AGAIN;
      if(real_pos<pos)
      {
	 off_t to_skip=pos-real_pos;
	 if(to_skip>size1)
	    to_skip=size1;
	 recv_buf->Skip(to_skip);
	 real_pos+=to_skip;
	 goto get_again;
      }
      if(size>size1)
	 size=size1;
      size=buf->MoveDataHere(recv_buf,size);
      if(size<=0)
	 return DO_AGAIN;
      pos+=size;
      real_pos+=size;
      rate_limit->BytesGot(size);
      TrySuccess();
      return size;
   }
   return DO_AGAIN;
}

int Fish::Write(const void *buf,int size)
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
   {
      size=entity_size-pos;
      // tried to write more than originally requested. Make it retry with Open:
      if(size==0)
	 return STORE_FAILED;
   }
   if(size<=0)
      return 0;
   send_buf->Put((char*)buf,size);
   TrySuccess();
   rate_limit->BytesPut(size);
   pos+=size;
   real_pos+=size;
   return(size);
}
int Fish::Buffered()
{
   if(send_buf==0)
      return 0;
   return send_buf->Size();
}
int Fish::StoreStatus()
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
   if(RespQueueSize()==0)
      return OK;
   return IN_PROGRESS;
}

int Fish::Done()
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

void Fish::SuspendInternal()
{
   super::SuspendInternal();
   if(recv_buf)
      recv_buf->SuspendSlave();
   if(send_buf)
      send_buf->SuspendSlave();
}
void Fish::ResumeInternal()
{
   if(recv_buf)
      recv_buf->ResumeSlave();
   if(send_buf)
      send_buf->ResumeSlave();
   super::ResumeInternal();
}

const char *Fish::CurrentStatus()
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

bool Fish::SameSiteAs(const FileAccess *fa) const
{
   if(!SameProtoAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   return(!xstrcasecmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool Fish::SameLocationAs(const FileAccess *fa)	const
{
   if(!SameSiteAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   if(xstrcmp(cwd,o->cwd))
      return false;
   return true;
}

void Fish::Reconfig(const char *name)
{
   super::Reconfig(name);
   if(!xstrcmp(name,"fish:charset") && recv_buf && send_buf)
   {
      if(!IsSuspended())
	 cache->TreeChanged(this,"/");
      const char *charset=ResMgr::Query("fish:charset",hostname);
      if(charset && *charset)
      {
	 send_buf->SetTranslation(charset,false);
	 recv_buf->SetTranslation(charset,true);
      }
      else
      {
	 send_buf->SetTranslator(0);
	 recv_buf->SetTranslator(0);
      }
   }
}

void Fish::ClassInit()
{
   // register the class
   Register("fish",Fish::New);
}
FileAccess *Fish::New() { return new Fish(); }

DirList *Fish::MakeDirList(ArgV *args)
{
   return new FishDirList(this,args);
}
#include "FileGlob.h"
Glob *Fish::MakeGlob(const char *pattern)
{
   return new GenericGlob(this,pattern);
}
ListInfo *Fish::MakeListInfo(const char *p)
{
   return new FishListInfo(this,p);
}

#undef super
#define super DirList
#include "ArgV.h"

int FishDirList::Do()
{
   if(done)
      return STALL;

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
      if(use_cache && FileAccess::cache->Find(session,pattern,FA::LONG_LIST,&err,
				    &cache_buffer,&cache_buffer_size))
      {
	 if(err)
	 {
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 session.Cast<Fish>()->DontEncodeFile();
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
      buf->PutEOF();
      FileAccess::cache->Add(session,pattern,FA::LONG_LIST,FA::OK,ubuf);
      return MOVED;
   }

   int m=STALL;

   if(len>0)
   {
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

const char *FishDirList::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      return xstring::format(_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
   }
   return "";
}

void FishDirList::SuspendInternal()
{
   super::SuspendInternal();
   if(ubuf)
      ubuf->SuspendSlave();
}
void FishDirList::ResumeInternal()
{
   if(ubuf)
      ubuf->ResumeSlave();
   super::ResumeInternal();
}

static FileSet *ls_to_FileSet(const char *b,int len)
{
   FileSet *set=new FileSet;
   while(len>0) {
      // find one line
      const char *line=b;
      int ll=len;
      const char *eol=find_char(b,len,'\n');
      if(eol) {
	 ll=eol-b;
	 len-=ll+1;
	 b+=ll+1;
      } else {
	 len=0;
      }
      if(ll && line[ll-1]=='\r')
	 --ll;
      if(ll==0)
	 continue;

      FileInfo *f=FileInfo::parse_ls_line(line,ll,"GMT");

      if(!f)
	 continue;

      set->Add(f);
   }
   return set;
}

FileSet *Fish::ParseLongList(const char *b,int len,int *err) const
{
   if(err)
      *err=0;
   return ls_to_FileSet(b,len);
}

// FishListInfo implementation
FileSet *FishListInfo::Parse(const char *b,int len)
{
   return ls_to_FileSet(b,len);
}


#include "modconfig.h"
#ifdef MODULE_PROTO_FISH
void module_init()
{
   Fish::ClassInit();
}
#endif
