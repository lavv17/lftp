/*
 * lftp and utils
 *
 * Copyright (c) 1996-2004 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include "ascii_ctype.h"
#include <fcntl.h>
#include "LsCache.h"
#include "log.h"
#include "url.h"
#include "misc.h"
#include "DummyProto.h"
#include "netrc.h"
#include "ArgV.h"
#include "ConnectionSlot.h"
#ifdef WITH_MODULES
# include "module.h"
#endif

FileAccess *FileAccess::chain=0;

void FileAccess::Init()
{
   vproto=0;
   home=0;
   portname=0;
   hostname=0;
   user=pass=0;
   pass_open=false;

   file=0;
   file_url=0;
   file1=0;
   real_cwd=0;
   default_cwd="~";
   cwd=xstrdup(default_cwd);
   real_pos=-1;
   pos=0;
   mode=CLOSED;
   try_time=0;
   retries=0;
   event_time=now;
   opt_date=0;
   opt_size=0;
   error=0;
   error_code=OK;
   saved_errno=0;
   mkdir_p=false;
   ascii=false;
   norest_manual=false;
   location=0;
   suggested_filename=0;
   entity_content_type=0;
   entity_charset=0;
   array_ptr=array_cnt=0;

   url=0;

   entity_size=NO_SIZE;
   entity_date=NO_DATE;

   closure=0;
   res_prefix=0;

   chmod_mode=0644;

   priority=last_priority=0;

   next=chain;
   chain=this;
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
   pass_open=fa->pass_open;
   xfree(hostname);
   hostname=xstrdup(fa->hostname);
   xfree(portname);
   portname=xstrdup(fa->portname);
   vproto=xstrdup(fa->vproto);
}

FileAccess::~FileAccess()
{
   xfree(vproto);
   xfree(file);
   xfree(file_url);
   xfree(cwd);
   xfree(real_cwd);
   xfree(error);
   xfree(home);
   xfree(user);
   xfree(pass);
   xfree(hostname);
   xfree(portname);
   xfree(url);
   xfree(closure);
   xfree(location);
   xfree(entity_content_type);
   xfree(entity_charset);
   for(FileAccess **scan=&chain; *scan; scan=&((*scan)->next))
   {
      if(*scan==this)
      {
	 *scan=next;
	 break;
      }
   }
}

void  FileAccess::DebugPrint(const char *prefix,const char *str,int level)
{
   if(!Log::global)
      return;
   int len=strlen(str);
   if(len>0 && str[len-1]=='\n')
      len--;
   if(len>0 && str[len-1]=='\r')
      len--;
   char *buf=string_alloca(strlen(prefix)+len+2);
   sprintf(buf,"%s%.*s\n",prefix,len,str);
   Log::global->Write(level,buf);
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
      event_time=now;
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
	 DebugPrint("**** ",str,0);
	 return 1;
      }
#endif /* SO_ERROR */
      if(pfd[i].revents&POLLERR)
      {
	 DebugPrint("**** ","POLLERR",0);
	 return 1;
      }
   } /* end for */
   return 0;
}

void FileAccess::NonBlock(int fd)
{
   int fl=fcntl(fd,F_GETFL);
   fcntl(fd,F_SETFL,fl|O_NONBLOCK);
}
void FileAccess::CloseOnExec(int fd)
{
   fcntl(fd,F_SETFD,FD_CLOEXEC);
}


void  FileAccess::Open(const char *fn,int mode,off_t offs)
{
#ifdef OPEN_DEBUG
   printf("FA::Open(%s,%d)\n",fn?fn:"NULL",mode);
#endif
   if(IsOpen())
      Close();
   Resume();
   event_time=now;
   file=xstrdup(fn);
   real_pos=-1;
   pos=offs;
   this->mode=mode;
   mkdir_p=false;
   Timeout(0);

   switch((open_mode)mode)
   {
   case STORE:
   case REMOVE:
   case MAKE_DIR:
   case CHANGE_MODE:
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

   if(error)
   {
      if(str_allocated<128+strlen(error))
	 str=(char*)xrealloc(str,str_allocated=128+strlen(error));
   }

   // note to translators: several errors should not be displayed to user;
   // so no need to translate them.
   switch(err)
   {
   case(IN_PROGRESS):
      return("Operation is in progress");
   case(OK):
      return("Error 0");
   case(SEE_ERRNO):
      if(error)
      {
   	 sprintf(str,"%s: %s",error,strerror(saved_errno));
	 return(str);
      }
      return(strerror(saved_errno));
   case(LOOKUP_ERROR):
      return(error);
   case(NOT_OPEN):   // Actually this means an error in application
      return("Class is not Open()ed");
   case(NO_FILE):
      if(error)
      {
   	 sprintf(str,_("Access failed: %s"),error);
	 return(str);
      }
      return(_("File cannot be accessed"));
   case(NO_HOST):
      return(_("Not connected"));
   case(FATAL):
      if(error)
      {
   	 sprintf(str,_("Fatal error: %s"),error);
	 return(str);
      }
      return(_("Fatal error"));
   case(STORE_FAILED):
      return(_("Store failed - you have to reput"));
   case(LOGIN_FAILED):
      if(error)
      {
   	 sprintf(str,"%s: %s",_("Login failed"),error);
	 return(str);
      }
      return(_("Login failed"));
   case(NOT_SUPP):
      if(error)
      {
   	 sprintf(str,"%s: %s",_("Operation not supported"),error);
	 return(str);
      }
      return(_("Operation not supported"));
   case(FILE_MOVED):
      if(error)
      {
   	 sprintf(str,_("File moved: %s"),error);
      }
      else
      {
	 if(str_allocated<64+xstrlen(location))
	    str=(char*)xrealloc(str,64+xstrlen(location));
	 sprintf(str,_("File moved to `%s'"),location?location:"?");
      }
      return str;
   }
   return("");
}

void FileAccess::Close()
{
   xfree(file); file=0;
   xfree(file_url); file_url=0;
   xfree(file1); file1=0;
   mode=CLOSED;
   opt_date=0;
   opt_size=0;
   array_for_info=0;
   array_ptr=array_cnt=0;
   entity_size=NO_SIZE;
   entity_date=NO_DATE;
   ascii=false;
   norest_manual=false;
   xfree(location); location=0;
   xfree(entity_content_type);
   entity_content_type=0;
   xfree(entity_charset);
   entity_charset=0;
   ClearError();
}

void FileAccess::Rename(const char *f,const char *f1)
{
   Close();
   file1=xstrdup(f1);
   Open(f,RENAME);

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

const char *FileAccess::GetFileURL(const char *f,int flags)
{
   const char *proto=GetVisualProto();
   if(proto[0]==0)
      return "";

   ParsedURL u("");

   u.proto=(char*)proto;
   if(!(flags&NO_USER))
      u.user=user;
   if((pass_open || (flags&WITH_PASSWORD)) && !(flags&NO_PASSWORD))
      u.pass=pass;
   u.host=hostname;
   u.port=portname;
   if(!(flags&NO_PATH))
   {
      if(!f || (f[0]!='/' && f[0]!='~'))
	 f=dir_file(cwd?cwd:"~",f);
      u.path=(char*)f;
   }

   xfree(url);
   url=u.Combine(home);

   return url;
}

const char *FileAccess::GetConnectURL(int flags)
{
   return GetFileURL(0,flags);
}

void FileAccess::Connect(const char *host1,const char *port1)
{
   Close();
   xfree(hostname);
   hostname=xstrdup(host1);
   xfree(portname);
   portname=xstrdup(port1);
   DontSleep();
   ResetLocationData();
}

void FileAccess::Login(const char *user1,const char *pass1)
{
   Close();
   xfree(user);
   user=xstrdup(user1);
   xfree(pass);
   pass=xstrdup(pass1);
   pass_open=false;

   if(user && pass==0)
   {
      for(FileAccess *o=chain; o!=0; o=o->next)
      {
	 pass=o->pass;
	 if(SameSiteAs(o) && o->pass)
	 {
	    pass=xstrdup(o->pass);
	    break;
	 }
	 pass=0;
      }
      if(pass==0 && hostname) // still no pass? Try .netrc
      {
	 NetRC::Entry *nrc=NetRC::LookupHost(hostname,user);
	 if(nrc)
	    pass=xstrdup(nrc->pass);
      }
   }
   ResetLocationData();
}

void FileAccess::AnonymousLogin()
{
   Close();
   xfree(user); user=0;
   xfree(pass); pass=0;
   pass_open=false;
   ResetLocationData();
}

void FileAccess::ResetLocationData()
{
   xfree(cwd);  cwd=xstrdup(default_cwd);
   xfree(home); home=0;
}

void FileAccess::SetPasswordGlobal(const char *p)
{
   xfree(pass);
   pass=xstrdup(p);
   for(FileAccess *o=chain; o!=0; o=o->next)
   {
      if(o==this)
	 continue;
      char *save_pass=o->pass;	 // cheat SameSiteAs.
      o->pass=pass;
      if(SameSiteAs(o))
      {
	 xfree(save_pass);
	 o->pass=xstrdup(pass);
      }
      else
	 o->pass=save_pass;
   }
}

void FileAccess::GetInfoArray(struct fileinfo *info,int count)
{
   Open(0,ARRAY_INFO);
   array_for_info=info;
   array_ptr=0;
   array_cnt=count;
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
      if(file)
	 expand_tilde(&file,home);
      if(file1)
	 expand_tilde(&file1,home);
      if(mode==CHANGE_DIR)
	 OptimizePath(file);
   }
}

const char *FileAccess::ExpandTildeStatic(const char *s)
{
   if(!home || !(s[0]=='~' && (s[1]=='/' || s[1]==0)))
      return s;

   static char *buf=0;
   static int buf_len=0;

   int len=strlen(s)+1;
   if(len>buf_len)
      buf=(char*)xrealloc(buf,buf_len=len);
   strcpy(buf,s);
   expand_tilde(&buf,home);
   return buf;
}

static inline
int last_element_is_doubledot(char *path,char *end)
{
   return((end==path+2 && !strncmp(path,"..",2))
        || (end>path+2 && !strncmp(end-3,"/..",3)));
}

int FileAccess::device_prefix_len(const char *path)
{
   ResValue dp=Query("device-prefix",hostname);
   if(dp.is_nil() || !dp.to_bool())
      return 0;
   int i=0;
   while(path[i] && (is_ascii_alnum(path[i]) || strchr("$_-",path[i])))
      i++;
   if(i>0 && path[i]==':')
      return i+1+(path[i+1]=='/');
   return 0;
}

void FileAccess::OptimizePath(char *path)
{
   int	 prefix_size=0;

   if(path[0]=='/')
   {
      prefix_size=1;
      if(path[1]=='/' && (!path[2] || path[2]!='/'))
	 prefix_size=2;
   }
   else if(path[0]=='~')
   {
      prefix_size=1;
      while(path[prefix_size]!='/' && path[prefix_size]!='\0')
	 prefix_size++;
   }
   else
   {
      // handle VMS and DOS devices.
      prefix_size=device_prefix_len(path);
   }

   char	 *in;
   char	 *out;

   in=out=path+prefix_size;

   while((in[0]=='.' && (in[1]=='/' || in[1]==0))
   || (in>path && in[-1]=='/' && (in[0]=='/'
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
	 // double slash
	 if(in[1]=='/') // || (strip_trailing_slash && in[1]=='\0'))
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
	       if(last_element_is_doubledot(path+prefix_size,out)
	       || out==path
	       || (out==path+prefix_size && out[-1]!='/'))
	       {
		  if(out>path && out[-1]!='/')
		     *out++='/';
		  *out++='.';
		  *out++='.';
	       }
	       else
	       {
		  while(out>path+prefix_size && *--out!='/')
		     ;
	       }
	       in+=3;
	       continue;
	    }
	 }
	 // don't add slash after prefix with slash
	 if(out>path && out[-1]=='/')
	 {
	    in++;
	    continue;
	 }
      }
      if((*out++=*in++)=='\0')
	 break;
   }
}

void FileAccess::Chdir(const char *path,bool verify)
{
   ExpandTildeInCWD();

   char	 *newcwd=(char*)alloca(xstrlen(cwd)+strlen(path)+2);

   if(path[0]=='/')
      strcpy(newcwd,path);
   else if(path[0]=='~')
      strcpy(newcwd,path);
   else if(device_prefix_len(path)>0)
      strcpy(newcwd,path);
   else
   {
      if(cwd && cwd[0])
      {
	 if(last_char(cwd)=='/')
	    sprintf(newcwd,"%s%s",cwd,path);
	 else
	    sprintf(newcwd,"%s/%s",cwd,path);
      }
      else
	 strcpy(newcwd,path);
   }

   OptimizePath(newcwd);
   strip_trailing_slashes(newcwd);

   if(verify)
      Open(newcwd,CHANGE_DIR);
   else
      SetCwd(newcwd);
}

void FileAccess::Chmod(const char *file,int m)
{
   chmod_mode=m;
   Open(file,CHANGE_MODE);
}

void FileAccess::SetError(int ec,const char *e)
{
   if(ec==SEE_ERRNO)
      saved_errno=errno;
   if(ec==NO_FILE && file && file[0] && !strstr(e,file))
   {
      char *m=string_alloca(strlen(e)+2+strlen(file)+2);
      sprintf(m,"%s (%s)",e,file);
      e=m;
   }
   xfree(error);
   error=xstrdup(e);
   error_code=ec;
}

void FileAccess::ClearError()
{
   error_code=OK;
   xfree(error);
   error=0;
}

void FileAccess::Fatal(const char *e)
{
   SetError(FATAL,e);
}

void FileAccess::SetSuggestedFileName(const char *fn)
{
   xfree(suggested_filename);
   suggested_filename=0;
   if(fn==0)
      return;

   // don't allow subdirectories.
   if(strchr(fn,'/') || strchr(fn,'\\'))
      return;
   for(int i=0; fn[i]; i++)
   {
      // don't allow control chars.
      if(iscntrl((unsigned char)fn[i]))
	 return;
   }
   if(!*fn)
      return;
   suggested_filename=xstrdup(fn);
}

FileAccess *SessionPool::pool[pool_size];

void SessionPool::Reuse(FileAccess *f)
{
   if(f==0)
      return;
   if(f->GetHostName()==0)
   {
      SMTask::Delete(f);
      return;
   }
   f->Close();
   f->SetPriority(0);
   int i;
   for(i=0; i<pool_size; i++)
   {
      assert(pool[i]!=f);
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
	 SMTask::Delete(pool[i]);
	 pool[i]=f;
	 return;
      }
   }
   SMTask::Delete(f);
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

void SessionPool::ClearAll()
{
   for(int n=0; n<pool_size; n++)
   {
      if(pool[n])
      {
	 SMTask::Delete(pool[n]);
	 pool[n]=0;
      }
   }
}

bool FileAccess::NotSerious(int e)
{
   switch(e)
   {
   case(EPIPE):
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

void FileAccess::BumpEventTime(time_t t)
{
   if(event_time<t)
      event_time=t;
}
void FileAccess::SetTryTime(time_t t)
{
   try_time=t;
}

ResValue FileAccess::Query(const char *name,const char *closure)
{
   const char *prefix=res_prefix;
   if(!prefix)
      prefix=GetProto();
   char *fullname=(char*)alloca(3+strlen(prefix)+1+strlen(name)+1);
   sprintf(fullname,"%s:%s",prefix,name);
   return ResMgr::Query(fullname,closure);
}

bool FileAccess::IsBetterThan(FileAccess *fa)
{
   return(SameProtoAs(fa) && this->IsConnected() > fa->IsConnected());
}

void FileAccess::Reconfig(const char *) {}
void FileAccess::ConnectVerify() {}
const char *FileAccess::CurrentStatus() { return ""; }
int FileAccess::Buffered() { return 0; }
bool FileAccess::IOReady() { return IsOpen(); }
int FileAccess::IsConnected() { return 0; }
void FileAccess::Disconnect() {}
void FileAccess::UseCache(bool) {}
bool FileAccess::NeedSizeDateBeforehand() { return false; }
void FileAccess::Cleanup() {}
void FileAccess::CleanupThis() {}
ListInfo *FileAccess::MakeListInfo(const char *path) { return 0; }
Glob *FileAccess::MakeGlob(const char *pattern) { return 0; }
DirList *FileAccess::MakeDirList(ArgV *a) { if(a) delete a; return 0; }

DirList::~DirList()
{
   delete buf;
   if(args)
      delete args;
}

void FileAccess::CleanupAll()
{
   for(FileAccess *o=chain; o!=0; o=o->next)
   {
      Enter(o);
      o->CleanupThis();
      Leave(o);
   }
}

FileAccess *FileAccess::NextSameSite(FA *scan)
{
   if(scan==0)
      scan=chain;
   else
      scan=scan->next;
   for( ; scan; scan=scan->next)
      if(scan!=this && SameSiteAs(scan))
	 return scan;
   return 0;
}

FileAccess *FileAccess::New(const char *proto,const char *host)
{
   if(!strcmp(proto,"slot"))
   {
      FA *session=ConnectionSlot::FindSession(host);
      return session?session->Clone():0;
   }

   FA *session=Protocol::NewSession(proto);
   if(!session)
      return 0;

   const char *n_proto=session->ProtocolSubstitution(host);
   if(n_proto && strcmp(n_proto,proto))
   {
      FA *n_session=Protocol::NewSession(n_proto);
      if(n_session)
      {
	 delete session;
	 session=n_session;
	 session->SetVisualProto(proto);
      }
   }

   return session;
}
FileAccess *FileAccess::New(const ParsedURL *u,bool dummy)
{
   FileAccess *s=New(u->proto,u->host);
   if(!s)
   {
      if(!dummy)
	 return 0;
      return new DummyNoProto(u->proto);
   }
   if(strcmp(u->proto,"slot"))
      s->Connect(u->host,u->port);
   if(u->user)
      s->Login(u->user,u->pass);
   // path?
   return s;
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
      if(!strcasecmp(scan->proto,proto))
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
void FileAccessOperation::SetErrorCached(const char *e)
{
   xfree(error_text);
   error_text=xasprintf(_("%s [cached]"),e);
   done=true;
}


// ListInfo implementation
ListInfo::ListInfo(FileAccess *s,const char *p)
{
   session=s;
   saved_cwd=0;
   result=0;

   exclude=0;
   exclude_prefix=0;

   need=0;

   follow_symlinks=false;

   if(session && p)
   {
      saved_cwd=xstrdup(session->GetCwd());
      session->Chdir(p,false);
   }
}

ListInfo::~ListInfo()
{
   if(session)
      session->Close();
   if(session && saved_cwd)
      session->SetCwd(saved_cwd);
   delete result;
   xfree(saved_cwd);
}

void ListInfo::SetExclude(const char *p,PatternSet *x)
{
   exclude=x;
   exclude_prefix=p;
}

#include "modconfig.h"
#ifndef MODULE_PROTO_FTP
# include "ftpclass.h"
# define _ftp Ftp::ClassInit()
#else
# define _ftp
#endif
#ifndef MODULE_PROTO_FILE
# include "LocalAccess.h"
# define _file LocalAccess::ClassInit()
#else
# define _file
#endif
#ifndef MODULE_PROTO_HTTP
# include "Http.h"
# define _http Http::ClassInit()
#else
# define _http
#endif
#ifndef MODULE_PROTO_FISH
# include "Fish.h"
# define _fish Fish::ClassInit()
#else
# define _fish
#endif
#ifndef MODULE_PROTO_SFTP
# include "SFtp.h"
# define _sftp SFtp::ClassInit()
#else
# define _sftp
#endif
void FileAccess::ClassInit()
{
   _ftp;
   _file;
   _http;
   _fish;
   _sftp;
}
