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

   switch(state)
   {
   case DISCONNECTED:
      if(mode==CLOSED)
	 return m;
      if(mode==CONNECT_VERIFY)
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
      filter_out=new OutputFilter("ssh gemini \"echo FISH:;/bin/sh\"",pipe_in[1]);   // FIXME
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
      send_buf=new FileOutputBuffer(filter_out);
      filter_out=0;
      recv_buf=new FileInputBuffer(new FDStream(pipe_in[0],"pipe in"));
      close(pipe_in[1]);
      pipe_in[1]=-1;
      state=CONNECTED;
      m=MOVED;

      Send("#FISH\n"
	   "echo; start_fish_server; echo '### 200'\n");
      expected_responses++;
   case CONNECTED:
      if(mode==CLOSED)
	 return m;
      SendMethod();

   }
   return m;
}

void Fish::Disconnect()
{
   Delete(send_buf);
   Delete(recv_buf);
   if(pipe_in[0]!=-1)
      close(pipe_in[0]);
   if(pipe_in[1]!=-1)
      close(pipe_in[1]);
   if(filter_out)
      delete filter_out;
   expected_responses=0;
}

void Fish::Init()
{
   send_buf=0;
   recv_buf=0;
   pipe_in[0]=pipe_in[1]=-1;
   filter_out=0;
   expected_responses=0;
   max_send=0;
}

Fish::Fish()
{
   Init();
   Reconfig(0);
}

Fish::~Fish()
{
   Disconnect();
}

void Fish::Close()
{
   switch(state)
   {
   case(DISCONNECTED):
   case(WAITING):
   case(CONNECTED):
      break;
   case(FILE_RECV):
   case(FILE_SEND):
   case(CONNECTING):
      Disconnect();
   }
   if(expected_responses>0)
      Disconnect(); // play safe.
}

void Fish::Send(const char *format,...)
{
   char *str=(char*)alloca(max_send);
   va_list va;
   va_start(va,format);

   vsprintf(str,format,va);

   va_end(va);

   DebugPrint("---> ",str,5);
   send_buf->Put(str);
}

void Fish::SendMethod()
{
}

void Fish::ClassInit()
{
   // register the class
   Register("fish",Fish::New);
}
FileAccess *Fish::New() { return new Fish(); }
