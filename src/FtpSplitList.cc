/*
 * lftp and utils
 *
 * Copyright (c) 1996-1999 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <stdio.h>
#include "xstring.h"
#include <sys/types.h>
#include <errno.h>
#include <stdlib.h>

#include "xalloca.h"
#include "FileAccess.h"
#include "FtpSplitList.h"
#include "FtpListInfo.h"
#include "xmalloc.h"
#include "LsCache.h"
#include "misc.h"

void FtpSplitList::Init(FileAccess *session,FA::open_mode n_mode)
{
   mode=n_mode;
   f=session;
   inbuf=0;
   buf=0;
   from_cache=false;
   state=INITIAL;
   ptr=buf;
   rate=new Speedometer();
}

FtpSplitList::FtpSplitList(FileAccess *session,FA::open_mode n_mode)
   : Glob("")
{
   Init(session,n_mode);
}

FtpSplitList::~FtpSplitList()
{
   if(f)
      f->Close();
   xfree(buf);
   Delete(rate);
}

int   FtpSplitList::Do()
{
   int	 res;
   char	 *nl;
   int   m=STALL;

   if(state==DONE)
   {
      if(!done)
      {
	 done=true;
	 return MOVED;
      }
      return m;
   }

   if(state==INITIAL)
   {
      if(use_cache)
      {
	 const char *b;
	 if(LsCache::Find(f,"",mode,&b,&inbuf))
	 {
	    buf=(char*)xmemdup(b,inbuf);
	    from_cache=true;
	    ptr=buf;
	 }
      }
      if(!from_cache)
      {
	 f->Open("",mode);
	 f->RereadManual();
      }
      state=GETTING_DATA;
      m=MOVED;
   }

   if(!from_cache)
   {
      char tmpbuf[0x1000];
      if(f->GetRealPos()==0 && f->GetPos()>0)
      {
	 f->SeekReal();
	 inbuf=0;
	 ptr=buf;
	 free_list();
      }
      res=f->Read(tmpbuf,sizeof(tmpbuf));
      if(res==f->DO_AGAIN)
	 return m;
      if(res<0)
      {
	 SetError(f->StrError(res));
	 state=DONE;
	 return MOVED;
      }

      if(res==0)
      {
	 // EOF
	 f->Close();

	 LsCache::Add(f,"",mode,buf,inbuf);

	 f=0;
	 state=DONE;
	 return MOVED;
      }
      rate->Add(res);
      int offs=ptr-buf;
      buf=(char*)xrealloc(buf,inbuf+res);
      ptr=buf+offs;
      memcpy(buf+inbuf,tmpbuf,res);
      inbuf+=res;
   }

   for( ; 0!=(nl=(char*)memchr(ptr,'\n',inbuf-(ptr-buf))); ptr=nl+1 )
   {
      int len=nl-ptr;
      if(nl[-1]=='\r')
	 len--;

      if(mode==FA::LIST)
      {
	 // workaround for some ftp servers
	 if(ptr[0]=='.' && ptr[1]=='/')
	 {
	    ptr+=2;
	    len-=2;
	 }
	 if(ptr[0]=='/' && ptr[1]=='/')
	 {
	    ptr++;
	    len--;
	 }
      }
      add(ptr,len);
   }

   if(from_cache)
      state=DONE;

   return MOVED;
}

const char *FtpSplitList::Status()
{
   static char s[256];
   if(state==GETTING_DATA)
   {
      sprintf(s,_("Getting file list (%ld) [%s]"),
		     f->GetPos(),f->CurrentStatus());
      return s;
   }
   return "";
}
