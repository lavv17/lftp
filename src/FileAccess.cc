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
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <fnmatch.h>
#include "LsCache.h"
#include "xalloca.h"
#include "log.h"
#include "url.h"
#ifdef WITH_MODULES
# include "module.h"
#endif

void FileAccess::Init()
{
   home=0;
   portname=0;
   hostname=0;
   group=gpass=0;
   user=pass=0;

   file=0;
   file1=0;
   real_cwd=0;
   default_cwd="~";
   cwd=xstrdup(default_cwd);
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

   entity_size=-1;
   entity_date=(time_t)-1;

   closure=0;
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
   xfree(portname);
   portname=xstrdup(fa->portname);
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
   xfree(portname); portname=0;
   xfree(url); url=0;
   xfree(closure); closure=0;
}

void  FileAccess::DebugPrint(const char *prefix,const char *str,int level)
{
   if(!Log::global)
      return;
   char *msg=(char*)alloca(strlen(prefix)+strlen(str)+6);

   do
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
   while(*str);
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
      return -1;
   if(pfd.revents)
      time(&event_time);
   return pfd.revents;
}

int   FileAccess::CheckHangup(struct pollfd *pfd,int num)
{
   for(int i=0; i<num; i++)
   {
#ifdef SO_ERROR
      char  str[256];
      int   s_errno=0;
      socklen_t len;

      errno=0;

// Where does the error number go - to errno or to the pointer?
// It seems that to errno, but if the pointer is NULL it dumps core.
// (solaris 2.5)
// It seems to be different on glibc 2.0 - check both errno and s_errno

      len=sizeof(s_errno);
      getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,(char*)&s_errno,&len);
      if(errno==ENOTSOCK)
	 return 0;
      if(errno!=0 || s_errno!=0)
      {
	 sprintf(str,_("Socket error (%s) - reconnecting"),
				    strerror(errno?errno:s_errno));
	 DebugPrint("**** ",str);
	 return 1;
      }
#endif /* SO_ERROR */
      if(pfd[i].revents&POLLERR)
      {
	 DebugPrint("**** ","POLLERR");
	 return 1;
      }
   } /* end for */
   return 0;
}

void FileAccess::NonBlock(int fd)
{
   int fl=0;
   fcntl(fd,F_GETFL,&fl);
   fcntl(fd,F_SETFL,fl|O_NONBLOCK);
}
void FileAccess::CloseOnExec(int fd)
{
   fcntl(fd,F_SETFD,FD_CLOEXEC);
}
void FileAccess::KeepAlive(int sock)
{
   static int one=1;
   setsockopt(sock,SOL_SOCKET,SO_KEEPALIVE,(char*)&one,sizeof(one));
}
void FileAccess::SetSocketBuffer(int sock,int socket_buffer)
{
   if(socket_buffer==0)
      return;
   if(-1==setsockopt(sock,SOL_SOCKET,SO_SNDBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_SNDBUF,%d): %s\n",socket_buffer,strerror(errno));
   if(-1==setsockopt(sock,SOL_SOCKET,SO_RCVBUF,(char*)&socket_buffer,sizeof(socket_buffer)))
      Log::global->Format(1,"setsockopt(SO_RCVBUF,%d): %s\n",socket_buffer,strerror(errno));
}
void FileAccess::SetSocketMaxseg(int sock,int socket_maxseg)
{
#ifndef SOL_TCP
# define SOL_TCP IPPROTO_TCP
#endif
#ifdef TCP_MAXSEG
   if(socket_maxseg==0)
      return;
   if(-1==setsockopt(sock,SOL_TCP,TCP_MAXSEG,(char*)&socket_maxseg,sizeof(socket_maxseg)))
      Log::global->Format(1,"setsockopt(TCP_MAXSEG,%d): %s\n",socket_maxseg,strerror(errno));
#endif
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

   switch((open_mode)mode)
   {
   case STORE:
   case REMOVE:
   case MAKE_DIR:
      LsCache::FileChanged(this,file);
      break;
   case REMOVE_DIR:
      LsCache::FileChanged(this,file);
      LsCache::TreeChanged(this,file);
      break;
   default:
      break;
   }
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
   entity_size=-1;
   entity_date=(time_t)-1;
}

void FileAccess::Rename(const char *f,const char *f1)
{
   Close();
   file=xstrdup(f);
   file1=xstrdup(f1);
   mode=RENAME;

   LsCache::TreeChanged(this,file);
   LsCache::FileChanged(this,file);
   LsCache::FileChanged(this,file1);
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
bool FileAccess::SameSiteAs(FileAccess *fa)
{
   return SameLocationAs(fa);
}

const char *FileAccess::GetConnectURL(int flags)
{
   int len=1;
   const char *proto=GetProto();
   if(proto[0]==0)
      return "";
   len+=strlen(proto)+strlen("://");
   if(user)
   {
      len+=strlen(user)*3+1;
      if(pass)
	 len+=strlen(pass)*3+1;
   }
   if(hostname)
      len+=strlen(hostname)*3;
   if(portname)
      len+=1+strlen(portname);
   if(cwd)
      len+=1+strlen(cwd)*3;
   url=(char*)xrealloc(url,len);
   sprintf(url,"%s://",proto);
   if(user)
   {
      url::encode_string(user,url+strlen(url),"/:@"URL_UNSAFE);
      if(pass && (flags&WITH_PASSWORD))
      {
	 strcat(url,":");
	 url::encode_string(pass,url+strlen(url),"/:@"URL_UNSAFE);
      }
      strcat(url,"@");
   }
   if(hostname)
      url::encode_string(hostname,url+strlen(url),"/:"URL_UNSAFE);
   if(portname)
      url::encode_string(portname,url+strlen(url),"/"URL_UNSAFE);
   if(cwd && strcmp(cwd,"~") && !(flags&NO_CWD))
   {
      if(cwd[0]!='/') // e.g. ~/path
	 strcat(url,"/");
      url::encode_string(cwd,url+strlen(url));
   }
   return url;
}

void FileAccess::Connect(const char *host1,const char *port1)
{
   Close();
   xfree(hostname);
   hostname=xstrdup(host1);
   xfree(portname);
   portname=xstrdup(port1);
   xfree(cwd);
   cwd=xstrdup(default_cwd);
   xfree(home); home=0;
}

void FileAccess::Login(const char *user1,const char *pass1)
{
   Disconnect();
   xfree(user);
   user=xstrdup(user1);
   xfree(pass);
   pass=xstrdup(pass1);
   xfree(cwd);
   cwd=xstrdup(default_cwd);
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

void FileAccess::Chdir(const char *path,bool verify)
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
   if(verify)
      Open(newcwd,CHANGE_DIR);
   else
   {
      xfree(cwd);
      cwd=xstrdup(newcwd);
   }
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

bool FileAccess::NotSerious(int e)
{
   switch(e)
   {
   case(ETIMEDOUT):
#ifdef ECONNRESET
   case(ECONNRESET):
#endif
   case(ECONNREFUSED):
#ifdef EHOSTUNREACH
   case(EHOSTUNREACH):
#endif
#ifdef EHOSTDOWN
   case(EHOSTDOWN):
#endif
#ifdef ENETRESET
   case(ENETRESET):
#endif
#ifdef ENETUNREACH
   case(ENETUNREACH):
#endif
#ifdef ENETDOWN
   case(ENETDOWN):
#endif
#ifdef ECONNABORTED
   case(ECONNABORTED):
#endif
      return true;
   }
   return false;
}


int FileAccess::BytesAllowed()
{
#define LARGE 0x10000000

   if(bytes_pool_rate==0) // unlimited
      return LARGE;

   if(now>bytes_pool_time)
   {
      time_t dif=now-bytes_pool_time;

      // prevent overflow
      if((LARGE-bytes_pool)/dif < bytes_pool_rate)
	 bytes_pool = bytes_pool_max>0 ? bytes_pool_max : LARGE;
      else
	 bytes_pool += dif*bytes_pool_rate;

      if(bytes_pool>bytes_pool_max && bytes_pool_max>0)
	 bytes_pool=bytes_pool_max;

      bytes_pool_time=now;
   }
   return bytes_pool;
}

void FileAccess::BytesUsed(int bytes)
{
   if(bytes_pool<bytes)
      bytes_pool=0;
   else
      bytes_pool-=bytes;
}

void FileAccess::BytesReset()
{
   bytes_pool=bytes_pool_rate;
   bytes_pool_time=now;
}

ResValue FileAccess::Query(const char *name,const char *closure)
{
   const char *prefix=GetProto();
   char *fullname=(char*)alloca(3+strlen(prefix)+1+strlen(name)+1);
   sprintf(fullname,"%s:%s",prefix,name);
   ResDecl *type;
   ResValue res(0);
   if(ResMgr::FindVar(fullname,&type)==0)
      res=ResMgr::Query(fullname,closure);
   else
   {
      sprintf(fullname,"net:%s",name);
      res=ResMgr::Query(fullname,closure);
   }
   return res;
}

void FileAccess::Reconfig()
{
   bytes_pool_rate = Query("limit-rate",closure);
   bytes_pool_max  = Query("limit-max",closure);
   BytesReset(); // to cut bytes_pool.
}

// FileAccess::Protocol implementation
FileAccess::Protocol *FileAccess::Protocol::chain=0;

FileAccess::Protocol::Protocol(const char *proto, SessionCreator *creator)
{
   this->proto=proto;
   this->New=creator;
   this->next=chain;
   chain=this;
}

FileAccess::Protocol *FileAccess::Protocol::FindProto(const char *proto)
{
   for(Protocol *scan=chain; scan; scan=scan->next)
      if(!strcmp(scan->proto,proto))
	 return scan;
   return 0;
}

FileAccess *FileAccess::Protocol::NewSession(const char *proto)
{
   Protocol *p;

   p=FindProto(proto);
   if(p)
      return p->New();

#ifdef WITH_MODULES
#define PROTO_PREFIX "proto-"
   char *mod=(char*)alloca(strlen(PROTO_PREFIX)+strlen(proto)+1);
   sprintf(mod,"%s%s",PROTO_PREFIX,proto);
   void *map=module_load(mod,0,0);
   if(map==0)
   {
      fprintf(stderr,"%s\n",module_error_message());
      return 0;
   }
   p=FindProto(proto);
   if(p)
      return p->New();
#endif
   return 0;
}

// FileAccessOperation implementation
FileAccessOperation::FileAccessOperation()
{
   done=false;
   error_text=0;
   use_cache=true;
}

FileAccessOperation::~FileAccessOperation()
{
   xfree(error_text);
}

void FileAccessOperation::SetError(const char *e)
{
   xfree(error_text);
   error_text=xstrdup(e);
   done=true;
}


// ListInfo implementation
ListInfo::ListInfo()
{
   result=0;

   rxc_exclude=0;
   rxc_include=0;
   path=0;

   need=0;

   follow_symlinks=false;
}

ListInfo::~ListInfo()
{
   if(result)
      delete result;
}

void ListInfo::SetExclude(const char *p,regex_t *x,regex_t *i)
{
   rxc_exclude=x;
   rxc_include=i;
   path=p;
}

// Glob implementation
Glob::~Glob()
{
   free_list();
   xfree(pattern);
}

Glob::Glob(const char *p)
{
   pattern=xstrdup(p);
   list=0;
   list_size=0;
   list_alloc=0;
}

void Glob::free_list()
{
   for(int i=0; i<list_size; i++)
      xfree(list[i]);
   xfree(list);
   list=0;
   list_alloc=0;
   list_size=0;
}

void Glob::add_force(const char *ptr,int len)
{
   // insert new file name into list
   if(list_size>=list_alloc-1)
   {
      if(list_alloc==0)
	 list_alloc=32;
      list=(char**)xrealloc(list,(list_alloc*=2)*sizeof(*list));
   }
   list[list_size]=(char*)xmalloc(len+1);
   memcpy(list[list_size],ptr,len);
   list[list_size][len]=0;
   list[++list_size]=0;
}
void Glob::add(const char *ptr,int len)
{
   char *s=(char*)alloca(len+1);
   memcpy(s,ptr,len);
   s[len]=0;

   if(pattern[0]!=0
   && fnmatch(pattern, s, FNM_PATHNAME|FNM_PERIOD)!=0)
      return; // unmatched

   add_force(ptr,len);
}

bool Glob::HasWildcards(const char *s)
{
   while(*s)
   {
      switch(*s)
      {
      case '\\':
	 if(s[1])
	    s++;
	 break;
      case '*':
      case '[':
      case '?':
	 return true;
      }
      s++;
   }
   return false;
}

void Glob::UnquoteWildcards(char *s)
{
   char *store=s;
   for(;;)
   {
      if(*s=='\\')
      {
	 if(s[1]=='*'
	 || s[1]=='['
	 || s[1]=='?'
	 || s[1]=='\\')
	    s++;
      }
      *store=*s;
      if(*s==0)
	 break;
      s++;
      store++;
   }
}

int NoGlob::Do()
{
   if(!done)
   {
      done=true;
      return MOVED;
   }
   return STALL;
}
NoGlob::NoGlob(const char *p) : Glob(p)
{
   char *p1=pattern;
   pattern=xstrdup("");
   UnquoteWildcards(p1);
   add(p1);
   xfree(p1);
}
