/*
 * lftp and utils
 *
 * Copyright (c) 1996-2005 by Alexander V. Lukyanov (lav@yars.free.net)
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
#include "SignalHook.h"
#include "FileGlob.h"
#ifdef WITH_MODULES
# include "module.h"
#endif

FileAccess *FileAccess::chain=0;
const FileAccessRef FileAccessRef::null;

void FileAccess::Init()
{
   ClassInit();

   pass_open=false;

   default_cwd="~";
   cwd.Set(default_cwd,false,0);
   new_cwd=0;
   limit=FILE_END;
   real_pos=UNKNOWN_POS;
   pos=0;
   mode=CLOSED;
   try_time=0;
   retries=0;
   opt_date=0;
   opt_size=0;
   error_code=OK;
   saved_errno=0;
   mkdir_p=false;
   ascii=false;
   norest_manual=false;
   array_ptr=array_cnt=0;

   entity_size=NO_SIZE;
   entity_date=NO_DATE;

   res_prefix=0;

   chmod_mode=0644;

   priority=last_priority=0;

   next=chain;
   chain=this;
}

FileAccess::FileAccess(const FileAccess *fa)
{
   Init();

   cwd=fa->cwd;
   home=fa->home;
   user.set(fa->user);
   pass.set(fa->pass);
   pass_open=fa->pass_open;
   hostname.set(fa->hostname);
   portname.set(fa->portname);
   vproto.set(fa->vproto);
}

FileAccess::~FileAccess()
{
   ListDel(FileAccess,chain,this,next);
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
   file.set(fn);
   real_pos=UNKNOWN_POS;
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
      cache->FileChanged(this,file);
      break;
   case REMOVE_DIR:
      cache->FileChanged(this,file);
      cache->TreeChanged(this,file);
      break;
   default:
      break;
   }
}

const char *FileAccess::StrError(int err)
{
   static xstring str;

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
   	 return str.vset(error.get(),": ",strerror(saved_errno),NULL);
      return(strerror(saved_errno));
   case(LOOKUP_ERROR):
      return(error);
   case(NOT_OPEN):   // Actually this means an error in application
      return("Class is not Open()ed");
   case(NO_FILE):
      if(error)
   	 return str.vset(_("Access failed: "),error.get(),NULL);
      return(_("File cannot be accessed"));
   case(NO_HOST):
      return(_("Not connected"));
   case(FATAL):
      if(error)
   	 return str.vset(_("Fatal error"),": ",error.get(),NULL);
      return(_("Fatal error"));
   case(STORE_FAILED):
      return(_("Store failed - you have to reput"));
   case(LOGIN_FAILED):
      if(error)
   	 return str.vset(_("Login failed"),": ",error.get(),NULL);
      return(_("Login failed"));
   case(NOT_SUPP):
      if(error)
   	 return str.vset(_("Operation not supported"),": ",error.get(),NULL);
      return(_("Operation not supported"));
   case(FILE_MOVED):
      if(error)
   	 return str.vset(_("File moved"),": ",error.get(),NULL);
      else
   	 return str.vset(_("File moved to `"),location?location.get():"?","'",NULL);
   }
   return("");
}

void FileAccess::Close()
{
   file.set(0);
   file_url.set(0);
   file1.set(0);
   delete new_cwd; new_cwd=0;
   mode=CLOSED;
   opt_date=0;
   opt_size=0;
   array_for_info=0;
   array_ptr=array_cnt=0;
   entity_size=NO_SIZE;
   entity_date=NO_DATE;
   ascii=false;
   norest_manual=false;
   location.set(0);
   entity_content_type.set(0);
   entity_charset.set(0);
   ClearError();
}

void FileAccess::Rename(const char *f,const char *f1)
{
   Close();
   file1.set(f1);
   Open(f,RENAME);

   cache->TreeChanged(this,file);
   cache->FileChanged(this,file);
   cache->FileChanged(this,file1);
}

void FileAccess::Mkdir(const char *fn,bool allp)
{
   Open(fn,MAKE_DIR);
   mkdir_p=allp;
}

bool FileAccess::SameLocationAs(const FileAccess *fa) const
{
   return SameSiteAs(fa);
}
bool FileAccess::SameSiteAs(const FileAccess *fa) const
{
   return SameProtoAs(fa);
}

const char *FileAccess::GetFileURL(const char *f,int flags) const
{
   static xstring url;

   const char *proto=GetVisualProto();
   if(proto[0]==0)
      return "";

   ParsedURL u("");

   u.proto=(char*)proto;
   if(!(flags&NO_USER))
      u.user=(char*)user.get();
   if((pass_open || (flags&WITH_PASSWORD)) && !(flags&NO_PASSWORD))
      u.pass=(char*)pass.get();
   u.host=(char*)hostname.get();
   u.port=(char*)portname.get();
   if(!(flags&NO_PATH))
   {
      if(cwd.url)
      {
	 Path f_path(cwd);
	 if(f)
	    f_path.Change(f,true);
	 if(f_path.url)
	 {
	    int f_path_index=url::path_index(f_path.url);
	    url.set_allocated(u.Combine(home));
	    url.append(f_path.url+f_path_index);
	    return url;
	 }
      }

      if(!f || (f[0]!='/' && f[0]!='~'))
	 f=dir_file(cwd.path?cwd.path.get():"~",f);
      u.path=(char*)f;
   }
   return url.set_allocated(u.Combine(home));
}

const char *FileAccess::GetConnectURL(int flags) const
{
   return GetFileURL(0,flags);
}

void FileAccess::Connect(const char *host1,const char *port1)
{
   Close();
   hostname.set(host1);
   portname.set(port1);
   DontSleep();
   ResetLocationData();
}

void FileAccess::Login(const char *user1,const char *pass1)
{
   Close();
   user.set(user1);
   pass.set(pass1);
   pass_open=false;

   if(user && pass==0)
   {
      FileAccess *o;
      for(o=chain; o!=0; o=o->next)
      {
	 pass.set(o->pass);
	 if(SameSiteAs(o) && o->pass)
	    break;
      }
      if(!o)
	 pass.set(0);
      if(pass==0 && hostname) // still no pass? Try .netrc
      {
	 NetRC::Entry *nrc=NetRC::LookupHost(hostname,user);
	 if(nrc)
	    pass.set(nrc->pass);
      }
   }
   ResetLocationData();
}

void FileAccess::AnonymousLogin()
{
   Close();
   user.set(0);
   pass.set(0);
   pass_open=false;
   ResetLocationData();
}

void FileAccess::ResetLocationData()
{
   cwd.Set(default_cwd,false,0);
   home.Set((char*)0);
}

void FileAccess::SetPasswordGlobal(const char *p)
{
   pass.set(p);
   xstring save_pass;
   for(FileAccess *o=chain; o!=0; o=o->next)
   {
      if(o==this)
	 continue;
      save_pass.set(o->pass);	 // cheat SameSiteAs.
      o->pass.set(pass);
      if(!SameSiteAs(o))
	 o->pass.set(save_pass);
   }
}

void FileAccess::GetInfoArray(struct fileinfo *info,int count)
{
   Open(0,ARRAY_INFO);
   array_for_info=info;
   array_ptr=0;
   array_cnt=count;
}

static void expand_tilde(xstring &path, const char *home)
{
   if(path[0]=='~' && (path[1]==0 || path[1]=='/'))
   {
      const char *saved_path=alloca_strdup(path+1);
      if(home[0]=='/' && home[1]==0 && saved_path[0]=='/')
	 path.set(saved_path);
      else
	 path.vset(home,saved_path,NULL);
   }
}

void  FileAccess::ExpandTildeInCWD()
{
   if(home)
   {
      cwd.ExpandTilde(home);
      if(new_cwd)
	 new_cwd->ExpandTilde(home);
      if(real_cwd)
	 expand_tilde(real_cwd,home);
      if(file)
	 expand_tilde(file,home);
      if(file1)
	 expand_tilde(file1,home);
   }
}

const char *FileAccess::ExpandTildeStatic(const char *s)
{
   if(!home || !(s[0]=='~' && (s[1]=='/' || s[1]==0)))
      return s;

   static xstring buf;
   buf.set(s);
   expand_tilde(buf,home);
   return buf;
}

static inline
bool last_element_is_doubledot(const char *path,const char *end)
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

void FileAccess::Path::Optimize(char *path,int device_prefix_len)
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
      prefix_size=device_prefix_len;
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
   cwd.ExpandTilde(home);

   Close();
   new_cwd=new Path(&cwd);
   new_cwd->Change(path,false);

   if(verify)
      Open(new_cwd->path,CHANGE_DIR);
   else
   {
      cwd.Set(new_cwd);
      delete new_cwd;
      new_cwd=0;
   }
}

void FileAccess::PathVerify(const Path &p)
{
   Close();
   new_cwd=new Path(p);
   Open(new_cwd->path,CHANGE_DIR);
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
      error.vset(e," (",file.get(),")",NULL);
   else
      error.set(e);
   error_code=ec;
}

void FileAccess::ClearError()
{
   error_code=OK;
   error.set(0);
}

void FileAccess::Fatal(const char *e)
{
   SetError(FATAL,e);
}

void FileAccess::SetSuggestedFileName(const char *fn)
{
   suggested_filename.set(0);
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
   suggested_filename.set(fn);
}

void FileAccess::SetFileURL(const char *u)
{
   file_url.set(u);
   if(new_cwd && mode==CHANGE_DIR)
      new_cwd->SetURL(u);
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

void FileAccess::SetTryTime(time_t t)
{
   try_time=t;
}

bool FileAccess::IsBetterThan(const FileAccess *fa) const
{
   return(SameProtoAs(fa) && this->IsConnected() > fa->IsConnected());
}

void FileAccess::Reconfig(const char *) {}
void FileAccess::ConnectVerify() { mode=CONNECT_VERIFY; }
const char *FileAccess::CurrentStatus() { return ""; }
int FileAccess::Buffered() { return 0; }
bool FileAccess::IOReady() { return IsOpen(); }
int FileAccess::IsConnected() const { return 0; }
void FileAccess::Disconnect() {}
void FileAccess::UseCache(bool) {}
bool FileAccess::NeedSizeDateBeforehand() { return false; }
void FileAccess::Cleanup() {}
void FileAccess::CleanupThis() {}
ListInfo *FileAccess::MakeListInfo(const char *path) { return 0; }
Glob *FileAccess::MakeGlob(const char *pattern) { return new NoGlob(pattern); }
DirList *FileAccess::MakeDirList(ArgV *a) { delete a; return 0; }

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

FileAccess *FileAccess::New(const char *proto,const char *host,const char *port)
{
   ClassInit();

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

   if(host)
      session->Connect(host,port);

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
void FileAccessOperation::SetError(const char *e)
{
   error_text.set(e);
   done=true;
}
void FileAccessOperation::SetErrorCached(const char *e)
{
   SetError(e);
   error_text.append(_(" [cached]"));
}


// ListInfo implementation
ListInfo::ListInfo(FileAccess *s,const char *p)
   : FileAccessOperation(s)
{
   exclude=0;
   exclude_prefix=0;

   need=0;

   follow_symlinks=false;

   if(session && p)
   {
      saved_cwd=session->GetCwd();
      session->Chdir(p,false);
   }
}

ListInfo::~ListInfo()
{
   if(session)
      session->Close();
   if(session && saved_cwd)
      session->SetCwd(saved_cwd);
}


// Path implementation
void FileAccess::Path::init()
{
   device_prefix_len=0;
   is_file=false;
}
FileAccess::Path::~Path()
{
}
void FileAccess::Path::Set(const char *new_path,bool new_is_file,const char *new_url,int new_device_prefix_len)
{
   path.set(new_path);
   is_file=new_is_file;
   url.set(new_url);
   device_prefix_len=new_device_prefix_len;
}
void FileAccess::Path::Set(const Path *o)
{
   Set(o->path,o->is_file,o->url,o->device_prefix_len);
}
void FileAccess::Path::Change(const char *new_path,bool new_is_file,const char *new_path_enc,int new_device_prefix_len)
{
   if(!new_path && new_path_enc)
   {
      char *np=alloca_strdup(new_path_enc);
      url::decode_string(np);
      new_path=np;
   }
   if(!new_path || !*new_path)
      return;
   const char *bn=basename_ptr(new_path);
   if(!strcmp(bn,".") || !strcmp(bn,".."))
      new_is_file=false;
   if(url)
   {
      int path_index=url::path_index(url);
      const char *old_url_path=url+path_index;
      char *new_url_path=string_alloca(strlen(old_url_path)+xstrlen(new_path)*3+2);
      strcpy(new_url_path,old_url_path);
      if(is_file)
      {
	 dirname_modify(new_url_path);
	 if(!new_url_path[0])
	    strcpy(new_url_path,"/~");
      }
      if(last_char(new_url_path)!='/')
	 strcat(new_url_path,"/");
      if(new_path[0]!='/' && new_path[0]!='~' && new_device_prefix_len==0)
      {
	 if(new_path_enc)
	    strcat(new_url_path,new_path_enc);
	 else
	 {
	    char *np=alloca_strdup(new_path);
	    Optimize(np);
	    if(np[0]=='.' && np[1]=='.' && (np[2]=='/' || np[2]==0))
	    {
	       url.set(0);
	       goto url_done;
	    }
	    else
	       url::encode_string(np,new_url_path+strlen(new_url_path),URL_PATH_UNSAFE);
	 }
      }
      else
      {
	 if(new_path_enc)
	    strcpy(new_url_path,new_path_enc);
	 else
	    url::encode_string(new_path,new_url_path,URL_PATH_UNSAFE);
      }
      Optimize(new_url_path+(!strncmp(new_url_path,"/~",2)));
      url.truncate(path_index);
      url.append(new_url_path);
   }
url_done:
   if(new_path[0]!='/' && new_path[0]!='~' && new_device_prefix_len==0
   && path && path[0])
   {
      if(is_file)
      {
	 dirname_modify(path.get_non_const());
	 if(!path[0])
	    path.set("~");
      }
      char *newcwd=string_alloca(xstrlen(path)+xstrlen(new_path)+2);
      if(last_char(path)=='/')
	 sprintf(newcwd,"%s%s",path.get(),new_path);
      else
	 sprintf(newcwd,"%s/%s",path.get(),new_path);
      new_path=newcwd;
   }
   path.set(new_path);
   device_prefix_len=new_device_prefix_len;
   Optimize();
   strip_trailing_slashes(path.get_non_const());
   is_file=new_is_file;
   if(!strcmp(path,"/") || !strcmp(path,"//"))
      is_file=false;
}
bool FileAccess::Path::operator==(const Path &p2) const
{
   const Path &p1=*this;
   if(p1.is_file!=p2.is_file)
      return false;
   if(xstrcmp(p1.path,p2.path))
      return false;
   if(xstrcmp(p1.url,p2.url))
      return false;
   return true;
}
void FileAccess::Path::ExpandTilde(const Path &home)
{
   if(!home.path)
      return;
   if(path && path[0]=='~' && (path[1]=='/' || path[1]=='\0'))
   {
      device_prefix_len=home.device_prefix_len;
      if(path[1]=='\0')
	 is_file=home.is_file;
   }
   expand_tilde(path,home.path);
}

#include "DirColors.h"
#include "LocalDir.h"
#include "FileCopy.h"
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
bool FileAccess::class_inited;
LsCache *FileAccess::cache;
void FileAccess::ClassInit()
{
   if(class_inited)
      return;
   class_inited=true;
   cache=new LsCache();

   SignalHook::ClassInit();
   ResMgr::ClassInit();

   _ftp;
   _file;
   _http;
   _fish;
   _sftp;

   // make it link in classes required by modules.
   LocalDirectory ld;
}
void FileAccess::ClassCleanup()
{
   DirColors::DeleteInstance();
   delete cache;
   cache=0;
   FileCopy::fxp_create=0;
}
