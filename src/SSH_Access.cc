/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2011 by Alexander V. Lukyanov (lav@yars.free.net)
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

/* $Id: SSH_Access.cc,v 1.3 2008/11/27 05:56:28 lav Exp $ */

#include <config.h>
#include "SSH_Access.h"
#include "misc.h"

void SSH_Access::MakePtyBuffers()
{
   int fd=ssh->getfd();
   if(fd==-1)
      return;
   ssh->Kill(SIGCONT);
   send_buf=new IOBufferFDStream(new FDStream(ssh->getfd_pipe_out(),"pipe-out"),IOBuffer::PUT);
   recv_buf=new IOBufferFDStream(new FDStream(ssh->getfd_pipe_in(),"pipe-in"),IOBuffer::GET);
   pty_send_buf=new IOBufferFDStream(ssh.borrow(),IOBuffer::PUT);
   pty_recv_buf=new IOBufferFDStream(new FDStream(fd,"pseudo-tty"),IOBuffer::GET);
}

int SSH_Access::HandleSSHMessage()
{
   int m=STALL;
   const char *b;
   int s;
   pty_recv_buf->Get(&b,&s);
   const char *eol=find_char(b,s,'\n');
   if(!eol)
   {
      const char *p="password:";
      const char *y="(yes/no)?";
      int p_len=strlen(p);
      int y_len=strlen(y);
      if(s>0 && b[s-1]==' ')
	 s--;
      if((s>=p_len && !strncasecmp(b+s-p_len,p,p_len))
      || (s>10 && !strncmp(b+s-2,"':",2)))
      {
	 if(!pass)
	 {
	    SetError(LOGIN_FAILED,_("Password required"));
	    return MOVED;
	 }
	 if(password_sent>0)
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
      if(!received_greeting && recv_buf->Size()>0)
      {
	 recv_buf->Get(&b,&s);
	 eol=find_char(b,s,'\n');
	 if(eol)
	 {
	    xstring &line=xstring::get_tmp(b,eol-b);
	    if(line.eq(greeting))
	       received_greeting=true;
	    LogRecv(4,line);
	    recv_buf->Skip(eol-b+1);
	 }
      }
      LogSSHMessage();
      return m;
   }
   const char *f=N_("Host key verification failed");
   if(!strncasecmp(b,f,strlen(f)))
   {
      LogSSHMessage();
      SetError(FATAL,_(f));
      return MOVED;
   }
   LogSSHMessage();
   return MOVED;
}

void SSH_Access::LogSSHMessage()
{
   const char *b;
   int s;
   pty_recv_buf->Get(&b,&s);
   const char *eol=find_char(b,s,'\n');
   if(!eol)
   {
      if(pty_recv_buf->Eof())
      {
	 if(s>0)
	    LogRecv(4,b);
	 LogError(0,_("Peer closed connection"));
      }
      if(pty_recv_buf->Error())
	 LogError(0,"pty read: %s",pty_recv_buf->ErrorText());
      if(pty_recv_buf->Eof() || pty_recv_buf->Error())
	 Disconnect();
      return;
   }
   s=eol-b+1;
   xstring &line=xstring::get_tmp(b,s-1);
   pty_recv_buf->Skip(s);
   LogRecv(4,line);

   if(!received_greeting && line.eq(greeting))
      received_greeting=true;
}

void SSH_Access::Disconnect()
{
   if(send_buf)
      LogNote(9,_("Disconnecting"));
   send_buf=0;
   recv_buf=0;
   pty_send_buf=0;
   pty_recv_buf=0;
   ssh=0;
   received_greeting=false;
   password_sent=0;
}

void SSH_Access::MoveConnectionHere(SSH_Access *o)
{
   send_buf=o->send_buf.borrow();
   recv_buf=o->recv_buf.borrow();
   pty_send_buf=o->pty_send_buf.borrow();
   pty_recv_buf=o->pty_recv_buf.borrow();
}
