/*
 * lftp - file transfer program
 *
 * Copyright (c) 2000 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef SFTP_H
#define SFTP_H

#include "NetAccess.h"
#include "StatusLine.h"
#include "PtyShell.h"

class SFtp : public NetAccess
{
   static const int PROTOCOL_VERSION=4;
   int	 protocol_version;

   const char *lc_to_utf8(const char *);
   const char *utf8_to_lc(const char *);

enum packet_type {
   SSH_FXP_INIT     =1,
   SSH_FXP_VERSION  =2,
   SSH_FXP_OPEN     =3,
   SSH_FXP_CLOSE    =4,
   SSH_FXP_READ     =5,
   SSH_FXP_WRITE    =6,
   SSH_FXP_LSTAT    =7,
   SSH_FXP_FSTAT    =8,
   SSH_FXP_SETSTAT  =9,
   SSH_FXP_FSETSTAT =10,
   SSH_FXP_OPENDIR  =11,
   SSH_FXP_READDIR  =12,
   SSH_FXP_REMOVE   =13,
   SSH_FXP_MKDIR    =14,
   SSH_FXP_RMDIR    =15,
   SSH_FXP_REALPATH =16,
   SSH_FXP_STAT     =17,
   SSH_FXP_RENAME   =18,
   SSH_FXP_READLINK =19,
   SSH_FXP_SYMLINK  =20,
   SSH_FXP_STATUS   =101,
   SSH_FXP_HANDLE   =102,
   SSH_FXP_DATA     =103,
   SSH_FXP_NAME     =104,
   SSH_FXP_ATTRS    =105,
   SSH_FXP_EXTENDED =200,
   SSH_FXP_EXTENDED_REPLY=201
};
static bool is_valid_reply(int p)
{
   return p==SSH_FXP_VERSION
      || p>=101 && p<=105
      || p==SSH_FXP_EXTENDED_REPLY;
}

#define SSH_FILEXFER_ATTR_EXTENDED     0x80000000
#define SSH_FILEXFER_ATTR_SIZE         0x00000001
#define SSH_FILEXFER_ATTR_UIDGID       0x00000002  // used in protocol v3
#define SSH_FILEXFER_ATTR_PERMISSIONS  0x00000004
#define SSH_FILEXFER_ATTR_ACCESSTIME   0x00000008
#define SSH_FILEXFER_ATTR_ACMODTIME    0x00000008  // used in protocol v3
#define SSH_FILEXFER_ATTR_CREATETIME   0x00000010
#define SSH_FILEXFER_ATTR_MODIFYTIME   0x00000020
#define SSH_FILEXFER_ATTR_ACL          0x00000040
#define SSH_FILEXFER_ATTR_OWNERGROUP   0x00000080
#define SSH_FILEXFER_ATTR_SUBSECOND_TIMES 0x00000100
#define SSH_FILEXFER_ATTR_EXTENDED     0x80000000

#define SSH_FILEXFER_TYPE_REGULAR      1
#define SSH_FILEXFER_TYPE_DIRECTORY    2
#define SSH_FILEXFER_TYPE_SYMLINK      3
#define SSH_FILEXFER_TYPE_SPECIAL      4
#define SSH_FILEXFER_TYPE_UNKNOWN      5

#define SSH_FXF_READ		       0x00000001
#define SSH_FXF_WRITE		       0x00000002
#define SSH_FXF_APPEND		       0x00000004
#define SSH_FXF_CREAT		       0x00000008
#define SSH_FXF_TRUNC		       0x00000010
#define SSH_FXF_EXCL		       0x00000020

enum sftp_status_t {
   SSH_FX_OK		   =0,
   SSH_FX_EOF		   =1,
   SSH_FX_NO_SUCH_FILE	   =2,
   SSH_FX_PERMISSION_DENIED=3,
   SSH_FX_FAILURE	   =4,
   SSH_FX_BAD_MESSAGE	   =5,
   SSH_FX_NO_CONNECTION    =6,
   SSH_FX_CONNECTION_LOST  =7,
   SSH_FX_OP_UNSUPPORTED   =8
};
static bool is_valid_status(int s)
{
   return s>=SSH_FX_OK && s<=SSH_FX_OP_UNSUPPORTED;
}

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
   int ssh_id;
   int reply_length;
   int handle;

   void Init();

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

   enum unpack_status_t
   {
      UNPACK_SUCCESS=0,
      UNPACK_WRONG_FORMAT=-1,
      UNPACK_PREMATURE_EOF=-2,
      UNPACK_NO_DATA_YET=1
   };
   class Packet
   {
   protected:
      int length;
      int unpacked;
      packet_type type;
      unsigned id;
      Packet(packet_type t)
	 {
	    type=t;
	    length=1;
	    if(HasID())
	       length+=4;
	 }
      static unpack_status_t DecodeString(Buffer *b,int *offset,int max_len,char **str_out,int *len_out);
      bool HasID() { return(type!=SSH_FXP_INIT && type!=SSH_FXP_VERSION); }
   public:
      Packet() { length=0; }
      virtual void Pack(Buffer *b)
	 {
	    b->PackUINT32BE(length);
	    b->PackUINT8(type);
	    if(HasID())
	       b->PackUINT32BE(id);
	 }
      virtual unpack_status_t Unpack(Buffer *b);
      virtual ~Packet() {}
      int GetLength() { return length; }
      packet_type GetPacketType() { return type; }
      int GetID() { return id; }
      void SetID(unsigned new_id) { id=new_id; }
      void DropData(Buffer *b) { b->Skip(4+(length>0?length:0)); }
      bool TypeIs(packet_type t) { return type==t; }
   };
   unpack_status_t UnpackPacket(Buffer *,Packet **);
   class PacketUINT32 : public Packet
   {
   protected:
      unsigned data;
      PacketUINT32(packet_type t,unsigned d=0) : Packet(t)
	 { data=d; length+=4; }
      unpack_status_t Unpack(Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    data=b->UnpackUINT32BE(unpacked);
	    unpacked+=4;
	    return UNPACK_SUCCESS;
	 }
   };
   class PacketSTRING : public Packet
   {
   protected:
      int string_len;
      char *string;
      PacketSTRING(packet_type t,const char *s=0,int l=-1) : Packet(t)
	 {
	    if(l==-1)
	       l=xstrlen(s);
	    string_len=l;
	    string=(char*)xmemdup(s,l);
	    length+=4+l;
	 }
      unpack_status_t Unpack(Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    res=DecodeString(b,&unpacked,0x10000,&string,&string_len);
	    return res;
	 }
      void Pack(Buffer *b)
	 {
	    Packet::Pack(b);
	    b->PackUINT32BE(string_len);
	    b->Put(string,string_len);
	 }
      const char *GetString() { return string; }
      int GetStringLength() { return string_len; }
   };
   class Request_INIT : public PacketUINT32
   {
   public:
      Request_INIT() : PacketUINT32(SSH_FXP_INIT,PROTOCOL_VERSION) {}
   };
   class Reply_VERSION : public PacketUINT32
   {
      char **extension_name;
      char **extension_data;
   public:
      Reply_VERSION() : PacketUINT32(SSH_FXP_VERSION) {}
      unpack_status_t Unpack(Buffer *b)
	 {
	    unpack_status_t res;
	    res=PacketUINT32::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
   	    // FIXME: unpack extensions.
	    return res;
	 }
      unsigned GetVersion() { return data; }
   };
   class Request_REALPATH : public PacketSTRING
   {
   public:
      Request_REALPATH(const char *p) : PacketSTRING(SSH_FXP_REALPATH,p) {}
   };
   struct ExtFileAttr
   {
      char *extended_type;
      char *extended_data;
      ExtFileAttr() { extended_type=extended_data=0; }
      ~ExtFileAttr() { xfree(extended_type); xfree(extended_data); }
      unpack_status_t Unpack(Buffer *b,int *offset);
   };
   struct FileAttrs
   {
      unsigned flags;
      int      type;		    // v4
      off_t    size;		    // present only if flag SIZE
      char     *owner;		    // present only if flag OWNERGROUP // v4
      char     *group;		    // present only if flag OWNERGROUP // v4
      uid_t    uid;		    // present only if flag UIDGID // v3
      gid_t    gid;		    // present only if flag UIDGID // v3
      unsigned permissions;	    // present only if flag PERMISSIONS
      time_t   atime;		    // present only if flag ACCESSTIME (ACMODTIME)
      int      atime_nseconds;	    // present only if flag SUBSECOND_TIMES
      time_t   createtime;	    // present only if flag CREATETIME
      int      createtime_nseconds; // present only if flag SUBSECOND_TIMES
      time_t   mtime;		    // present only if flag MODIFYTIME (ACMODTIME)
      int      mtime_nseconds;	    // present only if flag SUBSECOND_TIMES
      char     *acl;		    // present only if flag ACL
      unsigned extended_count;	    // present only if flag EXTENDED
      ExtFileAttr *extended_attrs;

      FileAttrs()
      {
	 flags=0; type=0; size=NO_SIZE; owner=group=acl=0; uid=gid=0;
	 permissions=0;
	 atime=createtime=mtime=NO_DATE;
	 atime_nseconds=createtime_nseconds=mtime_nseconds=0;
	 extended_count=0; extended_attrs=0;
      }
      ~FileAttrs()
      {
	 xfree(owner); xfree(group); xfree(acl);
	 delete[] extended_attrs;
      }
      unpack_status_t Unpack(Buffer *b,int *offset,int proto_version);
/*      void Pack(Buffer *b,int proto_version);*/
   };
   struct NameAttrs
   {
      char *name;
      char *longname;
      FileAttrs attrs;
      NameAttrs() { name=0; longname=0; }
      ~NameAttrs() { xfree(name); xfree(longname); }
      unpack_status_t Unpack(Buffer *b,int *offset,int proto_version);
/*      void Pack(Buffer *b,int proto_version);*/
   };
   class Reply_NAME : public Packet
   {
      int protocol_version;
      int count;
      NameAttrs *names;
   public:
      Reply_NAME(int pv) : Packet(SSH_FXP_NAME) { protocol_version=pv; }
      ~Reply_NAME() { delete[] names; }
      unpack_status_t Unpack(Buffer *b);
      int GetCount() { return count; }
      const NameAttrs *GetNameAttrs(int index)
	 {
	    if(index>count)
	       return 0;
	    return &names[index];
	 }
   };

   enum expect_t
   {
      EXPECT_HOME_PATH,
      EXPECT_VERSION,
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

   struct Expect
   {
      Packet *request;
      Packet *reply;
      expect_t tag;
      Expect *next;
      Expect(Packet *req,expect_t t) { request=req; tag=t; reply=0; }
      ~Expect() { delete request; delete reply; }
   };

   void PushExpect(Expect *);
   int HandleReplies();
   void CloseExpectQueue();
   int ReplyLogPriority(int);

   Expect *expect_chain;
   Expect **FindExpect(Packet *reply);
   void DeleteExpect(Expect **);

   char  **path_queue;
   int	 path_queue_len;
   void  PushDirectory(const char *);
   char  *PopDirectory();
   void	 EmptyPathQueue();

   int   RespQueueIsEmpty() { return expect_chain==0; }
   int	 RespQueueSize() { /*FIXME*/ }
   void  EmptyRespQueue() { /*FIXME*/ }

   void GetBetterConnection(int level,int count);
   void MoveConnectionHere(SFtp *o);

   bool	 eof;

   void	 SendRequest();
   void	 SendRequest(Packet *req,expect_t exp);

protected:
   void SetError(int code,const Packet *reply);
   void SetError(int code,const char *mess=0) { FA::SetError(code,mess); }

public:
   static void ClassInit();

   SFtp();
   SFtp(const SFtp*);
   ~SFtp();

   const char *GetProto() { return "sftp"; }

   FileAccess *Clone() { return new SFtp(this); }
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
   ListInfo *MakeListInfo(const char *dir);

   bool NeedSizeDateBeforehand() { return true; }

   void Suspend();
   void Resume();

   void Cleanup();
   void CleanupThis();
};

class SFtpDirList : public DirList
{
   FileAccess *session;
   Buffer *ubuf;
   char *pattern;

public:
   SFtpDirList(ArgV *a,FileAccess *fa);
   ~SFtpDirList();
   const char *Status();
   int Do();

   void Suspend();
   void Resume();
};

class SFtpListInfo : public GenericParseListInfo
{
   FileSet *Parse(const char *buf,int len);
public:
   SFtpListInfo(SFtp *session,const char *dir)
      : GenericParseListInfo(session,dir)
      {
	 can_get_prec_time=false;
      }
};

#endif
