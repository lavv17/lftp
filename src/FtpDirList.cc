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
#include "DirColors.h"

#include <sys/types.h>
#include <time.h>
#ifdef TM_IN_SYS_TIME
# include <sys/time.h>
#endif

#define super DirList

int FtpDirList::Do()
{
   int m=STALL;

   if(done)
      return m;

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

   while(len>0)
   {
      const char *eol=find_char(b,len,'\n');
      if(!eol && !ubuf->Eof() && len<0x1000)
	 return m;
      if(eol)
      {
	 int line_len=eol+1-b;
	 if(!TryEPLF(b, eol-b) && !TryColor(b, eol-b))
	    buf->Put(b,line_len);
	 ubuf->Skip(line_len);
      }
      else
      {
	 // too long line of missing \n on last line.
	 buf->Put(b,len);
	 ubuf->Skip(len);
      }
      ubuf->Get(&b,&len);
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

bool FtpDirList::TryEPLF(const char *b, int linelen)
{
   // check for EPLF listing
   if(linelen<2)
      return false;
   if(b[0]!='+')
      return false;

   const char *scan=b+1;
   int scan_len=linelen-1;

   char *name=0;
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
	 if(scan_len<2)
	    return false;
	 name=string_alloca(scan_len);
	 strncpy(name,scan+1,scan_len-1);
	 name[scan_len-1]=0;
	 if(scan_len>2 && name[scan_len-2]=='\r')
	    name[scan_len-2]=0;
	 if(name[0]==0)
	    return false;
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
   if(!name)
      return false;

   // ok, this is EPLF. Format new string.
   if(perms==-1)
      perms=(dir?0755:0644);
   char size_str[32];
   if(size==NO_SIZE)
      strcpy(size_str,"-");
   else
      sprintf(size_str,"%lld",(long long)size);
   const char *date_str="-";
   if(date!=NO_DATE)
      date_str=TimeDate(date).IsoDateTime();

   buf->Format("%c%s  %10s  %16s  ",
	 dir ? 'd':'-', format_perms(perms), size_str, date_str);

   if(color)
      DirColors::GetInstance()->
	 PutColored(buf,name,dir?FileInfo::DIRECTORY:FileInfo::NORMAL);
   else
      buf->Put(name);

   buf->Put("\r\n");
   return true;
}

bool FtpDirList::TryColor(const char *line_c,int len)
{
   if(!color)
      return false;

   char *line=string_alloca(len+1);
   strncpy(line,line_c,len);
   line[len]=0;
   if(len>0 && line[len-1]=='\r')
      line[len-1]=0;

   char year_or_time[6];
   char perms[12],user[32],group[32],month_name[4];
   int nlink,day,year,hour,minute;
   long long size;
   int consumed=0;

   int n=sscanf(line,"%11s %d %31s %31s %lld %3s %2d %5s%n",perms,&nlink,
	       user,group,&size,month_name,&day,year_or_time,&consumed);
   if(n==4) // bsd-like listing without group?
   {
      group[0]=0;
      n=sscanf(line,"%11s %d %31s %lld %3s %2d %5s%n",perms,&nlink,
	    user,&size,month_name,&day,year_or_time,&consumed);
   }
   if(consumed>0 && -1!=(parse_perms(perms+1))
   && -1!=(parse_month(month_name))
   && -1!=parse_year_or_time(year_or_time,&year,&hour,&minute)
   && strlen(line+consumed)>1);
   {
      // good.
      int type=-1;
      int name_start=consumed+1;
      int name_len=strlen(line+name_start);
      if(perms[0]=='d')
	 type=FileInfo::DIRECTORY;
      else if(perms[0]=='l')
      {
	 type=FileInfo::SYMLINK;
	 const char *str=strstr(line+name_start+1," -> ");
	 if(str)
	    name_len=str-(line+name_start);
      }
      else if(perms[0]=='-')
	 type=FileInfo::NORMAL;
      buf->Put(line,consumed+1);
      char *name=string_alloca(name_len+1);
      strncpy(name,line+name_start,name_len);
      name[name_len]=0;
      DirColors::GetInstance()->PutColored(buf,name,type);
      buf->Put(line+name_start+name_len);
      buf->Put("\r\n");
      return true;
   }
   return false;
}
