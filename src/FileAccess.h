/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "SMTask.h"
#include <stdio.h>
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

#include "xmalloc.h"
#include "ResMgr.h"
#include "FileSet.h"

#define NO_SIZE	     (-1L)
#define NO_SIZE_YET  (-2L)
#define NO_DATE	     ((time_t)-1L)
#define NO_DATE_YET  ((time_t)-2L)

class ListInfo;
class Glob;
class NoGlob;
class DirList;
class ArgV;

class FileAccess : public SMTask
{
public:
   enum open_mode
   {
      CLOSED,
      RETRIEVE,
      STORE,
      LONG_LIST,
      LIST,
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
      const char *file;
      off_t size;
      time_t time;
      bool get_size:1;
      bool get_time:1;
      bool is_dir:1;
   };

protected:
   char	 *vproto;
   char	 *hostname;
   char	 *portname;
   char  *user;
   char  *pass;
   bool	 pass_open;
   char	 *group;
   char	 *gpass;

   char	 *home;
   const char *default_cwd;
   char  *cwd;
   char  *file;
   char	 *file_url;
   char	 *file1;
   int	 mode;
   off_t pos;
   off_t real_pos;

   time_t *opt_date;
   off_t  *opt_size;

   int	 Poll(int fd,int ev);
   int   CheckHangup(struct pollfd *pfd,int num);
   static void NonBlock(int fd);
   static void CloseOnExec(int fd);

   void  DebugPrint(const char *prefix,const char *str,int level=9);

   time_t   try_time;
   time_t   event_time;
   void BumpEventTime(time_t t);
   int	 retries;

   fileinfo *array_for_info;
   int	 array_ptr;
   int	 array_cnt;

   bool	 mkdir_p;

   int	 saved_errno;

   void	 ExpandTildeInCWD();
   const char *ExpandTildeStatic(const char *s);

   char *real_cwd;
   void set_real_cwd(const char *c)
      {
	 xfree(real_cwd);
	 real_cwd=xstrdup(c);
      }

   char *url;

   off_t  entity_size; // size of file to be sent
   time_t entity_date; // date of file to be sent

   char *closure;
   const char *res_prefix;
   ResValue Query(const char *name,const char *closure=0);

   int chmod_mode;
   bool ascii;
   bool norest_manual;

   int	priority;   // higher priority can take over other session.

   bool Error() { return error_code!=OK; }
   void ClearError();
   void SetError(int code,const char *mess=0);
   void Fatal(const char *mess);
   char *error;
   int error_code;
   char *location;

   FileAccess *next;
   static FileAccess *chain;
   FileAccess *FirstSameSite() { return NextSameSite(0); }
   FileAccess *NextSameSite(FileAccess *);

   static int device_prefix_len(const char *path);

   virtual ~FileAccess();

public:
   virtual const char *GetProto() = 0; // http, ftp, file etc
   bool SameProtoAs(FileAccess *fa) { return !strcmp(GetProto(),fa->GetProto()); }
   virtual FileAccess *Clone() = 0;
   virtual const char *ProtocolSubstitution(const char *host) { return 0; }

   const char *GetVisualProto() { return vproto?vproto:GetProto(); }
   void SetVisualProto(const char *p) { xfree(vproto); vproto=xstrdup(p); }
   const char  *GetHome() { return home; }
   const char  *GetHostName() { return hostname; }
   const char  *GetUser() { return user; }
   const char  *GetPort() { return portname; }
   const char  *GetConnectURL(int flags=0);
   const char  *GetFileURL(const char *file,int flags=0);
   enum { NO_PATH=1,WITH_PASSWORD=2,NO_PASSWORD=4 };

   virtual void Connect(const char *h,const char *p);
   virtual void ConnectVerify();

   virtual void AnonymousLogin();

   virtual void Login(const char *u,const char *p);
   virtual void GroupLogin(const char *g,const char *p);

   virtual void Open(const char *file,int mode,off_t pos=0);
   void SetFileURL(const char *u) { xfree(file_url); file_url=xstrdup(u); }
   void SetSize(off_t s) { entity_size=s; }
   void SetDate(time_t d) { entity_date=d; }
   void WantDate(time_t *d) { opt_date=d; }
   void WantSize(off_t *s) { opt_size=s; }
   void AsciiTransfer() { ascii=true; }
   virtual void Close();

   virtual void	Rename(const char *rfile,const char *to);
   virtual void Mkdir(const char *rfile,bool allpath=false);
   virtual void Chdir(const char *dir,bool verify=true);
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

   const char *GetCwd() { return cwd; }
   const char *GetFile() { return file; }

   virtual int Do() = 0;
   virtual int Done() = 0;

   virtual bool SameLocationAs(FileAccess *fa);
   virtual bool SameSiteAs(FileAccess *fa);
   virtual bool IsBetterThan(FileAccess *fa);

   void Init();
   FileAccess() { Init(); }
   FileAccess(const FileAccess *);

   void	 DontSleep() { try_time=0; }

   bool	 IsClosed() { return mode==CLOSED; }
   bool	 IsOpen() { return !IsClosed(); }
   int	 OpenMode() { return mode; }

   virtual int  IsConnected(); // level of connection (0 - not connected).
   virtual void Disconnect();
   virtual void UseCache(bool);
   virtual bool NeedSizeDateBeforehand();

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
   virtual ListInfo *MakeListInfo();
   virtual Glob *MakeGlob(const char *pattern);
   virtual DirList *MakeDirList(ArgV *a);

   static bool NotSerious(int err);

   const char *GetNewLocation() { return location; }

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

   static FileAccess *New(const char *proto,const char *host=0);
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

   // not pretty (FIXME)
   int GetRetries() { return retries; }
   void SetRetries(int r) { retries=r; }
   time_t GetTryTime() { return try_time; }
   void SetTryTime(time_t t) { try_time=t; }

   static void ClassInit();
};

class FileAccessOperation : public SMTask
{
protected:
   bool done;
   char *error_text;
   void SetError(const char *);

   bool use_cache;

   virtual ~FileAccessOperation();

public:
   FileAccessOperation();

   virtual int Do() = 0;
   bool Done() { return done; }
   bool Error() { return error_text!=0; }
   const char *ErrorText() { return error_text; }

   virtual const char *Status() = 0;

   void UseCache(bool y=true) { use_cache=y; }
};

class Glob : public FileAccessOperation
{
protected:
   char  *pattern;
   FileSet list;
   bool	 dirs_only;
   bool	 files_only;
   bool	 match_period;
   bool	 inhibit_tilde;
   void	 add(const FileInfo *info);
   void	 add_force(const FileInfo *info);
   virtual ~Glob();
public:
   const char *GetPattern() { return pattern; }
   FileSet *GetResult() { return &list; }
   Glob(const char *p);
   void DirectoriesOnly() { dirs_only=true; }
   void FilesOnly() { files_only=true; }
   void NoMatchPeriod() { match_period=false; }
   void NoInhibitTilde() { inhibit_tilde=false; }
   void SortByName() { list.SortByName(); }

   static bool HasWildcards(const char *);
   static void UnquoteWildcards(char *);
};
class NoGlob : public Glob
{
public:
   NoGlob(const char *p);
   const char *Status() { return ""; }
   int Do();
};

class GlobURL
{
   FileAccess *session;
   bool reuse;
   char *url_prefix;
public:
   Glob *glob;
   GlobURL(FileAccess *s,const char *p);
   ~GlobURL();
   FileSet *GetResult();
   bool Done()  { return glob->Done(); }
   bool Error() { return glob->Error(); }
   const char *ErrorText() { return glob->ErrorText(); }
   const char *Status() { return glob->Status(); }
   void SortByName() { glob->SortByName(); }
};

#include "FileSet.h"

class ListInfo : public FileAccessOperation
{
protected:
   FileSet *result;

   const char *path;
   regex_t *rxc_exclude;
   regex_t *rxc_include;

   unsigned need;
   bool follow_symlinks;

   virtual ~ListInfo();

public:
   ListInfo();

   virtual void SetExclude(const char *p,regex_t *x,regex_t *i);

   FileSet *GetResult()
      {
	 FileSet *tmp=result;
      	 result=0;
	 return tmp;
	 // miss := (assign and return old value) :(
      	 // return result:=0;
      }

   void Need(unsigned mask) { need|=mask; }
   void NoNeed(unsigned mask) { need&=~mask; }
   void FollowSymlinks() { follow_symlinks=true; }
};

#include "buffer.h"

class DirList : public FileAccessOperation
{
protected:
   Buffer *buf;
   ArgV *args;
   ~DirList();
public:
   DirList(ArgV *a)
      {
	 buf=new Buffer();
	 args=a;
      }

   virtual int Do() = 0;
   virtual const char *Status() = 0;

   int Size() { return buf->Size(); }
   bool Eof() { return buf->Eof();  }
   void Get(const char **b,int *size) { buf->Get(b,size); }
   void Skip(int len) { buf->Skip(len); }
};


// shortcut
#define FA FileAccess

// cache of used sessions
class SessionPool
{
   static const int pool_size=64;
   static FileAccess *pool[pool_size];

public:
   static void Reuse(FileAccess *);
   static void Print(FILE *f);
   static FileAccess *GetSession(int n);

   // start with n==0, then increase n; returns 0 when no more
   static FileAccess *Walk(int *n,const char *proto);
};

#endif /* FILEACCESS_H */
