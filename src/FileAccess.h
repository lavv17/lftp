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
      CONNECT_VERIFY
   };

   struct fileinfo
   {
      char *file;
      long size;
      time_t time;
      bool get_size:1;
      bool get_time:1;
   };

protected:
   char	 *hostname;
   int	 port;
   char  *user;
   char  *pass;
   char	 *group;
   char	 *gpass;

   char	 *home;
   char  *cwd;
   char  *file;
   char	 *file1;
   int	 mode;
   long	 pos;
   long	 real_pos;

   time_t *opt_date;
   long	  *opt_size;

   int	 Poll(int fd,int ev);
   int   CheckHangup(struct pollfd *pfd,int num);

   char	 *last_error_resp;

   // these items are controlled globally
   static FILE *debug_file;
   static void (*debug_callback)(char *msg);
   static int  debug_level;

   void  DebugPrint(const char *prefix,const char *str,int level=0);

   time_t   try_time;
   time_t   event_time;

   fileinfo *array_for_info;
   int	 array_ptr;
   int	 array_cnt;

   bool	 mkdir_p;

   int	 saved_errno;

   int	 timeout;
   int	 sleep_time;

   void	 ExpandTildeInCWD();

   char *real_cwd;
   void set_real_cwd(const char *c)
      {
	 xfree(real_cwd);
	 real_cwd=xstrdup(c);
      }

   char *url;

public:
   virtual const char *GetProto() = 0; // http, ftp, file etc
   bool SameProtoAs(FileAccess *fa) { return !strcmp(GetProto(),fa->GetProto()); }
   virtual FileAccess *Clone() = 0;
   static FileAccess *New();

   const char  *GetHome() { return home; }
   const char  *GetHostName() { return hostname; }
   const char  *GetUser() { return user; }
   int	 GetPort() { return port; }
   virtual const char *GetConnectURL(int flags=0);
   enum { NO_CWD=1,WITH_PASSWORD=2 };

   virtual void Connect(const char *,int) {}
   virtual void ConnectVerify() {}

   virtual void AnonymousLogin();

   virtual void Login(const char *u,const char *p);
   virtual void GroupLogin(const char *g,const char *p);

   virtual void Open(const char *file,int mode,long pos=0);
   void WantDate(time_t *d) { opt_date=d; }
   void WantSize(long *s) { opt_size=s; }
   virtual void Close();

   virtual void	Rename(const char *file,const char *to);
   virtual void Mkdir(const char *file,bool allpath=false);
   virtual void Chdir(const char *dir);
   void Remove(const char *file)    { Open(file,REMOVE); }
   void RemoveDir(const char *dir)  { Open(dir,REMOVE_DIR); }

   void	 GetInfoArray(struct fileinfo *info,int count);

   virtual const char *CurrentStatus() { return(""); }

   virtual int Read(void *buf,int size) = 0;
   virtual int Write(const void *buf,int size) = 0;
   virtual int StoreStatus() = 0;
   long GetPos() { return pos; }
   long GetRealPos() { return real_pos<0?pos:real_pos; }
   void SeekReal() { pos=GetRealPos(); }

   const char *GetCwd() { return cwd; }

   virtual int Do() = 0;
   virtual int Done() = 0;

   virtual bool SameLocationAs(FileAccess *fa);
   virtual bool IsBetterThan(FileAccess *fa) { (void)fa; return false; }

   void Init();
   FileAccess() { Init(); }
   FileAccess(const FileAccess *);
   virtual ~FileAccess();

   void	 DontSleep() { try_time=0; }

   bool	 IsClosed() { return mode==CLOSED; }
   bool	 IsOpen() { return !IsClosed(); }

   static void SetDebug(FILE *new_debug_file,int new_debug_level,void (*cb)(char*)=0)
   {
      debug_file=new_debug_file;
      debug_callback=cb;
      debug_level=new_debug_level;
   }

   virtual void CopyOptions(FileAccess *fa) { (void)fa; }
   virtual bool IsConnected() { return false; }
   virtual void Disconnect() {}

   enum status
   {
      IN_PROGRESS=1,	// is returned only by *Status() or Done()
      OK=0,
      SEE_ERRNO=-100,
      LOOKUP_ERROR,
      NOT_OPEN,
      NO_FILE,
      NO_HOST,
      FATAL,
      STORE_FAILED,
      LOGIN_FAILED,
      DO_AGAIN,
      NOT_SUPP
   };

   const char *StrError(int err);
   virtual void Cleanup(bool all=false) { (void)all; }
      // close idle connections, etc.
   virtual class ListInfo *MakeListInfo() { return 0; }
};

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
