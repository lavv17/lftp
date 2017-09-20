/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2015 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FISH_H
#define FISH_H

#include "SSH_Access.h"
#include "StringSet.h"

class Fish : public SSH_Access
{
   enum state_t
   {
      DISCONNECTED,
      CONNECTING,
      CONNECTING_1,
      CONNECTED,
      FILE_RECV,
      FILE_SEND,
      WAITING,
      DONE
   };

   state_t state;

   void Init();

   int max_send;
   void	 Send(const char *format,...) PRINTF_LIKE(2,3);
   void	 SendMethod();
   void	 SendArrayInfoRequests();

   void DisconnectLL();
   int IsConnected() const
      {
	 if(state==DISCONNECTED)
	    return 0;
	 if(state==CONNECTING)
	    return 1;
	 return 2;
      }

   off_t body_size;
   off_t bytes_received;

   enum expect_t
   {
      EXPECT_FISH,
      EXPECT_VER,
      EXPECT_PWD,
      EXPECT_CWD,
      EXPECT_DIR,
      EXPECT_RETR_INFO,
      EXPECT_RETR,
      EXPECT_INFO,
      EXPECT_DEFAULT,
      EXPECT_STOR_PRELIMINARY,
      EXPECT_STOR,
      EXPECT_QUOTE,
      EXPECT_IGNORE
   };

   void PushExpect(expect_t);
   int HandleReplies();
   void CloseExpectQueue();
   int ReplyLogPriority(int);

   xqueue<expect_t,xarray<expect_t> > RespQueue;
   StringSet path_queue;
   void  PushDirectory(const char *d) { path_queue.Append(d); }
   void  PopDirectory(xstring *d) { d->set_allocated(path_queue.Pop()); }
   void	 EmptyPathQueue() { path_queue.Empty(); }

   bool  RespQueueIsEmpty() { return RespQueue.count()==0; }
   int	 RespQueueSize() { return RespQueue.count(); }
   void  EmptyRespQueue() { RespQueue.empty(); }

   void GetBetterConnection(int level);
   void MoveConnectionHere(Fish *o);

   xstring line;
   xstring message;

   bool	 eof;
   bool	 encode_file;

public:
   static void ClassInit();

   Fish();
   Fish(const Fish*);
   ~Fish();

   const char *GetProto() const { return "fish"; }

   FileAccess *Clone() const { return new Fish(this); }
   static FileAccess *New();

   int Do();
   int Done();
   int Read(Buffer *,int);
   int Write(const void *,int);
   int StoreStatus();
   int Buffered();

   void Close();
   const char *CurrentStatus();

   void Reconfig(const char *name=0);

   bool SameSiteAs(const FileAccess *fa) const;
   bool SameLocationAs(const FileAccess *fa) const;

   DirList *MakeDirList(ArgV *args);
   Glob *MakeGlob(const char *pattern);
   ListInfo *MakeListInfo(const char *p);
   FileSet *ParseLongList(const char *buf,int len,int *err=0) const;

   void DontEncodeFile() { encode_file=false; }

   bool NeedSizeDateBeforehand() { return true; }

   void SuspendInternal();
   void ResumeInternal();
};

class FishDirList : public DirList
{
   SMTaskRef<IOBuffer> ubuf;
   const xstring_ca pattern;

public:
   FishDirList(Fish *s,ArgV *a)
      : DirList(s,a), pattern(args->CombineShellQuoted(1)) {}
   const char *Status();
   int Do();

   void SuspendInternal();
   void ResumeInternal();
};

class FishListInfo : public GenericParseListInfo
{
   FileSet *Parse(const char *buf,int len);
public:
   FishListInfo(Fish *session,const char *path)
      : GenericParseListInfo(session,path)
      {
	 can_get_prec_time=false;
      }
};

#endif
