/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include "Fish.h"
#include <stdio.h>
#ifdef NEED_TRIO
#include "trio.h"
#define vsnprintf trio_vsnprintf
#endif
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include <time.h>
#include "ascii_ctype.h"
#include "LsCache.h"
#include "misc.h"
#include "log.h"

#define super NetAccess

#define max_buf 0x10000

static FileInfo *ls_to_FileInfo(char *line);

void Fish::GetBetterConnection(int level,int count)
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
	 if(!connection_takeover || o->priority>=priority)
	    continue;
	 o->Disconnect();
	 return;
      }
      else
      {
	 takeover_time=now;
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
      return;
   }
}

int Fish::Do()
{
   int m=STALL;
   int fd;
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

      if(pipe(pipe_in)==-1)
      {
	 if(errno==EMFILE || errno==ENFILE)
	 {
	    TimeoutS(1);
	    return m;
	 }
	 SetError(SEE_ERRNO,"pipe()");
	 return MOVED;
      }
      NonBlock(pipe_in[0]);
      CloseOnExec(pipe_in[0]);
      CloseOnExec(pipe_in[1]);

      const char *init="\"echo FISH:;/bin/bash\"";
      char *cmd=string_alloca(xstrlen(hostname)*2+xstrlen(portname)*2
			      +xstrlen(user)*2+128);
      strcpy(cmd,"ssh");
      if(user)
      {
	 strcat(cmd," -l ");
	 strcat(cmd,shell_encode(user));
      }
      if(portname)
      {
	 strcat(cmd," -p ");
	 strcat(cmd,shell_encode(portname));
      }
      strcat(cmd," ");
      strcat(cmd,shell_encode(hostname));
      strcat(cmd," ");
      strcat(cmd,init);
      Log::global->Format(9,"---- %s (%s)\n",_("Running ssh..."),cmd);
      filter_out=new OutputFilter(cmd,pipe_in[1]);
      filter_out->StderrToStdout();
      state=CONNECTING;
      event_time=now;
      m=MOVED;
   }
   case CONNECTING:
      fd=filter_out->getfd();
      if(fd==-1)
      {
	 if(filter_out->error())
	 {
	    SetError(FATAL,filter_out->error_text);
	    return MOVED;
	 }
	 TimeoutS(1);
	 return m;
      }
      filter_out->Kill(SIGCONT);
      send_buf=new IOBufferFDStream(filter_out,IOBuffer::PUT);
      filter_out=0;
      recv_buf=new IOBufferFDStream(new FDStream(pipe_in[0],"pipe in"),IOBuffer::GET);
      close(pipe_in[1]);
      pipe_in[1]=-1;
      state=CONNECTED;
      set_real_cwd("~");
      m=MOVED;

      Send("#FISH\n"
	   "echo;start_fish_server;"
	   "TZ=GMT;export TZ;LC_ALL=C;export LC_ALL;"
	   "echo '### 200'\n");
      PushExpect(EXPECT_FISH);
      Send("#VER 0.0.2\n"
	   "echo '### 000'\n");
      PushExpect(EXPECT_VER);
      if(home==0)
      {
	 Send("#PWD\n"
	      "pwd; echo '### 200'\n");
	 PushExpect(EXPECT_PWD);
      }
   case CONNECTED:
      if(mode==CLOSED)
	 return m;
      if(home==0 && !RespQueueIsEmpty())
	 goto usual_return;
      ExpandTildeInCWD();
      if(mode!=CHANGE_DIR && xstrcmp(cwd,real_cwd))
      {
	 if(path_queue_len==0 || strcmp(path_queue[path_queue_len-1],cwd))
	 {
	    Send("#CWD %s\n"
		 "cd %s; echo '### 000'\n",cwd,shell_encode(cwd));
	    PushExpect(EXPECT_CWD);
	    PushDirectory(cwd);
	 }
	 if(!RespQueueIsEmpty())
	    goto usual_return;
      }
      SendMethod();
      if(mode==LONG_LIST || mode==LIST || mode==QUOTE_CMD)
      {
	 state=FILE_RECV;
	 m=MOVED;
	 goto usual_return;
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
	 m=MOVED;
      }
      if(RespQueueSize()==0)
      {
	 if(mode==ARRAY_INFO && array_ptr<array_cnt)
	    SendArrayInfoRequests();
	 else
	    state=DONE;
	 m=MOVED;
      }
      goto usual_return;
   case FILE_RECV:
      if(recv_buf->Size()>=rate_limit->BytesAllowed())
      {
	 recv_buf->Suspend();
	 Timeout(1000);
      }
      else if(recv_buf->Size()>=max_buf)
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
   case DONE:
      goto usual_return;
   }
usual_return:
   if(m==MOVED)
      return MOVED;
   if(send_buf)
      BumpEventTime(send_buf->EventTime());
   if(recv_buf)
      BumpEventTime(recv_buf->EventTime());
   if(CheckTimeout())
      return MOVED;
// notimeout_return:
   if(m==MOVED)
      return MOVED;
   return m;
}

void Fish::MoveConnectionHere(Fish *o)
{
   send_buf=o->send_buf; o->send_buf=0;
   recv_buf=o->recv_buf; o->recv_buf=0;
   pipe_in[0]=o->pipe_in[0]; o->pipe_in[0]=-1;
   pipe_in[1]=o->pipe_in[1]; o->pipe_in[1]=-1;
   rate_limit=o->rate_limit; o->rate_limit=0;
   path_queue=o->path_queue; o->path_queue=0;
   path_queue_len=o->path_queue_len; o->path_queue_len=0;
   RespQueue=o->RespQueue; o->RespQueue=0;
   RQ_alloc=o->RQ_alloc; o->RQ_alloc=0;
   RQ_head=o->RQ_head; o->RQ_head=0;
   RQ_tail=o->RQ_tail; o->RQ_tail=0;
   event_time=o->event_time;
   set_real_cwd(o->real_cwd);
   o->set_real_cwd(0);
   state=CONNECTED;
   o->Disconnect();
}

void Fish::Disconnect()
{
   if(send_buf)
      DebugPrint("---- ",_("Disconnecting"),9);
   Delete(send_buf); send_buf=0;
   Delete(recv_buf); recv_buf=0;
   if(pipe_in[0]!=-1)
      close(pipe_in[0]);
   pipe_in[0]=-1;
   if(pipe_in[1]!=-1)
      close(pipe_in[1]);
   pipe_in[1]=-1;
   if(filter_out)
      delete filter_out;
   filter_out=0;
   EmptyRespQueue();
   EmptyPathQueue();
   state=DISCONNECTED;
   if(mode==STORE)
      SetError(STORE_FAILED,0);
}

void Fish::EmptyPathQueue()
{
   for(int i=0; i<path_queue_len; i++)
      xfree(path_queue[i]);
   path_queue_len=0;
}

void Fish::Init()
{
   state=DISCONNECTED;
   send_buf=0;
   recv_buf=0;
   recv_buf_suspended=false;
   pipe_in[0]=pipe_in[1]=-1;
   filter_out=0;
   max_send=0;
   line=0;
   message=0;
   RespQueue=0;
   RQ_alloc=0;
   RQ_head=RQ_tail=0;
   eof=false;
   path_queue=0;
   path_queue_len=0;
}

Fish::Fish()
{
   Init();
   Reconfig(0);
}

Fish::~Fish()
{
   Disconnect();
   xfree(line);
   xfree(message);
   EmptyRespQueue();
   xfree(RespQueue);
   EmptyPathQueue();
   xfree(path_queue);
}

Fish::Fish(const Fish *o) : super(o)
{
   Init();
}

void Fish::Close()
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
   char *str;

   static int max_send=256;
   for(;;)
   {
      str=string_alloca(max_send);
      int res=vsnprintf(str,max_send,format,va);
      if(res>=0 && res<max_send)
      {
	 if(res<max_send/16)
	    max_send/=2;
	 break;
      }
      max_send*=2;
   }

   DebugPrint("---> ",str,5);
   send_buf->Put(str);
   va_end(va);
}

const char *Fish::shell_encode(const char *string)
{
   if(!string)
      return 0;

   int c;
   static char *result=0;
   char *r;
   const char *s;

   result = (char*)xrealloc (result, 2 * strlen (string) + 1);

   for (r = result, s = string; s && (c = *s); s++)
   {
      switch (c)
      {
      case '\'':
      case '(': case ')':
      case '!': case '{': case '}':		/* reserved words */
      case '^':
      case '$': case '`':			/* expansion chars */
      case '*': case '[': case '?': case ']':	/* globbing chars */
      case ' ': case '\t': case '\n':		/* IFS white space */
      case '"': case '\\':		/* quoting chars */
      case '|': case '&': case ';':		/* shell metacharacters */
      case '<': case '>':
	 *r++ = '\\';
	 *r++ = c;
	 break;
      case '~':				/* tilde expansion */
      case '#':				/* comment char */
	 if (s == string)
	    *r++ = '\\';
	 /* FALLTHROUGH */
      default:
	 *r++ = c;
	 break;
      }
   }

   *r = '\0';
   return (result);
}

void Fish::SendArrayInfoRequests()
{
   for(int i=array_ptr; i<array_cnt; i++)
   {
      if(array_for_info[i].get_time || array_for_info[i].get_size)
      {
	 const char *e=shell_encode(array_for_info[i].file);
	 Send("#INFO %s\n"
	      "ls -lLd %s; echo '### 200'\n",array_for_info[i].file,e);
	 PushExpect(EXPECT_INFO);
      }
      else
      {
	 if(i==array_ptr)
	    array_ptr++;   // if it is first one, just skip it.
	 else
	    break;	   // otherwise, wait until it is first.
      }
   }
}

void Fish::SendMethod()
{
   const char *e=shell_encode(file);
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
	   "ls -l %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case LIST:
      if(!encode_file)
	 e=file;
      Send("#LIST %s\n"
	   "ls %s; echo '### 200'\n",e,e);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case RETRIEVE:
      Send("#RETR %s\n"
	   "ls -lLd %s; "
	   "echo '### 100'; cat %s; echo '### 200'\n",e,e,e);
      PushExpect(EXPECT_RETR_INFO);
      PushExpect(EXPECT_RETR);
      real_pos=0;
      break;
   case STORE:
      if(entity_size<0)
      {
	 SetError(NO_FILE,"Have to know file size before upload");
	 break;
      }
      // dd pays attansion to read boundaries and reads wrong number
      // of bytes when ibs>1. Have to use the inefficient ibs=1.
      Send("#STOR %lld %s\n"
           ">%s;echo '### 001';"
	   "dd ibs=1 count=%lld 2>/dev/null"
	   "|(cat>%s;cat>/dev/null);echo '### 200'\n",
	   (long long)entity_size,e,
	   e,
	   (long long)entity_size,
	   e);
      PushExpect(EXPECT_STOR_PRELIMINARY);
      PushExpect(EXPECT_STOR);
      real_pos=0;
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
   case QUOTE_CMD:
      Send("#EXEC %s\n"
	   "%s; echo '### 200'\n",file,file);
      PushExpect(EXPECT_QUOTE);
      real_pos=0;
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
   if(recv_buf==0 || state==FILE_RECV)
      return m;
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
	 DebugPrint("**** ",_("Peer closed connection"),0);
	 if(!RespQueueIsEmpty() && RespQueue[RQ_head]==EXPECT_CWD && message)
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
   xfree(line);
   line=(char*)xmemdup(b,s);
   line[s-1]=0;
   recv_buf->Skip(s);

   int code=-1;
   if(s>7 && !memcmp(line,"### ",4) && is_ascii_digit(line[4]))
      sscanf(line+4,"%3d",&code);

   DebugPrint("<--- ",line,ReplyLogPriority(code));
   if(code==-1)
   {
      if(message==0)
	 message=xstrdup(line);
      else
      {
	 message=(char*)xrealloc(message,xstrlen(message)+s+1);
	 strcat(message,"\n");
	 strcat(message,line);
      }
      return m;
   }

   if(RespQueueIsEmpty())
   {
      DebugPrint("**** ",_("extra server response"),3);
      xfree(message);
      message=0;
      return m;
   }
   expect_t &e=RespQueue[RQ_head];
   RQ_head++;

   bool keep_message=false;
   char *p;

   switch(e)
   {
   case EXPECT_FISH:
   case EXPECT_VER:
      // nothing (yet).
      break;;
   case EXPECT_PWD:
      if(!message)
	 break;
      xfree(home);
      home=message;
      message=0;
      break;
   case EXPECT_CWD:
      p=PopDirectory();
      if(message==0)
      {
	 set_real_cwd(p);
	 if(mode==CHANGE_DIR && RespQueueIsEmpty())
	 {
	    xfree(cwd);
	    cwd=p;
	    p=0;
	    eof=true;
	 }
      }
      else
	 SetError(NO_FILE,message);
      xfree(p);
      break;
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
      else if(message)
      {
	 FileInfo *fi=ls_to_FileInfo(message);
	 if(!fi)
	 {
	    SetError(NO_FILE,message);
	    return MOVED;
	 }
	 if(fi->defined&fi->SIZE)
	 {
	    entity_size=fi->size;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
	 if(fi->defined&(fi->DATE|fi->DATE_UNPREC))
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
      FileInfo *fi=ls_to_FileInfo(message);
      if(fi && fi->defined&fi->SIZE)
	 array_for_info[array_ptr].size=fi->size;
      else
	 array_for_info[array_ptr].size=NO_SIZE;
      if(fi && fi->defined&(fi->DATE|fi->DATE_UNPREC))
	 array_for_info[array_ptr].time=fi->date;
      else
	 array_for_info[array_ptr].time=NO_DATE;
      array_for_info[array_ptr].get_size=false;
      array_for_info[array_ptr].get_time=false;
      array_ptr++;
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
   {
      xfree(message);
      message=0;
   }

   return m;
}
void Fish::PushExpect(expect_t e)
{
   int newtail=RQ_tail+1;
   if(newtail>RQ_alloc)
   {
      if(RQ_head-0<newtail-RQ_alloc)
	 RespQueue=(expect_t*)
	    xrealloc(RespQueue,(RQ_alloc=newtail+16)*sizeof(*RespQueue));
      memmove(RespQueue,RespQueue+RQ_head,(RQ_tail-RQ_head)*sizeof(*RespQueue));
      RQ_tail=0+(RQ_tail-RQ_head);
      RQ_head=0;
      newtail=RQ_tail+1;
   }
   RespQueue[RQ_tail]=e;
   RQ_tail=newtail;
}
void Fish::CloseExpectQueue()
{
   for(int i=RQ_head; i<RQ_tail; i++)
   {
      switch(RespQueue[i])
      {
      case EXPECT_IGNORE:
      case EXPECT_FISH:
      case EXPECT_VER:
      case EXPECT_PWD:
      case EXPECT_CWD:
	 break;
      case EXPECT_RETR_INFO:
      case EXPECT_INFO:
      case EXPECT_RETR:
      case EXPECT_DIR:
      case EXPECT_QUOTE:
      case EXPECT_DEFAULT:
	 RespQueue[i]=EXPECT_IGNORE;
	 break;
      case EXPECT_STOR_PRELIMINARY:
      case EXPECT_STOR:
	 Disconnect();
	 break;
      }
   }
}

void Fish::PushDirectory(const char *p)
{
   path_queue=(char**)xrealloc(path_queue,++path_queue_len*sizeof(*path_queue));
   path_queue[path_queue_len-1]=xstrdup(p);
}
char *Fish::PopDirectory()
{
   assert(path_queue_len>0);
   char *p=path_queue[0];
   memmove(path_queue,path_queue+1,--path_queue_len*sizeof(*path_queue));
   return p; // caller should free it.
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

int Fish::Read(void *buf,int size)
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
      if(entity_size>=0 && pos>=entity_size)
      {
	 DebugPrint("---- ",_("Received all (total)"));
	 state=WAITING;
	 return 0;
      }
      if(entity_size>=0 && real_pos+size1>entity_size)
	 size1=entity_size-real_pos;
      if(entity_size==NO_SIZE)
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

      int bytes_allowed=rate_limit->BytesAllowed();
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
      memcpy(buf,buf1,size);
      recv_buf->Skip(size);
      pos+=size;
      real_pos+=size;
      rate_limit->BytesUsed(size);
      retries=0;
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
      int allowed=rate_limit->BytesAllowed();
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
   send_buf->Put((char*)buf,size);
   retries=0;
   rate_limit->BytesUsed(size);
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

void Fish::Suspend()
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
void Fish::Resume()
{
   if(!suspended)
      return;
   super::Resume();
   if(recv_buf && !recv_buf_suspended)
      recv_buf->Resume();
   if(send_buf)
      send_buf->Resume();
}

const char *Fish::CurrentStatus()
{
   switch(state)
   {
   case DISCONNECTED:
      return _("Not connected");
   case CONNECTING:
      return _("Connecting...");
   case CONNECTED:
      if(!RespQueueIsEmpty() && RespQueue[RQ_head]==EXPECT_FISH)
	 return _("Connecting...");
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

bool Fish::SameSiteAs(FileAccess *fa)
{
   if(!SameProtoAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   return(!xstrcmp(hostname,o->hostname) && !xstrcmp(portname,o->portname)
   && !xstrcmp(user,o->user) && !xstrcmp(pass,o->pass));
}

bool Fish::SameLocationAs(FileAccess *fa)
{
   if(!SameSiteAs(fa))
      return false;
   Fish *o=(Fish*)fa;
   if(xstrcmp(cwd,o->cwd))
      return false;
   return true;
}

void Fish::Cleanup()
{
   if(hostname==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}
void Fish::CleanupThis()
{
   if(mode!=CLOSED)
      return;
   Disconnect();
}

void Fish::Reconfig(const char *name)
{
   super::Reconfig(name);
}

void Fish::ClassInit()
{
   // register the class
   Register("fish",Fish::New);
}
FileAccess *Fish::New() { return new Fish(); }

DirList *Fish::MakeDirList(ArgV *args)
{
   return new FishDirList(args,this);
}
Glob *Fish::MakeGlob(const char *pattern)
{
   return new FishGlob(this,pattern);
}
ListInfo *Fish::MakeListInfo()
{
   return new FishListInfo(this);
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
      if(use_cache && LsCache::Find(session,pattern,FA::LONG_LIST,
				    &cache_buffer,&cache_buffer_size))
      {
	 ubuf=new Buffer();
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 ((Fish*)session)->DontEncodeFile();
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
      buf->PutEOF();

      const char *cache_buffer;
      int cache_buffer_size;
      ubuf->GetSaved(&cache_buffer,&cache_buffer_size);
      if(cache_buffer && cache_buffer_size>0)
      {
	 LsCache::Add(session,pattern,FA::LONG_LIST,
		      cache_buffer,cache_buffer_size);
      }

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

FishDirList::FishDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   pattern=args->Combine(1);
}

FishDirList::~FishDirList()
{
   Delete(ubuf);
   xfree(pattern);
}

const char *FishDirList::Status()
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

void FishDirList::Suspend()
{
   if(ubuf)
      ubuf->Suspend();
   super::Suspend();
}
void FishDirList::Resume()
{
   super::Resume();
   if(ubuf)
      ubuf->Resume();
}

static FileInfo *ls_to_FileInfo(char *line)
{
   int year=-1,month=-1,day=0,hour=0,minute=0;
   char month_name[32]="";
   char perms[12]="";
   int perms_code;
   int n_links;
   char user[32];
   char group[32];
   char year_or_time[6];
   int consumed;
   bool is_directory=false;
   bool is_sym_link=false;
   char *sym_link=0;
   long long size;
   int n;

   n=sscanf(line,"%11s %d %31s %31s %lld %3s %2d %5s%n",perms,&n_links,
	 user,group,&size,month_name,&day,year_or_time,&consumed);
   if(n==4) // bsd-like listing without group?
   {
      group[0]=0;
      n=sscanf(line,"%11s %d %31s %lld %3s %2d %5s%n",perms,&n_links,
	    user,&size,month_name,&day,year_or_time,&consumed);
   }
   if(n>=7 && -1!=(perms_code=parse_perms(perms+1))
   && -1!=(month=parse_month(month_name))
   && -1!=parse_year_or_time(year_or_time,&year,&hour,&minute))
   {
      if(perms[0]=='d')
	 is_directory=true;
      else if(perms[0]=='l')
      {
	 is_sym_link=true;
	 sym_link=strstr(line+consumed+1," -> ");
	 if(sym_link)
	 {
	    *sym_link=0;
	    sym_link+=4;
	 }
      }

      if(year!=-1)
      {
	 // server's y2000 problem :)
	 if(year<37)
	    year+=2000;
	 else if(year<100)
	    year+=1900;
      }

      if(day<1 || day>31 || hour<0 || hour>23 || minute<0 || minute>59
      || (month==-1 && !is_ascii_alnum((unsigned char)month_name[0])))
	 return 0;

      if(month==-1)
	 month=parse_month(month_name);
      if(month>=0)
      {
	 sprintf(month_name,"%02d",month+1);
	 if(year==-1)
	    year=guess_year(month,day);
      }

      FileInfo *fi=new FileInfo;
      fi->SetName(line+consumed+1);
      if(sym_link)
	 fi->SetSymlink(sym_link);
      else
	 fi->SetType(is_directory ? fi->DIRECTORY : fi->NORMAL);

      if(year!=-1 && month!=-1 && day!=-1 && hour!=-1 && minute!=-1)
      {
	 struct tm date;

	 date.tm_year=year-1900;
	 date.tm_mon=month;
	 date.tm_mday=day;
	 date.tm_hour=hour;
	 date.tm_min=minute;

	 date.tm_isdst=-1;
	 date.tm_sec=0;

	 fi->SetDateUnprec(mktime_from_utc(&date));
      }

      fi->SetSize(size);

      return fi;
   }
   return 0;
}

static FileSet *ls_to_FileSet(const char *b,int len)
{
   FileSet *set=new FileSet;
   char *buf=string_alloca(len+1);
   memcpy(buf,b,len);
   buf[len]=0;
   for(char *line=strtok(buf,"\n"); line; line=strtok(0,"\n"))
   {
      int ll=strlen(line);
      if(ll && line[ll-1]=='\r')
	 line[--ll]=0;
      if(ll==0)
	 continue;

      FileInfo *f=ls_to_FileInfo(line);

      if(!f)
	 continue;

      set->Add(f);
   }
   return set;
}

// FishGlob implementation
GenericParseGlob *FishGlob::MakeUpDirGlob()
{
   return new FishGlob(session,dir);
}
FileSet *FishGlob::Parse(const char *b,int len)
{
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
