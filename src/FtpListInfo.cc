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
#include <string.h>
#include <ctype.h>
#include "xalloca.h"

FtpListInfo::FtpListInfo(Ftp *s)
{
   session=s;
   get_info=0;
   get_info_cnt=0;
   glob=0;
   state=INITIAL;
}

FtpListInfo::~FtpListInfo()
{
   if(glob)
      delete glob;
   session->Close();
   xfree(get_info);
}

int FtpListInfo::Do()
{
   Ftp::fileinfo *cur;
   FileInfo *file;
   int res;
   int m=STALL;
   char	**glob_res;

   if(done)
      state=DONE;

   switch(state)
   {
   case(INITIAL):
      glob=new RemoteGlob(session,"",session->LONG_LIST);
      glob->NoCache();
      state=GETTING_LONG_LIST;
      m=MOVED;

   case(GETTING_LONG_LIST):
      if(!glob->Done())
	 return m;
      if(glob->Error())
      {
	 SetError(session->StrError(glob->ErrorCode()));
      	 delete glob;
	 glob=0;
	 return MOVED;
      }
      glob_res=glob->GetResult();
      result=ParseFtpLongList(glob_res);
      delete glob;

      glob=new RemoteGlob(session,"",session->LIST);
      glob->NoCache();
      state=GETTING_SHORT_LIST;
      m=MOVED;

   case(GETTING_SHORT_LIST):
      if(!glob->Done())
	 return m;
      if(glob->Error())
      {
	 SetError(session->StrError(glob->ErrorCode()));
      	 delete glob;
	 glob=0;
	 return MOVED;
      }
      glob_res=glob->GetResult();
      result->Merge(glob_res);
      delete glob;
      glob=0;

      if(rxc_exclude || rxc_include)
	 result->Exclude(path,rxc_exclude,rxc_include);

      state=GETTING_INFO;
      m=MOVED;

   case(GETTING_INFO):
      if(session->IsClosed())
      {
	 get_info_cnt=result->get_fnum();
	 if(get_info_cnt==0)
	    goto info_done;

	 get_info=(Ftp::fileinfo*)xmalloc(sizeof(*get_info)*get_info_cnt);
	 cur=get_info;

	 get_info_cnt=0;
	 result->rewind();
	 for(file=result->curr(); file!=0; file=result->next())
	 {
	    cur->get_size = !(file->defined & file->SIZE);
	    cur->get_time = !(file->defined & file->DATE);

	    if(file->defined & file->TYPE)
	    {
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
	 m=MOVED;
      }
      res=session->Done();
      if(res==Ftp::DO_AGAIN)
	 return m;
      if(res==Ftp::IN_PROGRESS)
	 return m;
      assert(res==Ftp::OK);
      session->Close();

      cur=get_info;
      while(get_info_cnt-->0)
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

mode_t	 parse_perms(const char *s)
{
   mode_t   p=0;

   if(strlen(s)!=9)
      bad: return (mode_t)-1;

   switch(s[0])
   {
   case('r'): p|=S_IRUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISUID; break;
   case('s'): p|=S_ISUID;
   case('x'): p|=S_IXUSR; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IRGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISGID; break;
   case('s'): p|=S_ISGID;
   case('x'): p|=S_IXGRP; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IROTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWOTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('T'): case('t'): p|=S_ISVTX; break;
   case('l'): p|=S_ISGID; p&=~S_IXGRP; break;
   case('x'): p|=S_IXOTH; break;
   case('-'): break;
   default: goto bad;
   }

   return p;
}

int   parse_month(char *m)
{
   static const char *months[]={
      "Jan","Feb","Mar","Apr","May","Jun",
      "Jul","Aug","Sep","Oct","Nov","Dec",0
   };
   for(int i=0; months[i]; i++)
      if(!strcasecmp(months[i],m))
	 return(i%12);
   return -1;
}

FileSet *FtpListInfo::ParseFtpLongList(char **lines)
{
#define FIRST_TOKEN strtok(line," \t")
#define NEXT_TOKEN  strtok(NULL," \t")
   char	 *line;
   int	 base_dir_len=-1;
   char	 *curr_dir=xstrdup("");
   int 	 len;

   FileSet *set=new FileSet;

   if(lines==0)
      return set;

   while((line=*lines++)!=0)
   {
      if(sscanf(line,"total %d",&len)==1)
	 continue;
      len=strlen(line);
      if(len==0)
	 continue;

      /* dir1/dir2/dir3: */
      if(line[len-1]==':' && strchr(line,' ')==0 && strchr(line,'\t')==0)
      {
	 // we got directory name
	 line[--len]=0;
	 if(base_dir_len>=0)
	 {
	    xfree(curr_dir);
	    if(len<=base_dir_len)
	       curr_dir=xstrdup("");   // unlikely case
	    else
	       curr_dir=xstrdup(line+base_dir_len);
	 }
   	 else
	 {
	    char *b=strrchr(line,'/');
	    if(b)
	       base_dir_len=b-line+1;
	    else
	       base_dir_len=0;
	 }
	 continue;
      }

      /* parse perms */
      char *t = FIRST_TOKEN;
      if(t==0)
	 continue;
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
      default:
	 continue;   // unknown
      }
      mode_t mode=parse_perms(t+1);
      if(mode!=(mode_t)-1)
	 fi.SetMode(mode);

      // link count
      t = NEXT_TOKEN;
      if(!t)
	 continue;

      // user
      t = NEXT_TOKEN;
      if(!t)
	 continue;

      // group or size
      char *group_or_size = NEXT_TOKEN;

      // size or month
      t = NEXT_TOKEN;
      if(!t)
	 continue;
      if(isdigit(*t))
      {
	 // size
      	 fi.SetSize(atol(t));
	 t = NEXT_TOKEN;
	 if(!t)
	    continue;
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
	 continue;
      date.tm_mday=atoi(day_of_month);

      bool year_anomaly=false;

      // time or year
      t = NEXT_TOKEN;
      if(!t)
	 continue;
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
      if(!name || !strcmp(name,".") || !strcmp(name,".."))
	 continue;

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
	 }
      }
      if(curr_dir[0])
      {
	 char *fullname=(char*)alloca(strlen(curr_dir)+1+strlen(name)+1);
	 sprintf(fullname,"%s/%s",curr_dir,name);
	 fi.SetName(fullname);
      }
      else
	 fi.SetName(name);

      set->Add(new FileInfo(fi));
   }
   return set;
}

const char *FtpListInfo::Status()
{
   switch(state)
   {
   case(DONE):
   case(INITIAL):
      return "";
   case(GETTING_LONG_LIST):
   case(GETTING_SHORT_LIST):
      return _("Getting directory contents...");
   case(GETTING_INFO):
      return _("Getting files information...");
   }
   // cat't happen
   abort();
}
