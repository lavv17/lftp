/*
 * lftp and utils
 *
 * Copyright (c) 1996-1997 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "FtpGlob.h"
#include "FtpListInfo.h"
#include "xmalloc.h"
#include "LsCache.h"
#include "misc.h"

void FtpGlob::Init(FileAccess *session,FA::open_mode n_mode)
{
   dir=0;
   mode=n_mode;
   f=session;
   inbuf=0;
   buf=0;
   flags=0;
   extra_slashes=0;
   from_cache=false;
   state=INITIAL;
   ptr=buf;
   use_long_list=true;
   dir_list=0;
   dir_index=0;
   updir_glob=0;
   if(n_mode!=FA::LIST)
   {
      NoLongList();
      NoChange();
   }
}

FtpGlob::FtpGlob(FileAccess *session,const char *n_pattern,FA::open_mode n_mode)
   : Glob(n_pattern)
{
   Init(session,n_mode);
   if(n_mode==FA::LIST)
      RestrictPath();
   dir=xstrdup(pattern);
   char *slash=strrchr(dir,'/');
   if(!slash)
      dir[0]=0;	  // current directory
   else if(slash>dir)
      *slash=0;	  // non-root directory
   else
      dir[1]=0;	  // root directory

   if(pattern[0] && !HasWildcards(pattern))
   {
      // no need to glob, just unquote
      char *u=alloca_strdup(pattern);
      UnquoteWildcards(u);
      add(u);
      state=DONE;
      return;
   }

#if 0
   if(dir[0] && mode==FA::LIST)
   {
      updir_glob=new FtpGlob(session,dir,mode);
   }
#endif
}

FtpGlob::~FtpGlob()
{
   if(f)
      f->Close();
   xfree(buf);
   xfree(dir);
}

int   FtpGlob::Do()
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
	 if(LsCache::Find(f,dir,mode,&buf,&inbuf))
	 {
	    from_cache=true;
	    ptr=buf;
	    use_long_list=false;
	 }
	 else if(mode==FA::LIST && use_long_list && (flags&RESTRICT_PATH)
	 && !(flags&NO_CHANGE)
	 && LsCache::Find(f,dir,FA::LONG_LIST,&buf,&inbuf))
	 {
	    from_cache=true;
	    ptr=buf;
	    NoChange();
	 }
      }
      if(!from_cache)
	 f->Open(dir,mode);
      state=GETTING_DATA;
      m=MOVED;
   }

   if(!from_cache)
   {
      char tmpbuf[0x1000];
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

	 LsCache::Add(f,dir,mode,buf,inbuf);

	 f=0;
	 state=DONE;
	 return MOVED;
      }
      int offs=ptr-buf;
      buf=(char*)xrealloc(buf,inbuf+res);
      ptr=buf+offs;
      memcpy(buf+inbuf,tmpbuf,res);
      inbuf+=res;
   }

   while((nl=(char*)memchr(ptr,'\n',inbuf-(ptr-buf))))
   {
      int len=nl-ptr;
      if(nl[-1]=='\r')
	 len--;

      if(!(flags&NO_CHANGE))
      {
	 // workaround for some ftp servers
	 if(ptr[0]=='.' && ptr[1]=='/')
	 {
	    ptr+=2;
	    len-=2;
	 }
	 else if(ptr[0]=='/' && ptr[1]=='/')
	 {
	    ptr++;
	    len--;
	 }
      }

      if(from_cache && use_long_list)
	 add_force(ptr,len);
      else
	 add(ptr,len);

      ptr=nl+1;
   }

   if(from_cache)
   {
      if(use_long_list && from_cache)
      {
	 // ok, now try to parse the long list.
	 int err=0;
	 FileSet *set=FtpListInfo::ParseFtpLongList(list,&err);
	 if(err>0) // ouch, there were errors. Revert to short list
	 {
	    if(set)
	       delete set;
	    free_list();
	    xfree(buf);
	    buf=ptr=0;
	    inbuf=0;

	    flags&=~NO_CHANGE;
	    use_long_list=false;
	    from_cache=false;
	    state=INITIAL;
	    return MOVED;
	 }
	 // all ok, transform the set
	 free_list();
	 set->ExcludeDots();
	 set->rewind();
	 for(FileInfo *info=set->curr(); info!=NULL; info=set->next())
	    add(dir_file(dir,info->name));
	 delete set;
      }
      state=DONE;
   }

   return MOVED;
}

const char *FtpGlob::Status()
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
