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

#include "FileAccess.h"
#include "xmalloc.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <ctype.h>
#include "xalloca.h"
#include "log.h"

void FileAccess::Init()
{
   home=0;
   port=0;
   hostname=0;
   group=gpass=0;
   user=pass=0;

   file=0;
   file1=0;
   real_cwd=0;
   cwd=xstrdup("~");
   real_pos=-1;
   pos=0;
   mode=CLOSED;
   try_time=0;
   time(&event_time);
   opt_date=0;
   opt_size=0;
   last_error_resp=0;
   saved_errno=0;
   mkdir_p=false;

   sleep_time=30; // retry with 30 second interval
   timeout=600;	  // 10 minutes with no events = reconnect

   url=0;
}

FileAccess::FileAccess(const FileAccess *fa)
{
   Init();

   xfree(cwd);
   cwd=xstrdup(fa->cwd);
   xfree(home);
   home=xstrdup(fa->home);
   xfree(user);
   user=xstrdup(fa->user);
   xfree(pass);
   pass=xstrdup(fa->pass);
   xfree(group);
   group=xstrdup(fa->group);
   xfree(gpass);
   gpass=xstrdup(fa->gpass);
   xfree(hostname);
   hostname=xstrdup(fa->hostname);
   port=fa->port;
}

FileAccess::~FileAccess()
{
   xfree(file); file=0;
   xfree(cwd); cwd=0;
   xfree(real_cwd); real_cwd=0;
   xfree(last_error_resp); last_error_resp=0;
   xfree(home); home=0;
   xfree(user); user=0;
   xfree(pass); pass=0;
   xfree(group); group=0;
   xfree(gpass); gpass=0;
   xfree(hostname); hostname=0;
   xfree(url); url=0;
}

void  FileAccess::DebugPrint(const char *prefix,const char *str,int level)
{
   if(!Log::global)
      return;
   char *msg=(char*)alloca(strlen(prefix)+strlen(str)+6);
   while(*str)
   {
      strcpy(msg,prefix);
      int msglen=strlen(msg);
      while(*str && *str!='\n')
      {
	 if(!(*str=='\r' && (str[1]=='\n' || str[1]=='\0')))
	    msg[msglen++]=*str;
	 str++;
      }
      msg[msglen++]='\n';
      msg[msglen]=0;

      Log::global->Write(level,msg);

      if(!*str++)
         break;
   }
}

int   FileAccess::Poll(int fd,int ev)
{
   struct pollfd pfd;
   pfd.fd=fd;
   pfd.events=ev;
   pfd.revents=0;
   int res=poll(&pfd,1,0);
   if(res<1)
      return 0;
   if(CheckHangup(&pfd,1))
      return 0;
   if(pfd.revents)
      time(&event_time);
   return pfd.revents;
}

int   FileAccess::CheckHangup(struct pollfd *pfd,int num)
{
#ifdef SO_ERROR
   for(int i=0; i<num; i++)
   {
      char  str[256];
      int   s_errno=0;
      ADDRLEN_TYPE len;

      errno=0;

/* When the poll() is emulated by select(), there is no error condition
   flag, so we can only check for socket errors */

// Where does the error number go - to errno or to the pointer?
// It seems that to errno, but if the pointer is NULL it dumps core.
// (solaris 2.5)
// It seems to be different on glibc 2.0 - check both errno and s_errno

#ifdef HAVE_POLL
      if(pfd[i].revents&POLLERR)
#endif
      {
	 len=sizeof(s_errno);
	 getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,(char*)&s_errno,&len);
	 if(errno!=0 || s_errno!=0)
	 {
	    sprintf(str,_("Socket error (%s) - reconnecting"),
				       strerror(errno?errno:s_errno));
	    DebugPrint("**** ",str);
	    Disconnect();
	    return 1;
	 }
      }
   } /* end for */
#endif /* SO_ERROR */
   return 0;
}

void  FileAccess::Open(const char *fn,int mode,long offs)
{
   Close();
   Resume();
   file=xstrdup(fn);
   real_pos=-1;
   pos=offs;
   this->mode=mode;
   mkdir_p=false;
   try_time=0;
   return;
}

const char *FileAccess::StrError(int err)
{
   static char *str=0;
   static unsigned str_allocated=0;

   switch(err)
   {
   case(IN_PROGRESS):
      return(_("Operation is in progress"));
   case(OK):
      return(_("Error 0"));
   case(SEE_ERRNO):
      return(strerror(saved_errno));
   case(LOOKUP_ERROR):
      return(last_error_resp);
   case(NOT_OPEN):   // Actually this means an error in application
      return(_("Class is not Open()ed"));
   case(NO_FILE):
      if(last_error_resp)
      {
	 if(str_allocated<64+strlen(last_error_resp))
	    str=(char*)xrealloc(str,64+strlen(last_error_resp));
   	 sprintf(str,_("Access failed: %s"),last_error_resp);
	 return(str);
      }
      return(_("File cannot be accessed"));
   case(NO_HOST):
      return(_("Not connected"));
   case(FATAL):
      if(last_error_resp)
      {
	 if(str_allocated<64+strlen(last_error_resp))
	    str=(char*)xrealloc(str,64+strlen(last_error_resp));
   	 sprintf(str,_("Fatal error: %s"),last_error_resp);
	 return(str);
      }
      return(_("Fatal error"));
   case(STORE_FAILED):
      return(_("Store failed - you have to reput"));
   case(LOGIN_FAILED):
      return(_("Login failed"));
   case(NOT_SUPP):
      return(_("Operation not supported"));
   }
   return("");
}

void FileAccess::Close()
{
   xfree(file); file=0;
   xfree(file1); file1=0;
   mode=CLOSED;
   opt_date=0;
   opt_size=0;
   array_for_info=0;
}

void FileAccess::Rename(const char *f,const char *f1)
{
   Close();
   file=xstrdup(f);
   file1=xstrdup(f1);
   mode=RENAME;
}

void FileAccess::Mkdir(const char *fn,bool allp)
{
   Open(fn,MAKE_DIR);
   mkdir_p=allp;
}

bool FileAccess::SameLocationAs(FileAccess *fa)
{
   if(strcmp(this->GetProto(),fa->GetProto()))
      return false;
   return true;
}

const char *FileAccess::GetConnectURL(int flags)
{
   int len=1;
   len+=strlen(GetProto())+strlen("://");
   if(user)
   {
      len+=strlen(user)+1;
      if(pass)
	 len+=strlen(pass)+1;
   }
   if(hostname)
      len+=strlen(hostname);
   if(port!=0)
      len+=1+20;
   if(cwd)
      len+=1+strlen(cwd);
   url=(char*)xrealloc(url,len);
   sprintf(url,"%s://",GetProto());
   if(user)
   {
      strcat(url,user);
      if(pass && (flags&WITH_PASSWORD))
      {
	 strcat(url,":");
	 strcat(url,pass);
      }
      strcat(url,"@");
   }
   if(hostname)
      sprintf(url+strlen(url),"%s",hostname);
   if(port!=0)
      sprintf(url+strlen(url),":%d",port);
   if(cwd && strcmp(cwd,"~") && !(flags&NO_CWD))
      sprintf(url+strlen(url),"/%s",cwd+(cwd[0]=='/'));

   return url;
}

void FileAccess::Login(const char *user1,const char *pass1)
{
   Disconnect();
   xfree(user);
   user=xstrdup(user1);
   xfree(pass);
   pass=xstrdup(pass1);
   xfree(cwd);
   cwd=xstrdup("~");
   xfree(home);
   home=0;
}

void FileAccess::GroupLogin(const char *group,const char *pass)
{
   Disconnect();
   xfree(this->group);
   this->group=xstrdup(group);
   xfree(this->gpass);
   this->gpass=xstrdup(pass);
}

void FileAccess::AnonymousLogin()
{
   Disconnect();
   xfree(user); user=0;
   xfree(pass); pass=0;
   xfree(group); group=0;
   xfree(gpass); gpass=0;
}

void FileAccess::GetInfoArray(struct fileinfo *info,int count)
{
   Close();
   array_for_info=info;
   array_ptr=0;
   array_cnt=count;
   mode=ARRAY_INFO;
}

static void expand_tilde(char **path, const char *home)
{
   if((*path)[0]=='~' && ((*path)[1]==0 || (*path)[1]=='/'))
   {
      int home_len=strlen(home);
      if(home_len==1 && home[0]=='/' && (*path)[1]=='/')
	 home_len=0;
      int path_len=strlen(*path+1);
      // Note: don't shrink the array, because later memmove would break
      // We could first memmove then shrink, but it won't give much
      if(home_len>1)
	 *path=(char*)xrealloc(*path,path_len+home_len+1);
      memmove(*path+home_len,*path+1,path_len+1);
      memmove(*path,home,home_len);
   }
}

void  FileAccess::ExpandTildeInCWD()
{
   if(home)
   {
      if(cwd)
	 expand_tilde(&cwd,home);
      if(real_cwd)
	 expand_tilde(&real_cwd,home);
      if(mode==CHANGE_DIR && file)
	 expand_tilde(&file,home);
   }
}

static inline
int last_element_is_doubledot(char *path,char *end)
{
   return((end==path+2 && !strncmp(path,"..",2))
        || (end>path+2 && !strncmp(end-3,"/..",3)));
}

void FileAccess::Chdir(const char *path)
{
   char	 *newcwd=(char*)alloca(strlen(cwd)+strlen(path)+2);
   int	 prefix_size=0;

   ExpandTildeInCWD();

   if(cwd[0]=='/')
      prefix_size=1;
   else if(isalpha(cwd[0]) && cwd[1]==':')
      prefix_size=2+(cwd[2]=='/');
   else if(cwd[0]=='~')
   {
      prefix_size=1;
      while(cwd[prefix_size]!='/' && cwd[prefix_size]!='\0')
	 prefix_size++;
   }

   if(path[0]=='/')
   {
      strcpy(newcwd,path);
      prefix_size=1;
   }
   else if(isalpha(path[0]) && path[1]==':')
   {
      strcpy(newcwd,path);
      prefix_size=2+(path[2]=='/');
   }
   else if(path[0]=='~')
   {
      strcpy(newcwd,path);
      prefix_size=1;
      while(newcwd[prefix_size]!='/' && newcwd[prefix_size]!='\0')
	 prefix_size++;
   }
   else
   {
      if(cwd[0])
	 sprintf(newcwd,"%s/%s",cwd,path);
      else
	 strcpy(newcwd,path);
   }

   char	 *in;
   char	 *out;

   in=out=newcwd+prefix_size;

   while((in[0]=='.' && (in[1]=='/' || in[1]==0))
   || (in>newcwd && in[-1]=='/' && (in[0]=='/'
	 || (in[0]=='.' && in[1]=='.' && (in[2]=='/' || in[2]==0)))))
   {
      if(in[0]=='.' && in[1]=='.')
	 in++;
      in++;
      if(*in=='/')
	 in++;
   }

   for(;;)
   {
      if(in[0]=='/')
      {
	 // double slash or a slash on the end
	 if(in[1]=='/' || in[1]=='\0')
	 {
	    in++;
	    continue;
	 }
	 if(in[1]=='.')
	 {
	    // . - cur dir
	    if(in[2]=='/' || in[2]=='\0')
	    {
	       in+=2;
	       continue;
	    }
	    // .. - prev dir
	    if(in[2]=='.' && (in[3]=='/' || in[3]=='\0'))
	    {
	       if(last_element_is_doubledot(newcwd+prefix_size,out)
	       || out==newcwd
	       || (out==newcwd+prefix_size && out[-1]!='/'))
	       {
		  if(out>newcwd && out[-1]!='/')
		     *out++='/';
		  *out++='.';
		  *out++='.';
	       }
	       else
	       {
		  while(out>newcwd+prefix_size && *--out!='/')
		     ;
	       }
	       in+=3;
	       continue;
	    }
	 }
	 // don't add slash after prefix with slash
	 if(out>newcwd && out[-1]=='/')
	 {
	    in++;
	    continue;
	 }
      }
      if((*out++=*in++)=='\0')
	 break;
   }
   Open(newcwd,CHANGE_DIR);
}


FileAccess *SessionPool::pool[pool_size];

void SessionPool::Reuse(FileAccess *f)
{
   if(f->GetHostName()==0)
   {
      delete f;
      return;
   }
   f->Close();
   int i;
   for(i=0; i<pool_size; i++)
   {
      if(pool[i]==0)
      {
	 pool[i]=f;
	 return;
      }
   }
   for(i=0; i<pool_size; i++)
   {
      if(f->IsBetterThan(pool[i]))
      {
	 delete pool[i];
	 pool[i]=f;
	 return;
      }
   }
   delete f;
}

void SessionPool::Print(FILE *f)
{
   int arr[pool_size];
   int n=0;
   int i;

   for(i=0; i<pool_size; i++)
   {
      if(pool[i]==0)
	 continue;
      int j;
      for(j=0; j<n; j++)
	 if(pool[arr[j]]->SameLocationAs(pool[i]))
	    break;
      if(j==n)
	 arr[n++]=i;
   }

   // sort?

   for(i=0; i<n; i++)
      fprintf(f,"%d\t%s\n",arr[i],pool[arr[i]]->GetConnectURL());
}

FileAccess *SessionPool::GetSession(int n)
{
   if(n<0 || n>=pool_size)
      return 0;
   FileAccess *s=pool[n];
   pool[n]=0;
   return s;
}

FileAccess *SessionPool::Walk(int *n,const char *proto)
{
   for( ; *n<pool_size; (*n)++)
   {
      if(pool[*n] && !strcmp(pool[*n]->GetProto(),proto))
	 return pool[*n];
   }
   return 0;
}
