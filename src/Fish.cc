/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <assert.h>
#include "ascii_ctype.h"

#define super NetAccess

int Fish::Do()
{
   int m=STALL;
   int fd;

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

   m|=HandleReplies();

   if((state==FILE_RECV || state==FILE_SEND)
   && rate_limit==0)
      rate_limit=new RateLimit();

   switch(state)
   {
   case DISCONNECTED:
      if(mode==CLOSED)
	 return m;
      if(mode==CONNECT_VERIFY)
	 return m;

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

      DebugPrint("---- ",_("Running ssh..."));
      filter_out=new OutputFilter("ssh gemini \"echo FISH:;/bin/bash\"",pipe_in[1]);   // FIXME
      filter_out->StderrToStdout();
      state=CONNECTING;
      m=MOVED;
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
      m=MOVED;

      Send("#FISH\n"
	   "echo; start_fish_server; echo '### 200'\n");
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
      state=WAITING;
      m=MOVED;
   case WAITING:
      if(RespQueueIsEmpty() && (mode==LONG_LIST || mode==LIST || mode==RETRIEVE))
      {
	 state=FILE_RECV;
	 m=MOVED;
      }
      goto usual_return;
   case FILE_RECV:
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
notimeout_return:
   if(m==MOVED)
      return MOVED;
   return m;
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
   state=DISCONNECTED;
}

void Fish::Init()
{
   state=DISCONNECTED;
   send_buf=0;
   recv_buf=0;
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
   case(FILE_RECV):
   case(FILE_SEND):
   case(CONNECTING):
      Disconnect();
   }
   if(!RespQueueIsEmpty())
      Disconnect(); // play safe.
   state=(recv_buf?CONNECTED:DISCONNECTED);
   eof=false;
   super::Close();
}

void Fish::Send(const char *format,...)
{
   va_list va;
   va_start(va,format);
   char *str;
#ifdef HAVE_VSNPRINTF
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
#else // !HAVE_VSNPRINTF
   str=string_alloca(2048);
   vsprintf(str,format,va);
#endif
   DebugPrint("---> ",str,5);
   send_buf->Put(str);
   va_end(va);
}

const char *Fish::shell_encode(const char *string)
{
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

void Fish::SendMethod()
{
   const char *e=shell_encode(file);
   switch((open_mode)mode)
   {
   case CHANGE_DIR:
      Send("#CWD %s\n"
	   "cd %s; echo '### 000'\n",file,e);
      PushExpect(EXPECT_CWD);
      PushDirectory(file);
      break;
   case LONG_LIST:
      Send("#DIR %s\n"
	   "ls -l %s; echo '### 200'\n",file,file);
      PushExpect(EXPECT_DIR);
      real_pos=0;
      break;
   case RETRIEVE:
      Send("#RETR %s\n"
	   "ls -l %s | ( read a b c d x e; echo $x );"
	   "echo '### 100'; cat %s; echo '### 200'\n",file,e,e);
      PushExpect(EXPECT_RETR_SIZE);
      PushExpect(EXPECT_RETR);
      real_pos=0;
      break;
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
      return m;
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
   case EXPECT_RETR_SIZE:
      if(message && is_ascii_digit(message[0]))
      {
	 long long size_ll;
	 if(1==sscanf(message,"%lld",&size_ll))
	 {
	    entity_size=size_ll;
	    if(opt_size)
	       *opt_size=entity_size;
	 }
      }
      state=FILE_RECV;
      break;
   case EXPECT_RETR:
      eof=true;
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
	 return 0;
      }
      if(entity_size==NO_SIZE)
      {
	 // FIXME memstr(
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
}
int Fish::StoreStatus()
{
}

int Fish::Done()
{
   if(mode==CLOSED)
      return OK;
   if(Error())
      return error_code;
   if(eof)
      return OK;
   if(mode==CONNECT_VERIFY)
      return OK;
   return IN_PROGRESS;
}

const char *Fish::CurrentStatus()
{
   return "FIXME";
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

void Fish::Reconfig(const char *name)
{
}

void Fish::ClassInit()
{
   // register the class
   Register("fish",Fish::New);
}
FileAccess *Fish::New() { return new Fish(); }

#ifdef MODULE
CDECL void module_init()
{
   Fish::ClassInit();
}
#endif
