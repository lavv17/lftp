/*
 * lftp - file transfer program
 *
 * Copyright (c) 1999-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "FileAccess.h"
#include "FtpDirList.h"
#include "LsCache.h"
#include "ArgV.h"
#include "misc.h"

#include <sys/types.h>
#include <time.h>
#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#define super DirList

int FtpDirList::Do()
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

   while(len>0)
   {
      const char *eol=find_char(b,len,'\n');
      if(!eol && !ubuf->Eof() && len<0x1000)
	 return m;
      int line_len=len;
      char *eplf = NULL;
      if(eol)
      {
	 line_len=eol+1-b;

	 eplf = EPLF(b, eol-b);
      }

      if(eplf) {
	 /* put the new string instead */
	 buf->Put(eplf,strlen(eplf));
	 xfree(eplf);
      } else
         buf->Put(b,line_len);

      ubuf->Skip(line_len);
      len -= line_len;
      b += line_len;

      m=MOVED;
   }

   if(ubuf->Error())
   {
      SetError(ubuf->ErrorText());
      m=MOVED;
   }
   return m;
}

FtpDirList::FtpDirList(ArgV *a,FileAccess *fa)
   : DirList(a)
{
   session=fa;
   ubuf=0;
   pattern=args->Combine(1);
}

FtpDirList::~FtpDirList()
{
   Delete(ubuf);
   xfree(pattern);
}

const char *FtpDirList::Status()
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

void FtpDirList::Suspend()
{
   if(ubuf)
      ubuf->Suspend();
   super::Suspend();
}
void FtpDirList::Resume()
{
   super::Resume();
   if(ubuf)
      ubuf->Resume();
}

char *FtpDirList::EPLF(const char *b, int linelen)
{
   // check for EPLF listing
   if(linelen <= 1) return NULL;
   if(b[0]!='+')  return NULL;

   const char *scan=b+1;
   int scan_len=linelen-1;

   const char *name=0;
   int name_len=0;
   off_t size=NO_SIZE;
   time_t date=NO_DATE;
   bool dir=false;
   int perms=-1;
   long long size_ll;
   long date_l;
   while(scan && scan_len>0)
   {
      switch(*scan)
      {
      case '\t':  // the rest is file name.
	 name=scan+1;
	 name_len=scan_len-1;
	 scan=0;
	 break;
      case 's':
	 if(1 != sscanf(scan+1,"%lld",&size_ll))
	    break;
	 size = size_ll;
	 break;
      case 'm':
	 if(1 != sscanf(scan+1,"%ld",&date_l))
	    break;
	 date = date_l;
	 break;
      case '/':
	 dir=true;
	 break;
      case 'r':
	 dir=false;
	 break;
      case 'i':
	 break;
      case 'u':
	 if(scan[1]=='p')  // permissions.
	    sscanf(scan+2,"%o",&perms);
	 break;
      default:
	 name=0;
	 scan=0;
	 break;
      }
      if(scan==0 || scan_len==0)
	 break;
      const char *comma=find_char(scan,scan_len,',');
      if(comma)
      {
	 scan_len-=comma+1-scan;
	 scan=comma+1;
      }
      else
	 break;
   }
   if(!name || name_len == 0) return NULL;

   // ok, this is EPLF. Format new string.
   char *line_add=(char *) xmalloc(80+name_len);
   if(perms==-1)
      perms=(dir?0755:0644);
   char size_str[32];
   if(size==NO_SIZE)
      strcpy(size_str,"-");
   else
      sprintf(size_str,"%lld",(long long)size);
   char date_str[21];
   if(date == NO_DATE)
      strcpy(date_str,"-");
   else {
      struct tm *t=localtime(&date);
      sprintf(date_str, "%04d-%02d-%02d %02d:%02d",
	    t->tm_year+1900,t->tm_mon+1,t->tm_mday,
	    t->tm_hour,t->tm_min);
   }
   sprintf(line_add, "%c%s  %10s  %16s  %.*s",
	 dir ? 'd':'-', format_perms(perms), size_str,
	 date_str, name_len, name);

   return line_add;
}
