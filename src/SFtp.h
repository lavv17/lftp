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
#include <sys/types.h>
#include <sys/stat.h>

class SFtp : public NetAccess
{
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
   SSH_FX_OK		     =0,
   SSH_FX_EOF		     =1,
   SSH_FX_NO_SUCH_FILE	     =2,
   SSH_FX_PERMISSION_DENIED  =3,
   SSH_FX_FAILURE	     =4,
   SSH_FX_BAD_MESSAGE	     =5,
   SSH_FX_NO_CONNECTION      =6,
   SSH_FX_CONNECTION_LOST    =7,
   SSH_FX_OP_UNSUPPORTED     =8,
   SSH_FX_INVALID_HANDLE     =9,
   SSH_FX_NO_SUCH_PATH       =10,
   SSH_FX_FILE_ALREADY_EXISTS=11,
   SSH_FX_WRITE_PROTECT      =12,
   SSH_FX_NO_MEDIA           =13
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
      CONNECTING_2,
      CONNECTED,
      FILE_RECV,
      FILE_SEND,
      WAITING,
      DONE
   };

   state_t state;
   bool received_greeting;
   unsigned ssh_id;
   char *handle;
   int handle_len;

   void Init();

   void	 Send(const char *format,...) PRINTF_LIKE(2,3);
   void	 SendMethod();
   void	 SendArrayInfoRequests();

   IOBuffer *send_buf;
   IOBuffer *recv_buf;
   bool recv_buf_suspended;
   IOBuffer *pty_send_buf;
   IOBuffer *pty_recv_buf;

   Buffer *file_buf;

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
      bool HasID() { return(type!=SSH_FXP_INIT && type!=SSH_FXP_VERSION); }
   public:
      Packet() { length=0; }
      virtual void ComputeLength() { length=1+4*HasID(); }
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
      const char *GetPacketTypeText();
      unsigned GetID() { return id; }
      void SetID(unsigned new_id) { id=new_id; }
      void DropData(Buffer *b) { b->Skip(4+(length>0?length:0)); }
      bool TypeIs(packet_type t) const { return type==t; }
      static unpack_status_t UnpackString(Buffer *b,int *offset,int limit,char **str_out,int *len_out=0);
      static void PackString(Buffer *b,const char *str,int len=-1);
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
      void ComputeLength() { Packet::ComputeLength(); length+=4; }
      void Pack(Buffer *b) { Packet::Pack(b); b->PackUINT32BE(data); }
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
	    string=(char*)xmalloc(l+1);
	    memcpy(string,s,l);
	    string[l]=0;
	    length+=4+l;
	 }
      unpack_status_t Unpack(Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    res=UnpackString(b,&unpacked,length+4,&string,&string_len);
	    return res;
	 }
      void ComputeLength() { Packet::ComputeLength(); length+=4+string_len; }
      void Pack(Buffer *b)
	 {
	    Packet::Pack(b);
	    Packet::PackString(b,string,string_len);
	 }
      const char *GetString() { return string; }
      int GetStringLength() { return string_len; }
   };
   class Request_INIT : public PacketUINT32
   {
   public:
      Request_INIT(int v) : PacketUINT32(SSH_FXP_INIT,v) {}
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
   class Request_STAT : public PacketSTRING
   {
   public:
      Request_STAT(const char *p) : PacketSTRING(SSH_FXP_STAT,p) {}
      const char *GetName() { return string; }
   };
   struct ExtFileAttr
   {
      char *extended_type;
      char *extended_data;
      ExtFileAttr() { extended_type=extended_data=0; }
      ~ExtFileAttr() { xfree(extended_type); xfree(extended_data); }
      unpack_status_t Unpack(Buffer *b,int *offset,int limit);
      void Pack(Buffer *b);
   };
   struct FileACE
   {
      unsigned ace_type;
      unsigned ace_flag;
      unsigned ace_mask;
      char     *who;
      FileACE() { ace_type=ace_flag=ace_mask=0; who=0; }
      ~FileACE() { xfree(who); }
      unpack_status_t Unpack(Buffer *b,int *offset,int limit);
      void Pack(Buffer *b);
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
      unsigned atime_nseconds;	    // present only if flag SUBSECOND_TIMES
      time_t   createtime;	    // present only if flag CREATETIME
      unsigned createtime_nseconds; // present only if flag SUBSECOND_TIMES
      time_t   mtime;		    // present only if flag MODIFYTIME (ACMODTIME)
      unsigned mtime_nseconds;	    // present only if flag SUBSECOND_TIMES
      unsigned ace_count;	    // present only if flag ACL
      FileACE  *ace;
      unsigned extended_count;	    // present only if flag EXTENDED
      ExtFileAttr *extended_attrs;

      FileAttrs()
      {
	 flags=0; type=0; size=NO_SIZE; owner=group=0; uid=gid=0;
	 permissions=0;
	 atime=createtime=mtime=NO_DATE;
	 atime_nseconds=createtime_nseconds=mtime_nseconds=0;
	 extended_count=0; extended_attrs=0;
	 ace_count=0; ace=0;
      }
      ~FileAttrs()
      {
	 xfree(owner); xfree(group);
	 delete[] extended_attrs;
	 delete[] ace;
      }
      unpack_status_t Unpack(Buffer *b,int *offset,int limit,int proto_version);
      void Pack(Buffer *b,int proto_version);
      int ComputeLength(int v)
	 {
	    Buffer *b=new Buffer;
	    Pack(b,v);
	    int size=b->Size();
	    Delete(b);
	    return size;
	 }
   };
   struct NameAttrs
   {
      char *name;
      char *longname;
      FileAttrs attrs;
      NameAttrs() { name=0; longname=0; }
      ~NameAttrs() { xfree(name); xfree(longname); }
      unpack_status_t Unpack(Buffer *b,int *offset,int limit,int proto_version);
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
   class Reply_ATTRS : public Packet
   {
      int protocol_version;
      FileAttrs attrs;
   public:
      Reply_ATTRS(int pv) : Packet(SSH_FXP_ATTRS) { protocol_version=pv; }
      unpack_status_t Unpack(Buffer *b);
      const FileAttrs *GetAttrs() { return &attrs; }
   };
   class Request_OPEN : public Packet
   {
      int protocol_version;
      char *filename;
      unsigned pflags;
   public:
      FileAttrs attrs;
      Request_OPEN(const char *fn,unsigned fl,int pv) : Packet(SSH_FXP_OPEN)
	 {
	    filename=xstrdup(fn);
	    pflags=fl;
	    protocol_version=pv;
	 }
      ~Request_OPEN() { xfree(filename); }
      void ComputeLength()
	 {
	    Packet::ComputeLength();
	    length+=4+strlen(filename)+4+attrs.ComputeLength(protocol_version);
	 }
      void Pack(Buffer *b)
	 {
	    Packet::Pack(b);
	    Packet::PackString(b,filename);
	    b->PackUINT32BE(pflags);
	    attrs.Pack(b,protocol_version);
	 }
   };
   class Reply_HANDLE : public PacketSTRING
   {
   public:
      Reply_HANDLE() : PacketSTRING(SSH_FXP_HANDLE) {}
      char *GetHandle(int *out_len=0)
	 {
	    char *out=(char*)xmemdup(string,string_len+1);
	    if(out_len)
	       *out_len=string_len;
	    return out;
	 }
   };
   class Request_CLOSE : public PacketSTRING
   {
   public:
      Request_CLOSE(const char *h,int len) : PacketSTRING(SSH_FXP_CLOSE,h,len) {}
   };
   class Request_FSTAT : public PacketSTRING
   {
   public:
      Request_FSTAT(const char *h,int len) : PacketSTRING(SSH_FXP_FSTAT,h,len) {}
   };
   class Reply_STATUS : public Packet
   {
      int protocol_version;
      unsigned code;
      char *message;
      char *language;
   public:
      Reply_STATUS(int pv) { protocol_version=pv; code=0; message=0; language=0; }
      ~Reply_STATUS() { xfree(message); xfree(language); }
      unpack_status_t Unpack(Buffer *b);
      int GetCode() { return code; }
      const char *GetCodeText();
      const char *GetMessage() { return message; }
   };
   class Request_READ : public PacketSTRING
   {
   public:
      off_t pos;
      unsigned len;
      Request_READ(const char *h,int hlen,off_t p,unsigned l)
       : PacketSTRING(SSH_FXP_READ,h,hlen) { pos=p; len=l; }
      void ComputeLength() { PacketSTRING::ComputeLength(); length+=8+4; }
      void Pack(Buffer *b);
   };
   class Reply_DATA : public PacketSTRING
   {
   public:
      Reply_DATA() : PacketSTRING(SSH_FXP_DATA) {}
      void GetData(const char **b,int *s) { *b=string; *s=string_len; }
   };

   enum expect_t
   {
      EXPECT_HOME_PATH,
      EXPECT_VERSION,
      EXPECT_CWD,
      EXPECT_HANDLE,
      EXPECT_HANDLE_STALE,
      EXPECT_DATA,
      EXPECT_RETR,
      EXPECT_INFO,
      EXPECT_DEFAULT,
      EXPECT_STOR_PRELIMINARY,
      EXPECT_STOR,
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
   int HandlePty();
   void HandleExpect(Expect *);
   void CloseExpectQueue();
   int ReplyLogPriority(int);

   Expect *expect_chain;
   Expect **expect_chain_end;
   Expect **FindExpect(Packet *reply);
   void DeleteExpect(Expect **);
   Expect *FindExpectExclusive(Packet *reply);
   Expect *ooo_chain; 	// out of order replies buffered

   int   RespQueueIsEmpty() { return expect_chain==0; }
   int	 RespQueueSize() { /*FIXME*/ }
   void  EmptyRespQueue()
      {
	 while(expect_chain)
	    DeleteExpect(&expect_chain);
	 while(ooo_chain)
	    DeleteExpect(&ooo_chain);
      }

   bool GetBetterConnection(int level,bool limit_reached);
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

// FIXME
class SFtpListInfo : public GenericParseListInfo
{
public:
   SFtpListInfo(SFtp *session,const char *dir)
      : GenericParseListInfo(session,dir)
      {
	 can_get_prec_time=false;
      }
};

#endif
