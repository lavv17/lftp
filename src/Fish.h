/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef FISH_H
#define FISH_H

#include "NetAccess.h"
#include "PtyShell.h"

class Fish : public NetAccess
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
   bool received_greeting;
   int  password_sent;

   void Init();

   int max_send;
   void	 Send(const char *format,...) PRINTF_LIKE(2,3);
   void	 SendMethod();
   void	 SendArrayInfoRequests();

   IOBuffer *send_buf;
   IOBuffer *recv_buf;
   bool recv_buf_suspended;

   PtyShell *ssh;

   void Disconnect();
   int IsConnected()
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

   expect_t *RespQueue;
   int	 RQ_alloc;   // memory allocated
   int	 RQ_head;
   int	 RQ_tail;

   char  **path_queue;
   int	 path_queue_len;
   void  PushDirectory(const char *);
   char  *PopDirectory();
   void	 EmptyPathQueue();

   int   RespQueueIsEmpty() { return RQ_head==RQ_tail; }
   int	 RespQueueSize() { return RQ_tail-RQ_head; }
   void  EmptyRespQueue() { RQ_head=RQ_tail=0; }

   void GetBetterConnection(int level);
   void MoveConnectionHere(Fish *o);

   char  *line;
   char  *message;

   bool	 eof;
   bool	 encode_file;

public:
   static void ClassInit();

   Fish();
   Fish(const Fish*);
   ~Fish();

   const char *GetProto() { return "fish"; }

   FileAccess *Clone() { return new Fish(this); }
   static FileAccess *New();

   int Do();
   int Done();
   int Read(void *,int);
   int Write(const void *,int);
   int StoreStatus();
   int Buffered();

   void Close();
   const char *CurrentStatus();

   void Reconfig(const char *name=0);

   bool SameSiteAs(FileAccess *fa);
   bool SameLocationAs(FileAccess *fa);

   DirList *MakeDirList(ArgV *args);
   Glob *MakeGlob(const char *pattern);
   ListInfo *MakeListInfo(const char *p);
   FileSet *ParseLongList(const char *buf,int len,int *err=0);

   static const char *shell_encode(const char *);
   void DontEncodeFile() { encode_file=false; }

   bool NeedSizeDateBeforehand() { return true; }

   void Suspend();
   void Resume();

   void Cleanup();
   void CleanupThis();
};

class FishDirList : public DirList
{
   FileAccess *session;
   IOBuffer *ubuf;
   char *pattern;

public:
   FishDirList(ArgV *a,FileAccess *fa);
   ~FishDirList();
   const char *Status();
   int Do();

   void Suspend();
   void Resume();
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
