/*
 * lftp and utils
 *
 * Copyright (c) 1998 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <getopt.h>

#include "FtpCopy.h"
#include "url.h"
#include "misc.h"
#include "GetPass.h"

void FtpCopy::Close()
{
   src->Close();
   dst->Close();
}

int FtpCopy::Do()
{
   int src_res,dst_res;
   int m=STALL;
   switch(state)
   {
   case INIT:
      if(!cont)
	 goto pre_WAIT;

   pre_GET_SIZE:
      dst_size=0;
      if(no_rest)
	 goto pre_WAIT;
      info.file=dst_file;
      info.get_size=true;
      info.get_time=false;
      dst->GetInfoArray(&info,1);
      state=GET_SIZE;
      m=MOVED;
   case GET_SIZE:
      dst_res=dst->Done();
      if(dst_res==Ftp::IN_PROGRESS)
      	 return m;
      if(dst_res<0)
	 dst_size=0;
      else
	 dst_size=(info.size<0?0:info.size);
      dst->Close();

   pre_WAIT:
      // setup source
      src->Open(src_file,src->RETRIEVE,dst_size);
      src->copy_mode=src->COPY_SOURCE;
      src->copy_passive=reverse_passive;
      // setup dest
      dst->Open(dst_file,dst->STORE,dst_size);
      dst->copy_mode=dst->COPY_DEST;
      dst->copy_passive=!reverse_passive;
      state=WAIT;
      m=MOVED;
   case WAIT:
      // check for errors
      if(src->state==src->COPY_FAILED
      || dst->state==dst->COPY_FAILED)
      {
	 if((src->flags&src->NOREST_MODE)
	 || (dst->flags&dst->NOREST_MODE))
	    no_rest=true;
	 Close();
	 goto pre_GET_SIZE;
      }

      src_res=src->Done();
      dst_res=dst->Done();
      if(src_res==src->OK && dst_res==dst->OK)
	 goto pre_DONE;
      if(src_res!=src->IN_PROGRESS && src_res!=src->OK)
      {
	 eprintf("%s: %s: %s\n",op,src_url,src->StrError(src_res));
	 goto pre_ERROR;
      }
      if(dst_res!=dst->IN_PROGRESS && dst_res!=dst->OK)
      {
	 eprintf("%s: %s: %s\n",op,dst_url,dst->StrError(dst_res));
	 goto pre_ERROR;
      }


      // exchange copy address
      if(src->copy_addr_valid && !dst->copy_addr_valid)
      {
	 memcpy(&dst->copy_addr,&src->copy_addr,sizeof(src->copy_addr));
	 dst->copy_addr_valid=true;
	 m=MOVED;
      }
      else if(!src->copy_addr_valid && dst->copy_addr_valid)
      {
	 memcpy(&src->copy_addr,&dst->copy_addr,sizeof(src->copy_addr));
	 src->copy_addr_valid=true;
	 m=MOVED;
      }
      else
	 block+=TimeOut(1000); // for the rolling stick :)
      return m;

   pre_ERROR:
      Close();
      state=ERROR;
      m=MOVED;
   case ERROR:
      return m;

   pre_DONE:
      Close();
      state=DONE;
      m=MOVED;
   case DONE:
      return m;
   }
   return m;
}

void FtpCopy::Init()
{
   src=dst=0;
   src_file=dst_file=0;
   state=INIT;
   cont=false;
   dst_size=0;
   no_rest=false;
   reverse_passive=false;
}

FtpCopy::~FtpCopy()
{
   delete args;
   xfree(src_file);
   xfree(dst_file);
   if(src) SessionPool::Reuse(src);
   if(dst) SessionPool::Reuse(dst);
}

int FtpCopy::ProcessURL(const char *url,Ftp **session,char **file,FileAccess *def)
{
   ParsedURL pu(url);

   if(pu.proto && pu.path)
   {
      if(strcmp(pu.proto,"ftp"))
      {
      no_ftp_proto:
	 eprintf(_("Sorry, %s can work with only ftp protocol\n"),op);
	 return -1;
      }
      FileAccess *new_session;
      new_session=FileAccess::New(pu.proto);
      if(!new_session)
      {
	 eprintf(_("%s: %s - not supported protocol\n"),
		  op,pu.proto);
	 return -1;
      }
      char *pass=pu.pass;
      if(pu.user && !pass)
      {
	 char *prompt=(char*)alloca(strlen(url)+80);
	 sprintf(prompt,"Password for %s: ",url);
	 bool old_running=running;
	 running=true;	// to prevent re-entering
	 pass=GetPass(prompt);
	 running=old_running;
      }
      if(pu.user && pass)
	 new_session->Login(pu.user,pass);
      if(pu.host)
	 new_session->Connect(pu.host,pu.port);
      *session=(Ftp*)new_session; // we are sure this is Ftp
      *file=xstrdup(pu.path);
   }
   else
   {
      if(strcmp(def->GetProto(),"ftp"))
	 goto no_ftp_proto;
      *session=(Ftp*)def->Clone(); // we are sure this is Ftp
      *file=xstrdup(url);
   }
   return 0;
}

FtpCopy::FtpCopy(ArgV *a,FileAccess *def)
{
   Init();

   args=a;
   op=args->a0();

   int opt;
   while((opt=args->getopt("+cp"))!=EOF)
   {
      switch(opt)
      {
      case('c'):
	 cont=true;
	 break;
      case('p'):
	 reverse_passive=true;
	 break;
      case('?'):
      err:
// 	 eprintf(_("Try `help %s' for more information.\n"),op);
	 eprintf(_("Usage: %s [-c] [-p] <source> <dest>\n"),op);
	 state=ERROR;
	 return;
      }
   }
   args->back();

   src_url=args->getnext();
   dst_url=args->getnext();
   if(!src_url || !dst_url)
      goto err;

   if(ProcessURL(src_url,&src,&src_file,def)==-1
   || ProcessURL(dst_url,&dst,&dst_file,def)==-1)
   {
      state=ERROR;
      return;
   }

   int dst_len=strlen(dst_file);
   if(dst_len>1 && dst_file[dst_len-1]=='/')
   {
      char *b=basename_ptr(src_file);
      dst_file=(char*)xrealloc(dst_file,dst_len+strlen(b)+1);
      strcat(dst_file,b);
   }
}

int FtpCopy::Done()
{
   if(state==DONE || state==ERROR)
      return true;
   return false;
}

int FtpCopy::ExitCode()
{
   return state!=DONE;
}

void FtpCopy::ShowRunStatus(StatusLine *sl)
{
   if(state==GET_SIZE)
      sl->Show(_("Getting size of `%s' [%s]"),dst_url,dst->CurrentStatus());
   else if(state==WAIT)
      sl->Show(_("Copying of `%s' in progress (%c)"),src_url,
	    "|/-\\"[time(0)%4]);
   else
      sl->Clear();
}
