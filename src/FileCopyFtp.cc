/*
 * lftp and utils
 *
 * Copyright (c) 1998-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "FileCopyFtp.h"

#define super FileCopy

void FileCopyFtp::Close()
{
   ftp_src->Close();
   ftp_dst->Close();
}

int FileCopyFtp::Do()
{
   int src_res,dst_res;
   int m=super::Do();
   if(state!=DO_COPY || put->GetRealPos()==FILE_END || get->Eof())
      return m;

   if(ftp_src->IsClosed())
   {
      ((FileCopyPeerFA*)get)->OpenSession();
      ftp_src->SetCopyMode(Ftp::COPY_SOURCE,reverse_passive,src_retries);
      m=MOVED;
   }
   if(ftp_dst->IsClosed())
   {
      ((FileCopyPeerFA*)put)->OpenSession();
      ftp_dst->SetCopyMode(Ftp::COPY_DEST,!reverse_passive,dst_retries);
      m=MOVED;
   }
   // check for errors
   if(ftp_src->CopyFailed() || ftp_dst->CopyFailed())
   {
      if(ftp_src->RestartFailed() || ftp_dst->RestartFailed())
      {
	 get->CannotSeek(put->GetSize());
	 put->CannotSeek(put->GetSize());
      }
      src_retries=ftp_src->GetRetries();
      dst_retries=ftp_dst->GetRetries();
      Close();
      if(put->CanSeek())
	 put->Seek(FILE_END);
      else
	 put->Seek(0);
      return MOVED;
   }

   src_res=ftp_src->Done();
   dst_res=ftp_dst->Done();
   if(src_res==FA::OK && dst_res==FA::OK)
   {
      Close();
      get->PutEOF();
      return MOVED;
   }
   if(src_res!=FA::IN_PROGRESS && src_res!=FA::OK)
   {
      SetError(ftp_src->StrError(src_res));
      return MOVED;
   }
   if(dst_res!=FA::IN_PROGRESS && dst_res!=FA::OK)
   {
      SetError(ftp_dst->StrError(dst_res));
      return MOVED;
   }

   // exchange copy address
   if(ftp_dst->SetCopyAddress(ftp_src) || ftp_src->SetCopyAddress(ftp_dst))
      m=MOVED;

   long add=ftp_dst->GetPos()-put->GetRealPos();
   if(add>0)
      RateAdd(add);

   long pos=ftp_dst->GetPos();
   get->SetPos(pos);
   put->SetPos(pos);

   return m;
}

void FileCopyFtp::Init()
{
   ftp_src=ftp_dst=0;
   no_rest=false;
   reverse_passive=false;
   src_retries=dst_retries=0;
}

FileCopyFtp::~FileCopyFtp()
{
}

// s,d must be FileCopyPeerFA, s->GetSession(),d->GetSession() must be Ftp.
FileCopyFtp::FileCopyFtp(FileCopyPeer *s,FileCopyPeer *d,bool c,bool rp)
   : super(s,d,c)
{
   Init();
   reverse_passive=rp;
   // not pretty.
   ftp_src=(Ftp*)(s->GetSession());
   ftp_dst=(Ftp*)(d->GetSession());

   ((FileCopyPeerFA*)s)->SetFXP();
   ((FileCopyPeerFA*)d)->SetFXP();

   if(ftp_src->IsPassive() && !ftp_dst->IsPassive())
      reverse_passive=true;
   else if(!ftp_src->IsPassive() && ftp_dst->IsPassive())
      reverse_passive=false;
}

FileCopy *FileCopyFtp::New(FileCopyPeer *s,FileCopyPeer *d,bool c)
{
   FA *s_s=s->GetSession();
   FA *d_s=d->GetSession();
   if(!s_s || !d_s)
      return 0;
   if(strcmp(s_s->GetProto(),"ftp") || strcmp(d_s->GetProto(),"ftp"))
      return 0;
   if(!(bool)ResMgr::Query("ftp:use-fxp",s_s->GetHostName())
   || !(bool)ResMgr::Query("ftp:use-fxp",d_s->GetHostName()))
      return 0;
   return new FileCopyFtp(s,d,c,ResMgr::Query("ftp:fxp-passive-source",0));
}
