/*
 * lftp and utils
 *
 * Copyright (c) 1998-2002 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "misc.h"
#include "ftpclass.h"
#include "ascii_ctype.h"

#define number_of_parsers 6

FileSet *FtpListInfo::Parse(const char *buf,int len)
{
   if(mode==FA::LONG_LIST)
   {
      if(len==0)
      {
	 mode=FA::LIST;
	 return 0;
      }
      int err;
      FileSet *set=session->ParseLongList(buf,len,&err);
      if(!set || err>0)
	 mode=FA::LIST;
      return set;
   }
   else
   {
      return ParseShortList(buf,len);
   }
}

FileSet *Ftp::ParseLongList(const char *buf,int len,int *err_ret)
{
   if(err_ret)
      *err_ret=0;

   int err[number_of_parsers];
   FileSet *set[number_of_parsers];
   int i;
   for(i=0; i<number_of_parsers; i++)
   {
      err[i]=0;
      set[i]=new FileSet;
   }

   char *line=0;
   int line_alloc=0;
   int line_len;

   FtpLineParser guessed_parser=0;
   FileSet **the_set=0;
   int *the_err=0;
   int *best_err1=&err[0];
   int *best_err2=&err[1];

   const char *tz=Query("timezone",hostname);

   for(;;)
   {
      char *nl=(char*)memchr(buf,'\n',len);
      if(!nl)
	 break;
      line_len=nl-buf;
      if(line_len>0 && buf[line_len-1]=='\r')
	 line_len--;
      if(line_len==0)
      {
	 len-=nl+1-buf;
	 buf=nl+1;
	 continue;
      }
      if(line_alloc<line_len+1)
	 line=string_alloca(line_alloc=line_len+128);
      memcpy(line,buf,line_len);
      line[line_len]=0;
      const char *buf_line=buf;

      len-=nl+1-buf;
      buf=nl+1;

      if(!guessed_parser)
      {
	 for(i=0; i<number_of_parsers; i++)
	 {
	    FileInfo *info=(*line_parsers[i])(line,&err[i],tz);
	    if(info && !strchr(info->name,'/'))
	       set[i]->Add(info);
	    else
	       delete info;

	    if(*best_err1>err[i])
	       best_err1=&err[i];
	    if(*best_err2>err[i] && best_err1!=&err[i])
	       best_err2=&err[i];
	    if(*best_err2 > (*best_err1+1)*16)
	    {
	       i=best_err1-err;
	       guessed_parser=line_parsers[i];
	       the_set=&set[i];
	       the_err=&err[i];
	       break;
	    }
	    if(*best_err1>16)
	       goto leave; // too many errors with best parser.
	    // Restore line contents. It could be clobbered by parser.
	    memcpy(line,buf_line,line_len);
	    line[line_len]=0;
	 }
      }
      else
      {
	 FileInfo *info=(*guessed_parser)(line,the_err,tz);
	 if(info && !strchr(info->name,'/'))
	    (*the_set)->Add(info);
	 else
	    delete info;
      }
   }
   if(!the_set)
   {
      i=best_err1-err;
      the_set=&set[i];
      the_err=&err[i];
   }
leave:
   for(i=0; i<number_of_parsers; i++)
      if(&set[i]!=the_set)
	 delete set[i];
   if(err_ret && the_err)
      *err_ret=*the_err;
   return the_set?*the_set:0;
}

FileSet *FtpListInfo::ParseShortList(const char *buf,int len)
{
   FileSet *set=new FileSet;
   char *line=0;
   int line_alloc=0;
   int line_len;
   for(;;)
   {
      // workaround for some ftp servers
      if(len>=2 && buf[0]=='.' && buf[1]=='/')
      {
	 buf+=2;
	 len-=2;
      }
#if 0 // not possible here
      if(len>=2 && buf[0]=='/' && buf[1]=='/')
      {
	 buf++;
	 len--;
      }
#endif

      char *nl=(char*)memchr(buf,'\n',len);
      if(!nl)
	 break;
      line_len=nl-buf;
      if(line_len>0 && buf[line_len-1]=='\r')
	 line_len--;
      if(line_len==0)
      {
	 len-=nl+1-buf;
	 buf=nl+1;
	 continue;
      }
      if(line_alloc<line_len+1)
	 line=string_alloca(line_alloc=line_len+128);
      memcpy(line,buf,line_len);
      line[line_len]=0;

      len-=nl+1-buf;
      buf=nl+1;

      if(!strchr(line,'/'))
	 set->Add(new FileInfo(line));
   }
   return set;
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
FileInfo *ParseFtpLongList_UNIX(char *line,int *err,const char *tz)
{
#define FIRST_TOKEN strtok(line," \t")
#define NEXT_TOKEN  strtok(NULL," \t")
#define ERR do{(*err)++;delete fi;return(0);}while(0)
   int	 tmp;

   if(sscanf(line,"total %d",&tmp)==1)
      return 0;

   FileInfo *fi=0; /* don't instantiate until we at least have something */
   /* parse perms */
   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;

   fi = new FileInfo;
   switch(t[0])
   {
   case('l'):  // symlink
      fi->SetType(fi->SYMLINK);
      break;
   case('d'):  // directory
      fi->SetType(fi->DIRECTORY);
      break;
   case('-'):  // plain file
      fi->SetType(fi->NORMAL);
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
      fi->SetMode(mode);

   // link count
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   fi->SetNlink(atoi(t));

   // user
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   fi->SetUser(t);

   // group or size
   char *group_or_size = NEXT_TOKEN;

   // size or month
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   if(isdigit(*t))
   {
      // it's size, so the previous was group:
      fi->SetGroup(group_or_size);
      long long size;
      if(sscanf(t,"%lld",&size)==1)
	 fi->SetSize(size);
      t = NEXT_TOKEN;
      if(!t)
	 ERR;
   }
   else
   {
      // it was month, so the previous was size:
      long long size;
      if(sscanf(group_or_size,"%lld",&size)==1)
	 fi->SetSize(size);
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
   int prec=30;
   if(strlen(t)==5)
   {
      sscanf(t,"%2d:%2d",&date.tm_hour,&date.tm_min);
      date.tm_year=guess_year(date.tm_mon,date.tm_mday,date.tm_hour,date.tm_min) - 1900;
   }
   else
   {
      if(day_of_month+strlen(day_of_month)+1 == t)
	 year_anomaly=true;
      date.tm_year=atoi(t)-1900;
      /* We don't know the hour.  Set it to something other than 0, or
       * DST -1 will end up changing the date. */
      date.tm_hour = 12;
      prec=12*60*60;
   }

   date.tm_isdst=-1;
   date.tm_sec=30;

   fi->SetDate(mktime_from_tz(&date,tz),prec);

   char *name=strtok(NULL,"");
   if(!name)
      ERR;

   // there are ls which outputs extra space after year.
   if(year_anomaly && *name==' ')
      name++;

   if(fi->filetype==fi->SYMLINK)
   {
      char *arrow=name;
      while((arrow=strstr(arrow," -> "))!=0)
      {
	 if(arrow!=name && arrow[4]!=0)
	 {
	    *arrow=0;
	    fi->SetSymlink(arrow+4);
	    break;
	 }
	 arrow++;
      }
   }
   fi->SetName(name);

   return fi;
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
FileInfo *ParseFtpLongList_NT(char *line,int *err,const char *tz)
{
   char *t = FIRST_TOKEN;
   FileInfo *fi=0;
   if(t==0)
      ERR;
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
   tms.tm_sec=30;	   /* seconds after the minute [0, 61]  */
   tms.tm_min=minute;      /* minutes after the hour [0, 59] */
   tms.tm_hour=hour;	   /* hour since midnight [0, 23] */
   tms.tm_mday=day;	   /* day of the month [1, 31] */
   tms.tm_mon=month-1;     /* months since January [0, 11] */
   tms.tm_year=year-1900;  /* years since 1900 */
   tms.tm_isdst=-1;

   fi=new FileInfo();
   fi->SetDate(mktime_from_tz(&tms,tz),30);

   long long size;
   if(!strcmp(t,"<DIR>"))
      fi->SetType(fi->DIRECTORY);
   else
   {
      fi->SetType(fi->NORMAL);
      if(sscanf(t,"%lld",&size)!=1)
	 ERR;
      fi->SetSize(size);
   }

   t=strtok(NULL,"");
   if(t==0)
      ERR;
   while(*t==' ')
      t++;
   if(*t==0)
      ERR;
   fi->SetName(t);

   return fi;
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
FileInfo *ParseFtpLongList_EPLF(char *line,int *err,const char *)
{
   int len=strlen(line);
   const char *b=line;
   FileInfo *fi=0;

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
   if(name==0)
      ERR;

   fi=new FileInfo(name);
   if(size!=NO_SIZE)
      fi->SetSize(size);
   if(date!=NO_DATE)
      fi->SetDate(date,0);
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
FileInfo *ParseFtpLongList_OS2(char *line,int *err,const char *tz)
{
   FileInfo *fi=0;

   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;

   long long size;
   if(sscanf(t,"%lld",&size)!=1)
      ERR;
   fi=new FileInfo;
   fi->SetSize(size);

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   fi->SetType(fi->NORMAL);
   if(!strcmp(t,"DIR"))
   {
      fi->SetType(fi->DIRECTORY);
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
   tms.tm_sec=30;	   /* seconds after the minute [0, 61]  */
   tms.tm_min=minute;      /* minutes after the hour [0, 59] */
   tms.tm_hour=hour;	   /* hour since midnight [0, 23] */
   tms.tm_mday=day;	   /* day of the month [1, 31] */
   tms.tm_mon=month-1;     /* months since January [0, 11] */
   tms.tm_year=year-1900;  /* years since 1900 */
   tms.tm_isdst=-1;
   fi->SetDate(mktime_from_tz(&tms,tz),30);

   t=strtok(NULL,"");
   if(t==0)
      ERR;
   while(*t==' ')
      t++;
   if(*t==0)
      ERR;
   fi->SetName(t);

   return fi;
}

static
FileInfo *ParseFtpLongList_MacWebStar(char *line,int *err,const char *tz)
{
   FileInfo *fi=0;

   char *t = FIRST_TOKEN;
   if(t==0)
      ERR;

   fi=new FileInfo;
   switch(t[0])
   {
   case('l'):  // symlink
      fi->SetType(fi->SYMLINK);
      break;
   case('d'):  // directory
      fi->SetType(fi->DIRECTORY);
      break;
   case('-'):  // plain file
      fi->SetType(fi->NORMAL);
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
   if(mode==(mode_t)-1)
      ERR;
   // permissions are meaningless here.

   // "folder" or 0
   t = NEXT_TOKEN;
   if(!t)
      ERR;

   if(strcmp(t,"folder"))
   {
      // size?
      t = NEXT_TOKEN;
      if(!t)
	 ERR;
      // size
      t = NEXT_TOKEN;
      if(!t)
	 ERR;
      if(isdigit((unsigned char)*t))
      {
	 long long size;
	 if(sscanf(t,"%lld",&size)==1)
	    fi->SetSize(size);
      }
      else
	 ERR;
   }
   else
   {
      // ??
      t = NEXT_TOKEN;
      if(!t)
	 ERR;
   }

   // month
   t = NEXT_TOKEN;
   if(!t)
      ERR;

   struct tm date;
   memset(&date,0,sizeof(date));

   date.tm_mon=parse_month(t);
   if(date.tm_mon==-1)
      ERR;

   const char *day_of_month = NEXT_TOKEN;
   if(!day_of_month)
      ERR;
   date.tm_mday=atoi(day_of_month);

   // time or year
   t = NEXT_TOKEN;
   if(!t)
      ERR;
   if(parse_year_or_time(t,&date.tm_year,&date.tm_hour,&date.tm_min)==-1)
      ERR;

   date.tm_isdst=-1;
   date.tm_sec=30;
   int prec=30;

   if(date.tm_year==-1)
      date.tm_year=guess_year(date.tm_mon,date.tm_mday,date.tm_hour,date.tm_min) - 1900;
   else
   {
      date.tm_hour=12;
      prec=12*60*60;
   }

   fi->SetDate(mktime_from_tz(&date,tz),prec);

   char *name=strtok(NULL,"");
   if(!name)
      ERR;

   // no symlinks on Mac, but anyway.
   if(fi->filetype==fi->SYMLINK)
   {
      char *arrow=name;
      while((arrow=strstr(arrow," -> "))!=0)
      {
	 if(arrow!=name && arrow[4]!=0)
	 {
	    *arrow=0;
	    fi->SetSymlink(arrow+4);
	    break;
	 }
	 arrow++;
      }
   }
   fi->SetName(name);

   return fi;
}

/*
Type=cdir;Modify=20021029173810;Perm=el;Unique=BP8AAjJufAA; /
Type=pdir;Modify=20021029173810;Perm=el;Unique=BP8AAjJufAA; ..
Type=dir;Modify=20010118144705;Perm=e;Unique=BP8AAjNufAA; bin
Type=dir;Modify=19981021003019;Perm=el;Unique=BP8AAlhufAA; pub
Type=file;Size=12303;Modify=19970124132601;Perm=r;Unique=BP8AAo9ufAA; mailserv.FAQ
*/
FileInfo *ParseFtpLongList_MLSD(char *line,int *err,const char *)
{
   FileInfo *fi=0;

   const char *name=0;
   off_t size=NO_SIZE;
   time_t date=NO_DATE;
   bool dir=false;
   bool type_known=false;
   int perms=-1;

   for(char *tok=strtok(line,";"); tok; tok=strtok(0,";"))
   {
      if(tok[0]==' ')
      {
	 name=tok+1;
	 break;
      }
      if(!strcasecmp(tok,"Type=cdir")
      || !strcasecmp(tok,"Type=pdir")
      || !strcasecmp(tok,"Type=dir"))
      {
	 dir=true;
	 type_known=true;
	 continue;
      }
      if(!strcasecmp(tok,"Type=file"))
      {
	 dir=false;
	 type_known=true;
      }
      if(!strncasecmp(tok,"Modify=",7))
      {
	 date=Ftp::ConvertFtpDate(tok+7);
	 continue;
      }
      if(!strncasecmp(tok,"Size=",5))
      {
	 size=atoll(tok+5);
	 continue;
      }
      if(!strncasecmp(tok,"Perm=",5))
      {
	 perms=0;
	 for(tok+=5; *tok; tok++)
	 {
	    switch(to_ascii_lower(*tok))
	    {
	    case 'e': perms|=0111; break;
	    case 'l': perms|=0444; break;
	    case 'r': perms|=0444; break;
	    case 'c': perms|=0200; break;
	    case 'w': perms|=0200; break;
	    }
	 }
	 continue;
      }
   }
   if(name==0 || !type_known)
      ERR;

   fi=new FileInfo(name);
   if(size!=NO_SIZE)
      fi->SetSize(size);
   if(date!=NO_DATE)
      fi->SetDate(date,0);
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

Ftp::FtpLineParser Ftp::line_parsers[number_of_parsers+1]={
   ParseFtpLongList_UNIX,
   ParseFtpLongList_NT,
   ParseFtpLongList_EPLF,
   ParseFtpLongList_MLSD,
   ParseFtpLongList_OS2,
   ParseFtpLongList_MacWebStar,
   0
};
