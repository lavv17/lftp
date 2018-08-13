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

#include "FileAccess.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <ctype.h>
#include <assert.h>
#include <stddef.h>
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

xlist_head<FileAccess> FileAccess::all_fa;
const FileAccessRef FileAccessRef::null;

void FileAccess::Init()
{
   ClassInit();

   pass_open=false;

   default_cwd="~";
   cwd.Set(default_cwd,false,0);
   limit=FILE_END;
   real_pos=UNKNOWN_POS;
   pos=0;
   mode=CLOSED;
   retries=0;
   max_retries=0;
   opt_date=0;
   opt_size=0;
   fileset_for_info=0;
   error_code=OK;
   saved_errno=0;
   mkdir_p=false;
   rename_f=false;
   ascii=false;
   norest_manual=false;

   entity_size=NO_SIZE;
   entity_date=NO_DATE;

   res_prefix=0;

   chmod_mode=0644;

   priority=last_priority=0;

   all_fa.add(all_fa_node);
}

FileAccess::FileAccess(const FileAccess *fa)
   : all_fa_node(this)
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
   all_fa_node.remove();
}

void  FileAccess::Open(const char *fn,int mode,off_t offs)
{
#ifdef OPEN_DEBUG
   printf("%p->FA::Open(%s,%d)\n",this,fn?fn:"NULL",mode);
#endif
   if(IsOpen())
      Close();
   Resume();
   file.set(fn);
   real_pos=UNKNOWN_POS;
   pos=offs;
   this->mode=mode;
   mkdir_p=false;
   rename_f=false;
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
   new_cwd=0;
   mode=CLOSED;
   opt_date=0;
   opt_size=0;
   fileset_for_info=0;
   retries=0;
   entity_size=NO_SIZE;
   entity_date=NO_DATE;
   ascii=false;
   norest_manual=false;
   location.set(0);
   entity_content_type.set(0);
   entity_charset.set(0);
   ClearError();
}

void FileAccess::Open2(const char *f,const char *f1,open_mode o)
{
   Close();
   file1.set(f1);
   Open(f,o);

   cache->TreeChanged(this,file);
   cache->FileChanged(this,file);
   cache->FileChanged(this,file1);
}

void FileAccess::Rename(const char *rfile,const char *to,bool clobber)
{
   Open2(rfile,to,RENAME);
   rename_f=clobber;
}

void FileAccess::Mkdir(const char *fn,bool allp)
{
   Open(fn,MAKE_DIR);
   mkdir_p=allp;
}

StringSet *FileAccess::MkdirMakeSet() const
{
   StringSet *set=new StringSet;
   const char *sl=strchr(file,'/');
   while(sl)
   {
      if(sl>file)
      {
	 xstring& tmp=xstring::get_tmp(file,sl-file);
	 if(tmp.ne(".") && tmp.ne(".."))
	    set->Append(tmp);
      }
      sl=strchr(sl+1,'/');
   }
   return set;
}

bool FileAccess::SameLocationAs(const FileAccess *fa) const
{
   return SameSiteAs(fa);
}
bool FileAccess::SameSiteAs(const FileAccess *fa) const
{
   return SameProtoAs(fa);
}

const xstring& FileAccess::GetFileURL(const char *f,int flags) const
{
   const char *proto=GetVisualProto();
   if(proto[0]==0)
      return xstring::get_tmp("");

   ParsedURL u;

   u.proto.set(proto);
   if(!(flags&NO_USER))
      u.user.set(user);
   if((pass_open || (flags&WITH_PASSWORD)) && !(flags&NO_PASSWORD))
      u.pass.set(pass);
   u.host.set(hostname);
   u.port.set(portname);
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
	    return u.CombineTo(xstring::get_tmp(""),home)
	       .append(f_path.url+f_path_index);
	 }
      }

      bool is_dir=((!f || !*f) && !cwd.is_file);

      if(!f || (f[0]!='/' && f[0]!='~'))
	 f=dir_file(cwd.path?cwd.path.get():"~",f);
      u.path.set(f);
      if(is_dir && url::dir_needs_trailing_slash(proto) && u.path.last_char()!='/')
	 u.path.append('/');
   }
   return u.CombineTo(xstring::get_tmp(""),home);
}

const xstring& FileAccess::GetConnectURL(int flags) const
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
      xlist_for_each(FileAccess,all_fa,node,o)
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

void FileAccess::ResetLocationData()
{
   cwd.Set(default_cwd,false,0);
   home.Set((char*)0);
}

void FileAccess::SetPasswordGlobal(const char *p)
{
   pass.set(p);
   xstring save_pass;
   xlist_for_each(FileAccess,all_fa,node,o)
   {
      if(o==this)
	 continue;
      save_pass.set(o->pass);	 // cheat SameSiteAs.
      o->pass.set(pass);
      if(!SameSiteAs(o))
	 o->pass.set(save_pass);
   }
}

void FileAccess::GetInfoArray(FileSet *info)
{
   Open(0,ARRAY_INFO);
   fileset_for_info=info;
   fileset_for_info->rewind();
}

static void expand_tilde(xstring &path, const char *home, int i=0)
{
   if(!(path[i]=='~' && (path[i+1]==0 || path[i+1]=='/')))
      return;
   char prefix_len=(last_char(home)=='/' ? 2 : 1);
   if(home[0]=='/' && i>0 && path[i-1]=='/')
      home++;
   path.set_substr(i,prefix_len,home);
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
void FileAccess::set_home(const char *h)
{
   home.Set(h);
   ExpandTildeInCWD();
}
const char *FileAccess::ExpandTildeStatic(const char *s) const
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

int FileAccess::device_prefix_len(const char *path) const
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

void FileAccess::Path::Optimize(xstring& path,int device_prefix_len)
{
   int prefix_size=0;

   if(path[0]=='/' && path[1]=='~' && device_prefix_len==1)
   {
      prefix_size=2;
      while(path[prefix_size]!='/' && path[prefix_size]!='\0')
	 prefix_size++;
   }
   else if(path[0]=='/')
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

   in=out=path.get_non_const()+prefix_size;

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

   while(*in)
   {
      if(in[0]=='/')
      {
	 // double slash
	 if(in[1]=='/')
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
      *out++=*in++;
   }
   path.truncate(path.length()-(in-out));
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
   if(ec==SEE_ERRNO && !saved_errno)
      saved_errno=errno;
   if(ec==NO_FILE && file && file[0] && !strstr(e,file))
      error.vset(e," (",file.get(),")",NULL);
   else
      error.set(e);
   error_code=ec;
}

void FileAccess::ClearError()
{
   saved_errno=0;
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
   if(strchr(fn,'/') || strchr(fn,'\\') || strchr(fn,':'))
      return;
   for(int i=0; fn[i]; i++)
   {
      // don't allow control chars.
      if(iscntrl((unsigned char)fn[i]))
	 return;
   }
   if(!*fn || *fn=='.')
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
      fprintf(f,"%d\t%s\n",arr[i],pool[arr[i]]->GetConnectURL().get());
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
   int pass=0;
   for(;;) {
      int left=0;
      for(int n=0; n<pool_size; n++) {
	 if(!pool[n])
	    continue;
	 if(pass==0)
	    pool[n]->Disconnect();
	 if(!pool[n]->IsConnected()) {
	    SMTask::Delete(pool[n]);
	    pool[n]=0;
	 } else {
	    left++;
	 }
      }
      if(left==0)
	 break;
      SMTask::Schedule();
      SMTask::Block();
      pass++;
   }
}

void FileAccess::SetTryTime(time_t t)
{
   if(t)
      reconnect_timer.Reset(Time(t));
   else
      reconnect_timer.Stop();
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
void FileAccess::UseCache(bool) {}
bool FileAccess::NeedSizeDateBeforehand() { return false; }
void FileAccess::Cleanup() {}
void FileAccess::CleanupThis() {}
ListInfo *FileAccess::MakeListInfo(const char *path) { return 0; }
Glob *FileAccess::MakeGlob(const char *pattern) { return new NoGlob(pattern); }
DirList *FileAccess::MakeDirList(ArgV *a) { delete a; return 0; }

void FileAccess::CleanupAll()
{
   xlist_for_each(FileAccess,all_fa,node,o)
   {
      Enter(o);
      o->CleanupThis();
      Leave(o);
   }
}

FileAccess *FileAccess::NextSameSite(FA *scan) const
{
   if(scan==0)
      scan=all_fa.first_obj();
   else
      scan=scan->all_fa_node.next_obj();
   for( ; scan; scan=scan->all_fa_node.next_obj())
      if(scan!=this && SameSiteAs(scan))
	 return scan;
   return 0;
}

FileAccess *FileAccess::New(const char *proto,const char *host,const char *port)
{
   ClassInit();

   if(proto==0)
      proto="file";

   if(!strcmp(proto,"slot"))
   {
      const FA *session=ConnectionSlot::FindSession(host);
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
	 Delete(session);
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
   const char *proto=u->proto?u->proto.get():"file";
   FileAccess *s=New(proto,u->host);
   if(!s)
   {
      if(!dummy)
	 return 0;
      return new DummyNoProto(proto);
   }
   if(strcmp(proto,"slot"))
      s->Connect(u->host,u->port);
   if(u->user)
      s->Login(u->user,u->pass);
   // path?
   return s;
}

FileAccess *FileAccess::GetNewLocationFA() const
{
   if(!location)
      return 0;
   ParsedURL url(location,true);
   if(!url.proto)
      return 0;
   return FileAccess::New(&url,true);
}


// FileAccess::Protocol implementation
xmap_p<FileAccess::Protocol> FileAccess::Protocol::proto_by_name;

FileAccess::Protocol::Protocol(const char *proto, SessionCreator *creator)
{
   this->proto=proto;
   this->New=creator;
   proto_by_name.add(proto,this);
}

FileAccess::Protocol *FileAccess::Protocol::FindProto(const char *proto)
{
   return proto_by_name.lookup(proto);
}

FileAccess *FileAccess::Protocol::NewSession(const char *proto)
{
   Protocol *p;

   p=FindProto(proto);
   if(p)
      return p->New();

#ifdef WITH_MODULES
#define PROTO_PREFIX "proto-"
   const char *mod=xstring::cat(PROTO_PREFIX,proto,NULL);
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

DirList::DirList(FileAccess *s,ArgV *a)
   : FileAccessOperation(s), buf(new Buffer()), args(a), color(false)
{
}
DirList::~DirList()
{
}

// ListInfo implementation
ListInfo::ListInfo(FileAccess *s,const char *p)
   : FileAccessOperation(s), exclude_prefix(0), exclude(0), need(0),
   follow_symlinks(false), try_recursive(false), is_recursive(false)
{
   if(session && p)
   {
      saved_cwd=session->GetCwd();
      session->Chdir(p,false);
   }
}

void ListInfo::PrepareToDie()
{
   if(session)
      session->Close();
   if(session && saved_cwd)
      session->SetCwd(saved_cwd);
}
ListInfo::~ListInfo() {}


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
      new_path=url::decode(new_path_enc);
   if(!new_path || !*new_path)
      return;
   const char *bn=basename_ptr(new_path);
   if(!strcmp(bn,".") || !strcmp(bn,".."))
      new_is_file=false;

   int path_index=0;
   if(url)
   {
      path_index=url::path_index(url);
      xstring new_url_path(url+path_index);
      if(is_file)
      {
	 dirname_modify(new_url_path);
	 if(!new_url_path[0])
	    new_url_path.set("/~");
      }
      if(new_url_path.last_char()!='/')
	 new_url_path.append('/');
      if(new_path[0]=='/' || new_path[0]=='~' || new_device_prefix_len!=0)
      {
	 bool have_slash=((new_path_enc?new_path_enc:new_path)[0]=='/');
	 new_url_path.set(have_slash?"":"/");
      }
      if(new_path_enc)
	 new_url_path.append(new_path_enc);
      else
	 new_url_path.append(url::encode(new_path,URL_PATH_UNSAFE));
      if(!new_is_file && url::dir_needs_trailing_slash(url) && new_url_path.last_char()!='/')
	 new_url_path.append('/');
      Optimize(new_url_path,!strncmp(new_url_path,"/~",2));
      url.truncate(path_index);
      url.append(new_url_path);
   }

   if(new_path[0]!='/' && new_path[0]!='~' && new_device_prefix_len==0
   && path && path[0])
   {
      if(is_file)
      {
	 dirname_modify(path);
	 if(!path[0])
	    path.set("~");
      }
      if(last_char(path)=='/')
	 new_path=xstring::format("%s%s",path.get(),new_path);
      else
	 new_path=xstring::format("%s/%s",path.get(),new_path);
   }
   path.set(new_path);
   device_prefix_len=new_device_prefix_len;
   Optimize();
   strip_trailing_slashes(path);
   is_file=new_is_file;
   if(!strcmp(path,"/") || !strcmp(path,"//"))
      is_file=false;

   // sanity check
   if(url)
   {
      ParsedURL u(url);
      if(u.path.length()>1)
	 u.path.chomp('/');
      if(!u.path.eq(path))
      {
	 LogError(0,"URL mismatch %s [%s] vs %s, dropping URL\n",url.get(),u.path.get(),path.get());
	 url.set(0);
      }
   }
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
   if(url)
   {
      int pi=url::path_index(url);
      if(url[pi]=='/' && url[pi+1]=='~')
	 pi++;
      expand_tilde(url,home.url?home.url.get():url::encode(home.path,URL_PATH_UNSAFE).get(),pi);
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

   if(!Log::global)
      Log::global=new Log("debug");

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
   Protocol::ClassCleanup();
   call_dynamic_hook("lftp_network_cleanup");
   DirColors::DeleteInstance();
   delete cache;
   cache=0;
   FileCopy::fxp_create=0;
}

const FileAccessRef& FileAccessRef::operator=(FileAccess *p)
{
   reuse();
   ptr=SMTask::MakeRef(p);
   return *this;
}

// hook-up gnulib...
CDECL_BEGIN
#include "md5.h"
#include "glob.h"
CDECL_END
void *_md5_hook=(void*)md5_init_ctx;
void *_glob_hook=(void*)glob;
