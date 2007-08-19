/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FILEACCESS_H
#define FILEACCESS_H

#include <time.h>
#include <sys/types.h>

#ifdef HAVE_SYS_STROPTS_H
# include <sys/stropts.h>
#endif

#ifdef HAVE_SYS_POLL_H
# include <sys/poll.h>
#else
# include <poll.h>
#endif
#include <assert.h>

#include "SMTask.h"
#include "trio.h"
#include "xstring.h"
#include "ResMgr.h"
#include "FileSet.h"
#include "ArgV.h"

#define FILE_END     ((off_t)-1L)
#define UNKNOWN_POS  ((off_t)-1L)

class ListInfo;
class Glob;
class NoGlob;
class DirList;
class FileAccessRef;

class FileAccess : public SMTask, public ResClient
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
      CHANGE_MODE
   };

   struct fileinfo
   {
      off_t size;
      const char *file;
      time_t time;
      bool get_size:1;
      bool get_time:1;
      bool is_dir:1;
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
      void ExpandTilde(const Path &home);
      static void Optimize(char *p,int dev_prefix=0);
      void Optimize() { Optimize(path.get_non_const(),device_prefix_len); }
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
   xstring vproto;
   xstring hostname;
   xstring portname;
   xstring user;
   xstring pass;
   bool	 pass_open;

   const char *default_cwd;
   Path	 home;
   Path	 cwd;
   Path	 *new_cwd;
   xstring file;
   xstring file_url;
   xstring file1;
   int	 mode;
   off_t pos;
   off_t real_pos;
   off_t limit;

   FileTimestamp *opt_date;
   off_t  *opt_size;

   static void NonBlock(int fd);
   static void CloseOnExec(int fd);

   static void DebugPrint(const char *prefix,const char *str,int level=9);
   static void LogError(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogNote(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogRecv(int level,const char *fmt,...) PRINTF_LIKE(2,3);
   static void LogSend(int level,const char *fmt,...) PRINTF_LIKE(2,3);

   time_t try_time;
   int retries;

   fileinfo *array_for_info;
   int	 array_ptr;
   int	 array_cnt;

   bool	 mkdir_p;

   int	 saved_errno;

   void	 ExpandTildeInCWD();
   const char *ExpandTildeStatic(const char *s);

   xstring real_cwd;
   void set_real_cwd(const char *c)
      {
	 real_cwd.set(c);
      }
   void set_home(const char *h)
      {
	 home.Set(h);
	 ExpandTildeInCWD();
      }

   off_t  entity_size; // size of file to be sent
   time_t entity_date; // date of file to be sent

   xstring closure;
   const char *res_prefix;
   const char *ResPrefix() const { return res_prefix?res_prefix:GetProto(); }
   const char *ResClosure() const { return closure?closure.get():GetHostName(); }

   int chmod_mode;
   bool ascii;
   bool norest_manual;

   int	priority;   // higher priority can take over other session.
   int	last_priority;

   bool Error() { return error_code!=OK; }
   void ClearError();
   void SetError(int code,const char *mess=0);
   void Fatal(const char *mess);
   xstring error;
   int error_code;
   xstring location;
   xstring suggested_filename;
   void SetSuggestedFileName(const char *fn);

   xstring entity_content_type;
   xstring entity_charset;

   FileAccess *next;
   static FileAccess *chain;
   FileAccess *FirstSameSite() { return NextSameSite(0); }
   FileAccess *NextSameSite(FileAccess *);

   int device_prefix_len(const char *path);

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
   const char  *GetConnectURL(int flags=0) const;
   const char  *GetFileURL(const char *file,int flags=0) const;
   enum { NO_PATH=1,WITH_PASSWORD=2,NO_PASSWORD=4,NO_USER=8 };

   void Connect(const char *h,const char *p);
   void ConnectVerify();
   void PathVerify(const Path &);
   virtual void AnonymousLogin();
   virtual void Login(const char *u,const char *p);

   // reset location-related state on Connect/Login/AnonymousLogin
   virtual void ResetLocationData();

   virtual void Open(const char *file,int mode,off_t pos=0);
   void SetFileURL(const char *u);
   void SetLimit(off_t lim) { limit=lim; }
   void SetSize(off_t s) { entity_size=s; }
   void SetDate(time_t d) { entity_date=d; }
   void WantDate(FileTimestamp *d) { opt_date=d; }
   void WantSize(off_t *s) { opt_size=s; }
   void AsciiTransfer() { ascii=true; }
   virtual void Close();

   virtual void	Rename(const char *rfile,const char *to);
   virtual void Mkdir(const char *rfile,bool allpath=false);
   virtual void Chdir(const char *dir,bool verify=true);
   void ChdirAccept() { cwd=*new_cwd; }
   void SetCwd(const Path &new_cwd) { cwd=new_cwd; }
   void Remove(const char *rfile)    { Open(rfile,REMOVE); }
   void RemoveDir(const char *dir)  { Open(dir,REMOVE_DIR); }
   void Chmod(const char *file,int m);

   void	 GetInfoArray(struct fileinfo *info,int count);
   int	 InfoArrayPercentDone()
      {
	 if(array_cnt==0)
	    return 100;
	 return array_ptr*100/array_cnt;
      }

   virtual const char *CurrentStatus();

   virtual int Read(void *buf,int size) = 0;
   virtual int Write(const void *buf,int size) = 0;
   virtual int Buffered();
   virtual int StoreStatus() = 0;
   virtual bool IOReady();
   off_t GetPos() { return pos; }
   off_t GetRealPos() { return real_pos<0?pos:real_pos; }
   void SeekReal() { pos=GetRealPos(); }
   void RereadManual() { norest_manual=true; }

   const Path& GetCwd() const { return cwd; }
   const Path& GetNewCwd() const { return *new_cwd; }
   const char *GetFile() const { return file; }

   virtual int Do() = 0;
   virtual int Done() = 0;

   virtual bool SameLocationAs(const FileAccess *fa) const;
   virtual bool SameSiteAs(const FileAccess *fa) const;
   bool IsBetterThan(const FileAccess *fa) const;

   void Init();
   FileAccess() { Init(); }
   FileAccess(const FileAccess *);
   virtual ~FileAccess();

   void	 DontSleep() { try_time=0; }

   bool	 IsClosed() { return mode==CLOSED; }
   bool	 IsOpen() { return !IsClosed(); }
   int	 OpenMode() { return mode; }

   virtual int  IsConnected() const; // level of connection (0 - not connected).
   virtual void Disconnect();
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
      NOT_SUPP
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

   static bool NotSerious(int err);

   const char *GetNewLocation() { return location; }
   const char *GetSuggestedFileName() { return suggested_filename; }
   const char *GetEntityContentType() { return entity_content_type; }
   const char *GetEntityCharset() { return entity_charset; }

   void Reconfig(const char *);


   typedef FileAccess *SessionCreator();
   class Protocol
   {
      static Protocol *chain;
      Protocol *next;
      const char *proto;
      SessionCreator *New;

      static Protocol *FindProto(const char *proto);
   public:
      static FileAccess *NewSession(const char *proto);
      Protocol(const char *proto,SessionCreator *creator);
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
   int GetRetries() { return retries; }
   void SetRetries(int r) { retries=r; }
   time_t GetTryTime() { return try_time; }
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
   const FileAccessRef& operator=(FileAccess *p) { reuse(); ptr=SMTask::MakeRef(p); return *this; }

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
   ~FileAccessRefC() { close(); }
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

   ~FileAccessOperation() { if(session) session->Close(); }

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

   const char *exclude_prefix;
   const PatternSet *exclude;

   unsigned need;
   bool follow_symlinks;

public:
   ListInfo(FileAccess *session,const char *path);
   ~ListInfo();

   void SetExclude(const char *p,const PatternSet *e) { exclude_prefix=p; exclude=e; }

   // caller has to delete the resulting FileSet itself.
   FileSet *GetResult() { return result.borrow(); }

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

public:
   DirList(FileAccess *s,ArgV *a)
      : FileAccessOperation(s), buf(new Buffer()), args(a), color(false) {}

   virtual int Do() = 0;
   virtual const char *Status() = 0;

   int Size() { return buf->Size(); }
   bool Eof() { return buf->Eof();  }
   void Get(const char **b,int *size) { buf->Get(b,size); }
   void Skip(int len) { buf->Skip(len); }

   void UseColor(bool c=true) { color=c; }
};

#endif /* FILEACCESS_H */
