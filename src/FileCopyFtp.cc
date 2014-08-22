/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "FileCopyFtp.h"
#include "log.h"

#define super FileCopy

#if !USE_SSL
#define protect false
#define passive_ssl_connect true
#endif

#define ftp_src get->GetSession().Cast<Ftp>()
#define ftp_dst put->GetSession().Cast<Ftp>()

void FileCopyFtp::Close()
{
   ftp_src->Close();
   ftp_dst->Close();
}

int FileCopyFtp::Do()
{
   int src_res,dst_res;
   int m=super::Do();

   if(disable_fxp || Error() || !put || !get)
      return m;

   if(state==PUT_WAIT && put->GetSeekPos()!=FILE_END)
   {
      if(get->GetSize()>=0 && put->GetSeekPos()>=get->GetSize())
	 return m;
      if(ftp_dst->IsClosed())
      {
	 put->OpenSession();
	 ftp_dst->SetCopyMode(Ftp::COPY_DEST,!passive_source,protect,
	       passive_source^passive_ssl_connect,dst_retries,dst_try_time);
	 m=MOVED;
      }
   }

   if(state!=DO_COPY || put->GetSeekPos()==FILE_END || get->Eof())
      return m;

   // FileCopy can suspend peers.
   get->Resume();
   put->Resume();

   if(ftp_src->IsClosed())
   {
      get->OpenSession();
      ftp_src->SetCopyMode(Ftp::COPY_SOURCE,passive_source,protect,
	    !(passive_source^passive_ssl_connect),src_retries,src_try_time);
      m=MOVED;
   }
   if(ftp_dst->IsClosed())
   {
      put->OpenSession();
      ftp_dst->SetCopyMode(Ftp::COPY_DEST,!passive_source,protect,
	    passive_source^passive_ssl_connect,dst_retries,dst_try_time);
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
      else if(passive_source==orig_passive_source)
      {
	 passive_source=!passive_source;
	 Log::global->Write(0,_("**** FXP: trying to reverse ftp:fxp-passive-source\n"));
      }
#if USE_SSL
      else if(passive_ssl_connect==orig_passive_ssl_connect)
      {
	 passive_ssl_connect=!passive_ssl_connect;
	 passive_source=orig_passive_source;
	 Log::global->Write(0,_("**** FXP: trying to reverse ftp:fxp-passive-sscn\n"));
      }
      else if(protect
      && !ResMgr::QueryBool("ftp:ssl-force",ftp_src->GetHostName())
      && !ResMgr::QueryBool("ftp:ssl-force",ftp_dst->GetHostName()))
      {
	 passive_source=orig_passive_source;
	 protect=false;
	 Log::global->Write(0,_("**** FXP: trying to reverse ftp:ssl-protect-fxp\n"));
      }
#endif // USE_SSL
      else
      {
	 // both ways failed. Fall back to normal copying.
	 Log::global->Write(0,_("**** FXP: giving up, reverting to plain copy\n"));
	 Close();
	 disable_fxp=true;
	 get->SetFXP(false);
	 put->SetFXP(false);

	 if(ResMgr::QueryBool("ftp:fxp-force",ftp_src->GetHostName())
	 || ResMgr::QueryBool("ftp:fxp-force",ftp_dst->GetHostName()))
	 {
	    SetError(_("ftp:fxp-force is set but FXP is not available"));
	    return MOVED;
	 }

	 off_t pos=put->GetRealPos();
	 if(!get->CanSeek(pos) || !put->CanSeek(pos))
	    pos=0;
	 get->Seek(pos);
	 put->Seek(pos);
	 RateReset();
	 return MOVED;
      }
      RateReset();

      src_retries=ftp_src->GetRetries();
      dst_retries=ftp_dst->GetRetries();
      src_try_time=ftp_src->GetTryTime();
      dst_try_time=ftp_dst->GetTryTime();
      Close();
      if(put->CanSeek())
      {
	 put->Seek(FILE_END);
	 get->Suspend();
	 put->Resume();
	 state=PUT_WAIT;
      }
      else
	 put->Seek(0);
      return MOVED;
   }

   src_res=ftp_src->Done();
   dst_res=ftp_dst->Done();
   if(src_res==FA::OK && dst_res==FA::OK)
   {
      Close();
      const long long size=GetSize();
      if(size>=0)
      {
	 get->SetPos(size);
	 put->SetPos(size);
      }
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

   if(!ftp_dst->CopyStoreAllowed()
   && ftp_src->CopyIsReadyForStore() && ftp_dst->CopyIsReadyForStore())
   {
      ftp_dst->CopyAllowStore();
      m=MOVED;
      RateReset();
   }

   // check for timeout when one session is done, and the other is stuck
   if(dst_res==FA::OK && src_res==FA::IN_PROGRESS)
      ftp_src->CopyCheckTimeout(ftp_dst);
   if(src_res==FA::OK && dst_res==FA::IN_PROGRESS)
      ftp_dst->CopyCheckTimeout(ftp_src);

   off_t add=ftp_dst->GetPos()-put->GetRealPos();
   if(add>0)
   {
      RateAdd(add);
      bytes_count+=add;
   }

   off_t pos=ftp_dst->GetPos();
   get->SetPos(pos);
   put->SetPos(pos);

   return m;
}

void FileCopyFtp::Init()
{
   no_rest=false;
   orig_passive_source=passive_source=false;
   src_retries=dst_retries=0;
   src_try_time=dst_try_time=0;
   disable_fxp=false;
#if USE_SSL
   protect=false;
   orig_passive_ssl_connect=passive_ssl_connect=true;
#endif
}

// s,d must be FileCopyPeerFA, s->GetSession(),d->GetSession() must be Ftp.
FileCopyFtp::FileCopyFtp(FileCopyPeer *s,FileCopyPeer *d,bool c,bool rp)
   : super(s,d,c)
{
   Init();
   passive_source=rp;

   get->SetFXP(true);
   put->SetFXP(true);

   if(ftp_src->IsPassive() && !ftp_dst->IsPassive())
      passive_source=true;
   else if(!ftp_src->IsPassive() && ftp_dst->IsPassive())
      passive_source=false;
   orig_passive_source=passive_source;

#if USE_SSL
   if(ResMgr::QueryBool("ftp:ssl-protect-fxp",ftp_src->GetHostName())
   || ResMgr::QueryBool("ftp:ssl-protect-fxp",ftp_dst->GetHostName()))
      protect=true;
   passive_ssl_connect=ResMgr::QueryBool("ftp:fxp-passive-sscn",0);
   orig_passive_ssl_connect=passive_ssl_connect;
#endif
}

FileCopy *FileCopyFtp::New(FileCopyPeer *s,FileCopyPeer *d,bool c)
{
   const FileAccessRef& s_s=s->GetSession();
   const FileAccessRef& d_s=d->GetSession();
   if(!s_s || !d_s)
      return 0;
   if((strcmp(s_s->GetProto(),"ftp") && strcmp(s_s->GetProto(),"ftps"))
   || (strcmp(d_s->GetProto(),"ftp") && strcmp(d_s->GetProto(),"ftps")))
      return 0;
   if(!ResMgr::QueryBool("ftp:use-fxp",s_s->GetHostName())
   || !ResMgr::QueryBool("ftp:use-fxp",d_s->GetHostName()))
      return 0;
   return new FileCopyFtp(s,d,c,ResMgr::QueryBool("ftp:fxp-passive-source",0));
}
