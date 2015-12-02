/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "FileAccess.h"
#include "FtpDirList.h"
#include "LsCache.h"
#include "ArgV.h"
#include "misc.h"
#include "DirColors.h"
#include "ftpclass.h"

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
      int err;
      if(use_cache && FileAccess::cache->Find(session,pattern,FA::LONG_LIST,&err,
				    &cache_buffer,&cache_buffer_size))
      {
	 if(err)
	 {
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open(pattern,FA::LONG_LIST);
	 ubuf=new IOBufferFileAccess(session);
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
      }
   }

   const char *b;
   int len;
   ubuf->Get(&b,&len);
   if(b==0) // eof
   {
      buf->PutEOF();
      FileAccess::cache->Add(session,pattern,FA::LONG_LIST,FA::OK,ubuf);
      return MOVED;
   }

   while(len>0)
   {
      const char *eol=find_char(b,len,'\n');
      if(!eol && !ubuf->Eof() && len<0x1000)
	 break;
      if(eol)
      {
	 int line_len=eol+1-b;
	 if(!TryEPLF(b, eol-b)
	 && !TryMLSD(b, eol-b)
	 && !TryColor(b, eol-b))
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

const char *FtpDirList::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
   {
      return xstring::format(_("Getting file list (%lld) [%s]"),
		     (long long)session->GetPos(),session->CurrentStatus());
   }
   return "";
}

void FtpDirList::SuspendInternal()
{
   super::SuspendInternal();
   if(ubuf)
      ubuf->SuspendSlave();
}
void FtpDirList::ResumeInternal()
{
   if(ubuf)
      ubuf->ResumeSlave();
   super::ResumeInternal();
}

void FtpDirList::FormatGeneric(FileInfo *fi)
{
   bool dir=(fi->defined&fi->TYPE) && fi->filetype==fi->DIRECTORY;
   if(!(fi->defined&fi->MODE))
      fi->mode=(dir?0755:0644);
   char size_str[32];
   if(fi->defined&fi->SIZE)
      snprintf(size_str,sizeof(size_str),"%lld",(long long)fi->size);
   else
      strcpy(size_str,"-");
   const char *date_str="-";
   if(fi->defined&fi->DATE)
      date_str=TimeDate(fi->date).IsoDateTime();

   buf->Format("%c%s  %10s  %16s  ",
	 dir ? 'd':'-', format_perms(fi->mode), size_str, date_str);

   if(color)
      DirColors::GetInstance()->
	 PutColored(buf,fi->name,fi->filetype);
   else
      buf->Put(fi->name);

   buf->Put("\r\n");
   delete fi;
}

FileInfo *ParseFtpLongList_EPLF(char *line,int *err,const char *);

bool FtpDirList::TryEPLF(const char *line_c, int len)
{
   // check for EPLF listing
   if(len<2)
      return false;
   if(line_c[0]!='+')
      return false;

   char *line=string_alloca(len+1);
   strncpy(line,line_c,len);
   line[len]=0;

   int err=0;
   FileInfo *fi=ParseFtpLongList_EPLF(line,&err,0);
   if(!fi)
      return false;

   // ok, this is EPLF. Format new string.
   FormatGeneric(fi);
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
      if(n!=7)
	 return false;
   }
   else if(n!=8)
      return false;
   if(consumed>0 && -1!=(parse_perms(perms+1))
   && -1!=(parse_month(month_name))
   && -1!=parse_year_or_time(year_or_time,&year,&hour,&minute)
   && strlen(line+consumed)>1)
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

FileInfo *ParseFtpLongList_MLSD(char *line,int *err,const char *);

bool FtpDirList::TryMLSD(const char *line_c,int len)
{
   char *line=string_alloca(len+1);
   strncpy(line,line_c,len);
   line[len]=0;

   int err=0;
   FileInfo *fi=ParseFtpLongList_MLSD(line,&err,0);
   if(!fi)
      return false;

   FormatGeneric(fi);
   return true;
}
