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
#include "FtpListInfo.h"
#include "FileSet.h"
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "xstring.h"
#include <ctype.h>
#include "xalloca.h"
#include "FtpSplitList.h"
#include "misc.h"

#define need_size (need&FileInfo::SIZE)
#define need_time (need&FileInfo::DATE)

FtpListInfo::FtpListInfo(Ftp *s)
{
   session=s;
   get_info=0;
   get_info_cnt=0;
   slist=0;
   state=INITIAL;
}

FtpListInfo::~FtpListInfo()
{
   Delete(slist);
   session->Close();
   xfree(get_info);
}

int FtpListInfo::Do()
{
   Ftp::fileinfo *cur;
   FileInfo *file;
   int res;
   int m=STALL;
   char	**slist_res;
   int err;

   if(done)
      state=DONE;

   switch(state)
   {
   case(INITIAL):
      slist=new FtpSplitList(session,session->LONG_LIST);
      slist->UseCache(use_cache);
      state=GETTING_LONG_LIST;
      m=MOVED;

   case(GETTING_LONG_LIST):
      if(!slist->Done())
	 return m;
      if(slist->Error())
      {
	 SetError(slist->ErrorText());
      	 Delete(slist);
	 slist=0;
	 return MOVED;
      }
      slist_res=slist->GetResult();

      // don't consider empty list to be valid
      if(slist_res && slist_res[0])
	 result=ParseFtpLongList(slist_res,&err);
      else
	 err=1;

      if(!result)
	 result=new FileSet;

      Delete(slist);
      slist=0;
      slist_res=0; // note: slist_res is pointer to part of slist

      if(err==0)
	 goto pre_GETTING_INFO;

      // there were parse errors, try another method
      slist=new FtpSplitList(session,session->LIST);
      slist->UseCache(use_cache);
      state=GETTING_SHORT_LIST;
      m=MOVED;

   case(GETTING_SHORT_LIST):
      if(!slist->Done())
	 return m;
      if(slist->Error())
      {
	 SetError(slist->ErrorText());
      	 Delete(slist);
	 slist=0;
	 return MOVED;
      }
      slist_res=slist->GetResult();
      result->Merge(slist_res);
      Delete(slist);
      slist=0;
      slist_res=0;

   pre_GETTING_INFO:
      result->ExcludeDots();
      if(rxc_exclude || rxc_include)
	 result->Exclude(path,rxc_exclude,rxc_include);

      state=GETTING_INFO;
      m=MOVED;

      get_info_cnt=result->get_fnum();
      if(get_info_cnt==0)
	 goto info_done;

      get_info=(Ftp::fileinfo*)xmalloc(sizeof(*get_info)*get_info_cnt);
      cur=get_info;

      get_info_cnt=0;
      result->rewind();
      for(file=result->curr(); file!=0; file=result->next())
      {
	 cur->get_size = !(file->defined & file->SIZE) && need_size;
	 cur->get_time = !(file->defined & file->DATE) && need_time;

	 if(file->defined & file->TYPE)
	 {
	    if(file->filetype==file->SYMLINK && follow_symlinks)
	    {
	       file->filetype=file->NORMAL;
	       file->defined &= ~(file->SIZE|file->SYMLINK_DEF|file->MODE|file->DATE_UNPREC);
	       cur->get_size=true;
	       cur->get_time=true;
	    }

	    if(file->filetype==file->SYMLINK)
	    {
	       // don't need these for symlinks
	       cur->get_size=false;
	       cur->get_time=false;
	    }
	    else if(file->filetype==file->DIRECTORY)
	    {
	       // don't need size for directories
	       cur->get_size=false;
	    }
	 }

	 if(cur->get_size || cur->get_time)
	 {
	    cur->file=file->name;
	    if(!cur->get_size)
	       cur->size=-1;
	    if(!cur->get_time)
	       cur->time=(time_t)-1;
	    cur++;
	    get_info_cnt++;
	 }
      }
      if(get_info_cnt==0)
	 goto info_done;
      session->GetInfoArray(get_info,get_info_cnt);
      state=GETTING_INFO;
      m=MOVED;
   case(GETTING_INFO):
      res=session->Done();
      if(res==Ftp::DO_AGAIN)
	 return m;
      if(res==Ftp::IN_PROGRESS)
	 return m;
      assert(res==Ftp::OK);
      session->Close();

      for(cur=get_info; get_info_cnt-->0; cur++)
      {
	 if(cur->time!=(time_t)-1)
	    result->SetDate(cur->file,cur->time);
	 if(cur->size!=-1)
	    result->SetSize(cur->file,cur->size);
      }

   info_done:
      xfree(get_info);
      get_info=0;

      done=true;
      state=DONE;
      m=MOVED;

   case(DONE):
      return m;
   }
   // can't happen
   abort();
}

const char *FtpListInfo::Status()
{
   static char s[256];
   const char *status;
   switch(state)
   {
   case(DONE):
   case(INITIAL):
      return "";
   case(GETTING_LONG_LIST):
   case(GETTING_SHORT_LIST):
      if(!slist)
	 return "";
      sprintf(s,_("Getting directory contents (%lld)"),
		     (long long)session->GetPos());
      if(slist->RateValid())
	 sprintf(s+strlen(s)," %s",slist->GetRateStr());
      status=session->CurrentStatus();
      if(status && status[0])
	 sprintf(s+strlen(s)," [%s]",status);
      return s;
   case(GETTING_INFO):
      // xgettext:c-format
      sprintf(s,_("Getting files information (%d%%)"),session->InfoArrayPercentDone());
      status=session->CurrentStatus();
      if(status && status[0])
	 sprintf(s+strlen(s)," [%s]",status);
      return s;
   }
   // cat't happen
   abort();
}


/*
total 123
-rwxr-xr-x   1 lav      root         4771 Sep 12  1996 install-sh
-rw-r--r--   1 lav      root         1349 Feb  2 14:10 lftp.lsm
drwxr-xr-x   4 lav      root         1024 Feb 22 15:32 lib
lrwxrwxrwx   1 lav      root           33 Feb 14 17:45 ltconfig -> /usr/share/libtool/ltconfig
NOTE: group may be missing.
*/
static
FileInfo *ParseFtpLongList_UNIX(const char *line_c,int *err)
{
#define FIRST_TOKEN strtok(line," \t")
#define NEXT_TOKEN  strtok(NULL," \t")
#define ERR do{(*err)++;return(0);}while(0)
   char	 *line=alloca_strdup(line_c);
   int 	 len=strlen(line);
   int	 tmp;

   if(len==0)
      return 0;
   if(sscanf(line,"total %d",&tmp)==1)
      return 0;

   /* parse perms */
   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;

   FileInfo fi;
   switch(t[0])
   {
   case('l'):  // symlink
      fi.SetType(fi.SYMLINK);
      break;
   case('d'):  // directory
      fi.SetType(fi.DIRECTORY);
      break;
   case('-'):  // plain file
      fi.SetType(fi.NORMAL);
      break;
   case('b'): // block
   case('c'): // char
   case('p'): // pipe
   case('s'): // sock
      return 0;  // ignore
   default:
      ERR;
   }
   mode_t mode=parse_perms(t+1);
   if(mode!=(mode_t)-1)
      fi.SetMode(mode);

   // link count
   t = NEXT_TOKEN;
   if(!t)
      ERR;

   // user
   t = NEXT_TOKEN;
   if(!t)
      ERR;

   // group or size
   char *group_or_size = NEXT_TOKEN;

   // size or month
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   if(isdigit(*t))
   {
      // size
      fi.SetSize(atol(t));
      t = NEXT_TOKEN;
      if(!t)
	 ERR;
   }
   else
   {
      // it was month
      fi.SetSize(atol(group_or_size));
   }

   struct tm date;
   memset(&date,0,sizeof(date));

   date.tm_mon=parse_month(t);
   if(date.tm_mon==-1)
      date.tm_mon=0;

   const char *day_of_month = NEXT_TOKEN;
   if(!day_of_month)
      ERR;
   date.tm_mday=atoi(day_of_month);

   bool year_anomaly=false;

   // time or year
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   date.tm_hour=date.tm_min=0;
   if(strlen(t)==5)
   {
      sscanf(t,"%2d:%2d",&date.tm_hour,&date.tm_min);
      time_t curr=time(0);
      struct tm &now=*localtime(&curr);
      date.tm_year=now.tm_year;
      if(date.tm_mon*64+date.tm_mday>now.tm_mon*64+now.tm_mday)
	 date.tm_year--;
   }
   else
   {
      if(day_of_month+strlen(day_of_month)+1 == t)
	 year_anomaly=true;
      date.tm_year=atoi(t)-1900;
   }

   date.tm_isdst=0;
   date.tm_sec=0;

   fi.SetDateUnprec(mktime(&date));

   char *name=strtok(NULL,"");
   if(!name)
      ERR;

   // there are ls which outputs extra space after year.
   if(year_anomaly && *name==' ')
      name++;

   if(fi.filetype==fi.SYMLINK)
   {
      char *arrow=name;
      while((arrow=strstr(arrow," -> "))!=0)
      {
	 if(arrow!=name && arrow[4]!=0)
	 {
	    *arrow=0;
	    fi.SetSymlink(arrow+4);
	    break;
	 }
	 arrow++;
      }
   }
   fi.SetName(name);

   return new FileInfo(fi);
}

/*
07-13-98  09:06PM       <DIR>          aix
07-13-98  09:06PM       <DIR>          hpux
07-13-98  09:06PM       <DIR>          linux
07-13-98  09:06PM       <DIR>          ncr
07-13-98  09:06PM       <DIR>          solaris
03-18-98  06:01AM              2109440 nlxb318e.tar
07-02-98  11:17AM                13844 Whatsnew.txt
*/
static
FileInfo *ParseFtpLongList_NT(const char *line_c,int *err)
{
   char	 *line=alloca_strdup(line_c);
   int 	 len=strlen(line);

   if(len==0)
      return 0;
   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;
   FileInfo fi;
   int month,day,year;
   if(sscanf(t,"%2d-%2d-%2d",&month,&day,&year)!=3)
      ERR;
   if(year>=70)
      year+=1900;
   else
      year+=2000;

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   int hour,minute;
   char am;
   if(sscanf(t,"%2d:%2d%c",&hour,&minute,&am)!=3)
      ERR;
   t = NEXT_TOKEN;
   if(t==0)
      ERR;

   if(am=='P') // PM - after noon
   {
      hour+=12;
      if(hour==24)
	 hour=0;
   }
   struct tm tms;
   tms.tm_sec=0;	   /* seconds after the minute [0, 61]  */
   tms.tm_min=minute;      /* minutes after the hour [0, 59] */
   tms.tm_hour=hour;	   /* hour since midnight [0, 23] */
   tms.tm_mday=day;	   /* day of the month [1, 31] */
   tms.tm_mon=month-1;     /* months since January [0, 11] */
   tms.tm_year=year-1900;  /* years since 1900 */
   tms.tm_isdst=0;
   fi.SetDateUnprec(mktime(&tms));

   long long size;
   if(!strcmp(t,"<DIR>"))
      fi.SetType(fi.DIRECTORY);
   else
   {
      fi.SetType(fi.NORMAL);
      if(sscanf(t,"%lld",&size)!=1)
	 ERR;
      fi.SetSize(size);
   }

   t=strtok(NULL,"");
   if(t==0)
      ERR;
   while(*t==' ')
      t++;
   if(*t==0)
      ERR;
   fi.SetName(t);

   return new FileInfo(fi);
}

/*
+i774.71425,m951188401,/,	users
+i774.49602,m917883130,r,s79126,	jgr_www2.exe

starts with +
comma separated
first character of field is type:
 i - ?
 m - modification time
 / - means directory
 r - means plain file
 s - size
 up - permissions in octal
 \t - file name follows.
*/
static
FileInfo *ParseFtpLongList_EPLF(const char *line,int *err)
{
   int len=strlen(line);
   const char *b=line;

   if(len<2 || b[0]!='+')
      ERR;

   const char *name=0;
   int name_len=0;
   off_t size=NO_SIZE;
   time_t date=NO_DATE;
   long date_l;
   long long size_ll;
   bool dir=false;
   bool type_known=false;
   int perms=-1;

   const char *scan=b+1;
   int scan_len=len-1;
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
	    type_known=true;
	    break;
	 case 'r':
	    dir=false;
	    type_known=true;
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
   if(name==0 && type_known)
      ERR;

   FileInfo *fi=new FileInfo();
   fi->SetName(name);
   if(size!=NO_SIZE)
      fi->SetSize(size);
   if(date!=NO_DATE)
      fi->SetDate(date);
   if(type_known)
   {
      if(dir)
	 fi->SetType(fi->DIRECTORY);
      else
	 fi->SetType(fi->NORMAL);
   }
   if(perms!=-1)
      fi->SetMode(perms);

   return fi;
}

/*
                 0          DIR  06-27-96  11:57  PROTOCOL
               169               11-29-94  09:20  SYSLEVEL.MPT
*/
static
FileInfo *ParseFtpLongList_OS2(const char *line_c,int *err)
{
   char	 *line=alloca_strdup(line_c);
   int 	 len=strlen(line);

   if(len==0)
      return 0;
   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;

   FileInfo fi;

   long long size;
   if(sscanf(t,"%lld",&size)!=1)
      ERR;
   fi.SetSize(size);

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   fi.SetType(fi.NORMAL);
   if(!strcmp(t,"DIR"))
   {
      fi.SetType(fi.DIRECTORY);
      t = NEXT_TOKEN;
      if(t==0)
	 ERR;
   }
   int month,day,year;
   if(sscanf(t,"%2d-%2d-%2d",&month,&day,&year)!=3)
      ERR;
   if(year>=70)
      year+=1900;
   else
      year+=2000;

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   int hour,minute;
   if(sscanf(t,"%2d:%2d",&hour,&minute)!=3)
      ERR;

   struct tm tms;
   tms.tm_sec=0;	   /* seconds after the minute [0, 61]  */
   tms.tm_min=minute;      /* minutes after the hour [0, 59] */
   tms.tm_hour=hour;	   /* hour since midnight [0, 23] */
   tms.tm_mday=day;	   /* day of the month [1, 31] */
   tms.tm_mon=month-1;     /* months since January [0, 11] */
   tms.tm_year=year-1900;  /* years since 1900 */
   tms.tm_isdst=0;
   fi.SetDateUnprec(mktime(&tms));

   t=strtok(NULL,"");
   if(t==0)
      ERR;
   while(*t==' ')
      t++;
   if(*t==0)
      ERR;
   fi.SetName(t);

   return new FileInfo(fi);
}

typedef FileInfo *(*ListParser)(const char *line,int *err);
static ListParser list_parsers[]={
   ParseFtpLongList_UNIX,
   ParseFtpLongList_NT,
   ParseFtpLongList_EPLF,
   ParseFtpLongList_OS2,
   0
};

FileSet *FtpListInfo::ParseFtpLongList(const char * const *lines_c,int *err_ret)
{
   if(lines_c==0)
      return new FileSet;

   FileSet *result;
   int err;
   FileSet *best_result=0;
   int best_err=0x10000000;

   for(ListParser *parser=list_parsers; *parser; parser++)
   {
      err=0;

      const char *line;
      const char * const *lines = lines_c;
      result=new FileSet;

      while((line=*lines++)!=0)
      {
	 FileInfo *fi=(*parser)(line,&err);
	 if(fi)
	 {
	    if(!strchr(fi->name,'/'))
	       result->Add(fi);
	 }
	 if(err>=best_err)
	    break;
      }

      if(err<best_err)
      {
	 if(best_result)
	    delete best_result;
	 best_result=result;
	 best_err=err;
	 result=0;
      }
      else
      {
	 delete result;
	 result=0;
      }
      if(best_err==0)
	 break;   // look no further
   }
   *err_ret=best_err;
   return best_result;
}
