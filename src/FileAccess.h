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

#ifndef FILEACCESS_H
#define FILEACCESS_H

#include <sys/types.h>
#include <time.h>

#include "SMTask.h"
#include "trio.h"
#include "xstring.h"
#include "ResMgr.h"
#include "FileSet.h"
#include "ArgV.h"
#include "ProtoLog.h"
#include "xlist.h"
#include "xmap.h"
#include "Timer.h"

#define FILE_END     ((off_t)-1L)
#define UNKNOWN_POS  ((off_t)-1L)

class ListInfo;
class Glob;
class NoGlob;
class DirList;
class FileAccessRef;
class Buffer;

class FileAccess : public SMTask, public ResClient, protected ProtoLog
{
   static bool class_inited;
public:
   static class LsCache *cache;
   enum open_mode
   {
      CLOSED,
      RETRIEVE,
      STORE,
      LONG_LIST,
      LIST,
      MP_LIST,
      CHANGE_DIR,
      MAKE_DIR,
      REMOVE_DIR,
      REMOVE,
      QUOTE_CMD,
      RENAME,
      ARRAY_INFO,
      CONNECT_VERIFY,
      CHANGE_MODE,
      LINK,
      SYMLINK,
   };

   class Path
   {
      void init();
   public:
      int   device_prefix_len;
      xstring path;
      bool  is_file;
      xstring url;
      Path() { init(); }
      Path(const Path *o) { init(); Set(o); }
      Path(const Path &o) { init(); Set(o); }
      Path(const char *new_path) { init(); Set(new_path); }
      Path(const char *new_path,bool new_is_file,const char *new_url=0,int new_device_prefix_len=0)
	 { init(); Set(new_path,new_is_file,new_url,new_device_prefix_len); }
      ~Path();
      void Set(const Path*);
      void Set(const Path &o) { Set(&o); }
      void Set(const char *new_path,bool new_is_file=false,const char *new_url=0,int device_prefix_len=0);
      void SetURL(const char *u) { url.set(u); }
      void Change(const char *new_path,bool new_is_file=false,const char *new_path_enc=0,int device_prefix_len=0);
      const xstring& GetDirectory() const { return is_file?dirname(path):path; }
      void ExpandTilde(const Path &home);
      static void Optimize(xstring& p,int dev_prefix=0);
      void Optimize() { Optimize(path,device_prefix_len); }
      const Path& operator=(const Path &o)
	 {
	    Set(&o);
	    return *this;
	 }
      operator const char *() const { return path; }
      bool operator==(const Path &p2) const;
      bool operator!=(const Path &p2) const { return !(*this==p2); }
   };

protected:
   xstring_c vproto;
   xstring_c hostname;
   xstring_c portname;
   xstring_c user;
   xstring_c pass;
   bool	 pass_open;

   const char *default_cwd;
   Path	 home;
   Path	 cwd;
   Ref<Path> new_cwd;
   xstring file;
   xstring file_url;
   xstring file1;
   int	 mode;
   off_t pos;
   off_t real_pos;
   off_t limit;

   FileTimestamp *opt_date;
   off_t  *opt_size;

   FileSet *fileset_for_info;

   Timer reconnect_timer;
   int retries;
   int max_retries;

   bool	 mkdir_p;
   bool	 rename_f;

   int	 saved_errno;

   void	 ExpandTildeInCWD();
   const char *ExpandTildeStatic(const char *s) const;

   xstring real_cwd;
   void set_real_cwd(const char *c)
      {
	 real_cwd.set(c);
      }
   void set_home(const char *h);

   off_t  entity_size; // size of file to be sent
   time_t entity_date; // date of file to be sent

   xstring_c closure;
   const char *res_prefix;
   const char *ResPrefix() const { return res_prefix?res_prefix:GetProto(); }
   const char *ResClosure() const { return closure?closure.get():GetHostName(); }

   int chmod_mode;
   bool ascii;
   bool norest_manual;
   bool fragile;

   int	priority;   // higher priority can take over other session.
   int	last_priority;

   bool Error() { return error_code!=OK; }
   void ClearError();
   void SetError(int code,const char *mess=0);
   void Fatal(const char *mess);
   xstring error;
   int error_code;

   xstring_c location;
   xstring_c location_file;
   int location_mode;
   bool location_permanent;

   xstring_c suggested_filename;
   void SetSuggestedFileName(const char *fn);

   xstring_c entity_content_type;
   xstring_c entity_charset;

   xstring_c last_disconnect_cause;

   xlist<FileAccess> all_fa_node;
   static xlist_head<FileAccess> all_fa;

   FileAccess *FirstSameSite() const { return NextSameSite(0); }
   FileAccess *NextSameSite(FileAccess *) const;

   StringSet *MkdirMakeSet() const; // splits the path for mkdir -p

   int device_prefix_len(const char *path) const;

   virtual ~FileAccess();

public:
   virtual const char *GetProto() const = 0; // http, ftp, file etc
   bool SameProtoAs(const FileAccess *fa) const { return !strcmp(GetProto(),fa->GetProto()); }
   virtual FileAccess *Clone() const = 0;
   virtual const char *ProtocolSubstitution(const char *host) { return 0; }

   const char *GetVisualProto() const { return vproto?vproto.get():GetProto(); }
   void SetVisualProto(const char *p) { vproto.set(p); }
   const char  *GetHome() const { return home; }
   const char  *GetHostName() const { return hostname; }
   const char  *GetUser() const { return user; }
   const char  *GetPassword() const { return pass; }
   const char  *GetPort() const { return portname; }
   const xstring& GetConnectURL(int flags=0) const;
   const xstring& GetFileURL(const char *file,int flags=0) const;
   enum { NO_PATH=1,WITH_PASSWORD=2,NO_PASSWORD=4,NO_USER=8 };
   const char *GetLastDisconnectCause() const { return last_disconnect_cause; }

   void Connect(const char *h,const char *p);
   void ConnectVerify();
   void PathVerify(const Path &);
   virtual void Login(const char *u,const char *p);
   void AnonymousLogin() { Login(0,0); }

   // reset location-related state on Connect/Login/AnonymousLogin
   virtual void ResetLocationData();

   virtual void Open(const char *file,int mode,off_t pos=0);
   void Open2(const char *f1,const char *f2,open_mode m);
   void SetFileURL(const char *u);
   void SetLimit(off_t lim) { limit=lim; }
   void SetSize(off_t s) { entity_size=s; }
   void SetDate(time_t d) { entity_date=d; }
   void WantDate(FileTimestamp *d) { opt_date=d; }
   void WantSize(off_t *s) { opt_size=s; }
   void AsciiTransfer() { ascii=true; }
   virtual void Close();

   void Rename(const char *rfile,const char *to,bool clobber=false);
   void Link(const char *f1,const char *f2) { Open2(f1,f2,LINK); }
   void Symlink(const char *f1,const char *f2) { Open2(f1,f2,SYMLINK); }
   void Mkdir(const char *rfile,bool allpath=false);
   void Chdir(const char *dir,bool verify=true);
   void ChdirAccept() { cwd=*new_cwd; }
   void SetCwd(const Path &new_cwd) { cwd=new_cwd; }
   void Remove(const char *rfile)    { Open(rfile,REMOVE); }
   void RemoveDir(const char *dir)  { Open(dir,REMOVE_DIR); }
   void Chmod(const char *file,int m);

   void	 GetInfoArray(FileSet *info);
   int	 InfoArrayPercentDone() { return fileset_for_info->curr_pct(); }

   virtual const char *CurrentStatus();

   virtual int Read(Buffer *buf,int size) = 0;
   virtual int Write(const void *buf,int size) = 0;
   virtual int Buffered();
   virtual int StoreStatus() = 0;
   virtual bool IOReady();
   off_t GetPos() const { return pos; }
   off_t GetRealPos() const { return real_pos<0?pos:real_pos; }
   void SeekReal() { pos=GetRealPos(); }
   void RereadManual() { norest_manual=true; }
   void SetFragile() { fragile=true; }

   const Path& GetCwd() const { return cwd; }
   const Path& GetNewCwd() const { return *new_cwd; }
   const char *GetFile() const { return file; }

   virtual int Do() = 0;
   virtual int Done() = 0;

   virtual bool SameLocationAs(const FileAccess *fa) const;
   virtual bool SameSiteAs(const FileAccess *fa) const;
   bool IsBetterThan(const FileAccess *fa) const;

   void Init();
   FileAccess() : all_fa_node(this) { Init(); }
   FileAccess(const FileAccess *);

   void	 DontSleep() { reconnect_timer.Stop(); }

   bool	 IsClosed() { return mode==CLOSED; }
   bool	 IsOpen() { return !IsClosed(); }
   int	 OpenMode() { return mode; }

   virtual int  IsConnected() const; // level of connection (0 - not connected).
   void Disconnect(const char *dc=0) { last_disconnect_cause.set(dc); DisconnectLL(); }
   virtual void DisconnectLL() {}
   virtual void UseCache(bool);
   virtual bool NeedSizeDateBeforehand();

   int GetErrorCode() { return error_code; }

   enum status
   {
      IN_PROGRESS=1,	// is returned only by *Status() or Done()
      OK=0,
      SEE_ERRNO=-100,
      LOOKUP_ERROR,
      NOT_OPEN,
      NO_FILE,
      NO_HOST,
      FILE_MOVED,
      FATAL,
      STORE_FAILED,
      LOGIN_FAILED,
      DO_AGAIN,
      NOT_SUPP,
      FRAGILE_FAILED,
   };

   virtual const char *StrError(int err);
   virtual void Cleanup();
   virtual void CleanupThis();
   void CleanupAll();
      // ^^ close idle connections, etc.
   virtual ListInfo *MakeListInfo(const char *path=0);
   virtual Glob *MakeGlob(const char *pattern);
   virtual DirList *MakeDirList(ArgV *a);
   virtual FileSet *ParseLongList(const char *buf,int len,int *err=0) const { return 0; }

   static bool NotSerious(int err) { return temporary_network_error(err); }

   const char *GetNewLocation() const { return location; }
   const char *GetNewLocationFile() const { return location_file; }
   int GetNewLocationMode() const { return location_mode; }
   bool IsNewLocationPermanent() const { return location_permanent; }
   virtual FileAccess *GetNewLocationFA() const;

   const char *GetSuggestedFileName() { return suggested_filename; }
   const char *GetEntityContentType() { return entity_content_type; }
   const char *GetEntityCharset() { return entity_charset; }

   void Reconfig(const char *);


   typedef FileAccess *SessionCreator();
   class Protocol
   {
      static xmap_p<Protocol> proto_by_name;
      const char *proto;
      SessionCreator *New;

      static Protocol *FindProto(const char *proto);
   public:
      static FileAccess *NewSession(const char *proto);
      Protocol(const char *proto,SessionCreator *creator);
      static void ClassCleanup() { proto_by_name.empty(); }
   };

   static void Register(const char *proto,SessionCreator *creator)
      {
	 (void)new Protocol(proto,creator);
      }

   static FileAccess *New(const char *proto,const char *host=0,const char *port=0);
   static FileAccess *New(const class ParsedURL *u,bool dummy=true);

   void SetPasswordGlobal(const char *p);
   void InsecurePassword(bool i)
      {
	 pass_open=i;
      }
   void SetPriority(int p)
      {
	 if(p==priority)
	    return;
	 priority=p;
	 current->Timeout(0);
      }
   int GetPriority() const { return priority; }

   // not pretty (FIXME)
   int GetRetries() const { return retries; }
   void SetRetries(int r) { retries=r; }
   int GetMaxRetries() const { return max_retries; }
   time_t GetTryTime() const { return reconnect_timer.GetStartTime(); }
   void SetTryTime(time_t t);

   const char *GetLogContext() { return hostname; }

   static void ClassInit();
   static void ClassCleanup();
};

// shortcut
#define FA FileAccess

// cache of used sessions
class SessionPool
{
   enum { pool_size=64 };
   static FileAccess *pool[pool_size];

public:
   static void Reuse(FileAccess *);
   static void Print(FILE *f);
   static FileAccess *GetSession(int n);

   // start with n==0, then increase n; returns 0 when no more
   static FileAccess *Walk(int *n,const char *proto);

   static void ClearAll();
};

class FileAccessRef : public SMTaskRef<FileAccess>
{
   FileAccessRef(const FileAccessRef&);  // disable cloning
   void operator=(const FileAccessRef&);   // and assignment

   void reuse() { if(ptr) { ptr->DecRefCount(); SessionPool::Reuse(ptr); ptr=0; } }

public:
   FileAccessRef() {}
   FileAccessRef(FileAccess *p) : SMTaskRef<FileAccess>(p) {}
   ~FileAccessRef() { reuse(); }
   const FileAccessRef& operator=(FileAccess *p);

   template<class T> const SMTaskRef<T>& Cast() const
      { void(static_cast<T*>(this->ptr)); return *(const SMTaskRef<T>*)this; }

   static const FileAccessRef null;
};

// constant ref (ref clone)
class FileAccessRefC
{
   void close() { if(*ref) (*ref)->Close(); }

protected:
   const FileAccessRef *ref;

public:
   FileAccessRefC(const FileAccessRef& p) { ref=&p; }
   const FileAccessRef& operator=(const FileAccessRef& p) { close(); ref=&p; return p; }
   operator const FileAccess*() const { return *ref; }
   FileAccess *operator->() const { return ref->operator->(); }
   operator const FileAccessRef&() { return *ref; }
};

// static reference
class FileAccessRefS : public FileAccessRef
{
   FileAccessRefS(const FileAccessRefS&);  // disable cloning
   void operator=(const FileAccessRefS&);   // and assignment

public:
   FileAccessRefS() {}
   FileAccessRefS(FileAccess *p) { ptr=p; }
   ~FileAccessRefS() { ptr=0; }
   const FileAccessRefS& operator=(FileAccess *p) { ptr=p; return *this; }
};

class FileAccessOperation : public SMTask
{
protected:
   FileAccessRefS session;
   bool done;
   xstring error_text;
   void SetError(const char *);
   void SetErrorCached(const char *);

   bool use_cache;

   void PrepareToDie() { if(session) session->Close(); }

public:
   FileAccessOperation(FileAccess *s) : session(s), done(false), use_cache(true) {}

   virtual int Do() = 0;
   bool Done() { return done; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }

   virtual const char *Status() = 0;

   void UseCache(bool y=true) { use_cache=y; }
};

#include "PatternSet.h"
class ListInfo : public FileAccessOperation
{
protected:
   FileAccess::Path saved_cwd;
   Ref<FileSet> result;
   Ref<FileSet> excluded;

   const char *exclude_prefix;
   const PatternSet *exclude;

   unsigned need;
   bool follow_symlinks;
   bool try_recursive;
   bool is_recursive;

   void PrepareToDie();
   ~ListInfo();

public:
   ListInfo(FileAccess *session,const char *path);

   void SetExclude(const char *p,const PatternSet *e) { exclude_prefix=p; exclude=e; excluded=new FileSet(); }
   void TryRecursive(bool y=true) { try_recursive=y; }

   // caller has to delete the resulting FileSet itself.
   FileSet *GetResult() { return result.borrow(); }
   FileSet *GetExcluded() { return excluded.borrow(); }
   bool IsRecursive() const { return is_recursive; }

   void Need(unsigned mask) { need|=mask; }
   void NoNeed(unsigned mask) { need&=~mask; }
   void FollowSymlinks() { follow_symlinks=true; }
};

#include "buffer.h"
class LsOptions
{
public:
   bool append_type:1;
   bool multi_column:1;
   bool show_all:1;
   LsOptions()
      {
	 append_type=false;
	 multi_column=false;
	 show_all=false;
      }
};

class DirList : public FileAccessOperation
{
protected:
   Ref<Buffer> buf;
   Ref<ArgV> args;
   bool color;
   ~DirList();

public:
   DirList(FileAccess *s,ArgV *a);

   virtual int Do() = 0;
   virtual const char *Status() = 0;

   int Size() { return buf->Size(); }
   bool Eof() { return buf->Eof();  }
   void Get(const char **b,int *size) { buf->Get(b,size); }
   void Skip(int len) { buf->Skip(len); }

   void UseColor(bool c=true) { color=c; }
};

class UploadState
{
   time_t try_time;
   off_t pos_watermark;
   int retries;
public:
   UploadState() : try_time(NO_DATE), pos_watermark(0),retries(-1) {}
   void Clear() {
      try_time=NO_DATE;
      pos_watermark=0;
      retries=-1;
   }
   void Save(const FileAccess *session) {
      try_time=session->GetTryTime();
      retries=session->GetRetries();
      off_t pos=session->GetRealPos();
      int max_retries=session->GetMaxRetries();
      if(max_retries>0 && retries>=max_retries)
	 pos=0;
      if(pos_watermark<pos) {
	 pos_watermark=pos;
	 retries=-1;
      }
   }
   void Restore(const FileAccessRef& session) {
      if(try_time!=NO_DATE)
	 session->SetTryTime(try_time);
      if(retries>=0)
	 session->SetRetries(retries+1);
   }
};

#endif /* FILEACCESS_H */
