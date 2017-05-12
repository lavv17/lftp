/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#define number_of_parsers 7

FileSet *FtpListInfo::Parse(const char *buf,int len)
{
   if(mode==FA::LONG_LIST || mode==FA::MP_LIST)
   {
      if(len==0 && mode==FA::LONG_LIST
      && !ResMgr::QueryBool("ftp:list-empty-ok",session->GetHostName()))
      {
	 mode=FA::LIST;
	 return 0;
      }
      int err;
      FileSet *set=session->ParseLongList(buf,len,&err);
      if(!set || err>0)
      {
	 if(mode==FA::MP_LIST)
	    mode=FA::LONG_LIST;
	 else
	    mode=FA::LIST;
      }
      return set;
   }
   else
   {
      return ParseShortList(buf,len);
   }
}

FileSet *Ftp::ParseLongList(const char *buf,int len,int *err_ret) const
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

   xstring line;
   xstring tmp_line;

   FtpLineParser guessed_parser=0;
   FileSet **the_set=0;
   int *the_err=0;
   int *best_err1=&err[0];
   int *best_err2=&err[1];

   const char *tz=Query("timezone",hostname);

   for(;;)
   {
      const char *nl=(char*)memchr(buf,'\n',len);
      if(!nl)
	 break;
      line.nset(buf,nl-buf);
      line.chomp('\r');
      if(line.length()==0)
      {
	 len-=nl+1-buf;
	 buf=nl+1;
	 continue;
      }

      len-=nl+1-buf;
      buf=nl+1;

      if(!guessed_parser)
      {
	 for(i=0; i<number_of_parsers; i++)
	 {
	    tmp_line.set(line);	 // parser can clobber the line - work on a copy
	    FileInfo *info=(*line_parsers[i])(tmp_line.get_non_const(),&err[i],tz);
	    if(info && info->name.length()>1)
	       info->name.chomp('/');
	    if(info && !strchr(info->name,'/'))
	       set[i]->Add(info);
	    else
	       delete info;

	    if(*best_err1>err[i])
	       best_err1=&err[i];
	    if(*best_err2>err[i] && best_err1!=&err[i])
	       best_err2=&err[i];
	    if(*best_err1>16)
	       goto leave; // too many errors with best parser.
	 }
	 if(*best_err2 > (*best_err1+1)*16)
	 {
	    i=best_err1-err;
	    guessed_parser=line_parsers[i];
	    the_set=&set[i];
	    the_err=&err[i];
	 }
      }
      else
      {
	 FileInfo *info=(*guessed_parser)(line.get_non_const(),the_err,tz);
	 if(info && info->name.length()>1)
	    info->name.chomp('/');
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

      const char *nl=(const char*)memchr(buf,'\n',len);
      if(!nl)
	 break;
      line_len=nl-buf;
      if(line_len>0 && buf[line_len-1]=='\r')
	 line_len--;
      FileInfo::type type=FileInfo::UNKNOWN;
      const char *slash=(const char*)memchr(buf,'/',line_len);
      if(slash)
      {
	 type=FileInfo::DIRECTORY;
	 line_len=slash-buf;
      }
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
      {
	 FileInfo *fi=new FileInfo(line);
	 if(type!=fi->UNKNOWN)
	    fi->SetType(type);
	 set->Add(fi);
      }
   }
   return set;
}

static
FileInfo *ParseFtpLongList_UNIX(char *line,int *err,const char *tz)
{
   int	 tmp;
   if(sscanf(line,"total %d",&tmp)==1)
      return 0;
   if(!strncasecmp(line,"Status of ",10))
      return 0;	  // STAT output.
   if(strchr("bcpsD",line[0])) // block, char, pipe, socket, Door.
      return 0;

   FileInfo *fi=FileInfo::parse_ls_line(line,tz);
   if(!fi)
   {
      (*err)++;
      return 0;
   }
   return fi;
}

#define FIRST_TOKEN strtok(line," \t")
#define NEXT_TOKEN  strtok(NULL," \t")
#define ERR do{(*err)++;delete fi;return(0);}while(0)

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
   char am='A'; // AM/PM is optional
   if(sscanf(t,"%2d:%2d%c",&hour,&minute,&am)<2)
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
ASUSER          8192 04/26/05 13:54:16 *DIR       dir/
ASUSER          8192 04/26/05 13:57:34 *DIR       dir1/
ASUSER        365255 02/28/01 15:41:40 *STMF      readme.txt
ASUSER       8489625 03/18/03 09:37:00 *STMF      saved.zip
ASUSER        365255 02/28/01 15:41:40 *STMF      unist.old
*/
static
FileInfo *ParseFtpLongList_AS400(char *line,int *err,const char *tz)
{
   char *t = FIRST_TOKEN;
   FileInfo *fi=0;
   if(t==0)
      ERR;
   char *user=t;

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   long long size;
   if(sscanf(t,"%lld",&size)!=1)
      ERR;

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   int month,day,year;
   if(sscanf(t,"%2d/%2d/%2d",&month,&day,&year)!=3)
      ERR;
   if(year>=70)
      year+=1900;
   else
      year+=2000;

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   int hour,minute,second;
   if(sscanf(t,"%2d:%2d:%2d",&hour,&minute,&second)!=3)
      ERR;
   t = NEXT_TOKEN;
   if(t==0)
      ERR;

   struct tm tms;
   tms.tm_sec=second;	   /* seconds after the minute [0, 61]  */
   tms.tm_min=minute;      /* minutes after the hour [0, 59] */
   tms.tm_hour=hour;	   /* hour since midnight [0, 23] */
   tms.tm_mday=day;	   /* day of the month [1, 31] */
   tms.tm_mon=month-1;     /* months since January [0, 11] */
   tms.tm_year=year-1900;  /* years since 1900 */
   tms.tm_isdst=-1;
   time_t mtime=mktime_from_tz(&tms,tz);

   t = NEXT_TOKEN;
   if(t==0)
      ERR;
   FileInfo::type type=FileInfo::UNKNOWN;
   if(!strcmp(t,"*DIR"))
      type=FileInfo::DIRECTORY;
   else
      type=FileInfo::NORMAL;

   t=strtok(NULL,"");
   if(t==0)
      ERR;
   while(*t==' ')
      t++;
   if(*t==0)
      ERR;
   char *slash=strchr(t,'/');
   if(slash)
   {
      if(slash==t)
	 return 0;
      *slash=0;
      type=FileInfo::DIRECTORY;
      if(slash[1])
      {
	 fi=new FileInfo(t);
	 fi->SetType(type);
	 return fi;
      }
   }
   fi=new FileInfo(t);
   fi->SetType(type);
   fi->SetSize(size);
   fi->SetDate(mtime,0);
   fi->SetUser(user);
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
	       if(sscanf(scan+2,"%o",&perms)!=1)
		  perms=-1;
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
   if(name==0 || !type_known)
      ERR;

   fi=new FileInfo(xstring::get_tmp(name,name_len));
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
modify=20161215062118;perm=flcdmpe;type=dir;UNIX.group=503;UNIX.mode=0700; directory-name
modify=20161213121618;perm=adfrw;size=6369064;type=file;UNIX.group=503;UNIX.mode=0644; file-name
modify=20120103123744;perm=adfrw;size=11;type=OS.unix=symlink;UNIX.group=0;UNIX.mode=0777; www
*/
FileInfo *ParseFtpLongList_MLSD(char *line,int *err,const char *)
{
   FileInfo *fi=0;

   const char *name=0;
   off_t size=NO_SIZE;
   time_t date=NO_DATE;
   const char *owner=0;
   const char *group=0;
   FileInfo::type type=FileInfo::UNKNOWN;
   int perms=-1;

   char *space=strstr(line,"; ");
   if(space) {
      name=space+2;
      *space=0;
   } else {
      /* NcFTPd does not put a semicolon after last fact, workaround it. */
      space=strchr(line,' ');
      if(!space)
	 ERR;
      name=space+1;
      *space=0;
   }

   for(char *tok=strtok(line,";"); tok; tok=strtok(0,";"))
   {
      if(!strcasecmp(tok,"Type=cdir")
      || !strcasecmp(tok,"Type=pdir")
      || !strcasecmp(tok,"Type=dir"))
      {
	 type=FileInfo::DIRECTORY;
	 continue;
      }
      if(!strcasecmp(tok,"Type=file"))
      {
	 type=FileInfo::NORMAL;
	 continue;
      }
      if(!strcasecmp(tok,"Type=OS.unix=symlink"))
      {
	 type=FileInfo::SYMLINK;
	 continue;
      }
      if(!strncasecmp(tok,"Modify=",7))
      {
	 date=Ftp::ConvertFtpDate(tok+7);
	 continue;
      }
      if(!strncasecmp(tok,"Size=",5))
      {
	 long long size_ll;
	 if(sscanf(tok+5,"%lld",&size_ll)==1)
	    size=size_ll;
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
      if(!strncasecmp(tok,"UNIX.mode=",10))
      {
	 if(sscanf(tok+10,"%o",&perms)!=1)
	    perms=-1;
	 continue;
      }
      if(!strncasecmp(tok,"UNIX.owner=",11))
      {
	 owner=tok+11;
	 continue;
      }
      if(!strncasecmp(tok,"UNIX.group=",11))
      {
	 group=tok+11;
	 continue;
      }
      if(!strncasecmp(tok,"UNIX.uid=",9))
      {
	 if(!owner)
	    owner=tok+9;
	 continue;
      }
      if(!strncasecmp(tok,"UNIX.gid=",9))
      {
	 if(!group)
	    group=tok+9;
	 continue;
      }
   }
   if(name==0 || !*name || type==FileInfo::UNKNOWN)
      ERR;

   fi=new FileInfo(name);
   if(size!=NO_SIZE)
      fi->SetSize(size);
   if(date!=NO_DATE)
      fi->SetDate(date,0);
   fi->SetType(type);
   if(perms!=-1)
      fi->SetMode(perms);
   if(owner)
      fi->SetUser(owner);
   if(group)
      fi->SetGroup(group);

   return fi;
}

Ftp::FtpLineParser Ftp::line_parsers[number_of_parsers]={
   ParseFtpLongList_UNIX,
   ParseFtpLongList_NT,
   ParseFtpLongList_EPLF,
   ParseFtpLongList_MLSD,
   ParseFtpLongList_AS400,
   ParseFtpLongList_OS2,
   ParseFtpLongList_MacWebStar,
};
