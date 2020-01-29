/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2012 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef SFTP_H
#define SFTP_H

#include "SSH_Access.h"
#include "StatusLine.h"
#include <sys/types.h>
#include <sys/stat.h>
#include "FileSet.h"

class SFtp : public SSH_Access
{
   int	 protocol_version;

   const char *lc_to_utf8(const char *);
   const char *utf8_to_lc(const char *);

public:
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
   SSH_FXP_RENAME   =18,   // v>=2
   SSH_FXP_READLINK =19,   // v>=3
   SSH_FXP_SYMLINK  =20,   // v<=5
   SSH_FXP_LINK     =21,   // v>=6
   SSH_FXP_BLOCK    =22,   // v>=6
   SSH_FXP_UNBLOCK  =23,   // v>=6

   SSH_FXP_STATUS   =101,
   SSH_FXP_HANDLE   =102,
   SSH_FXP_DATA     =103,
   SSH_FXP_NAME     =104,
   SSH_FXP_ATTRS    =105,
   SSH_FXP_EXTENDED =200,
   SSH_FXP_EXTENDED_REPLY=201
};

#define SSH_FILEXFER_ATTR_SIZE         0x00000001
#define SSH_FILEXFER_ATTR_UIDGID       0x00000002  // v<=3
#define SSH_FILEXFER_ATTR_PERMISSIONS  0x00000004
#define SSH_FILEXFER_ATTR_ACCESSTIME   0x00000008
#define SSH_FILEXFER_ATTR_ACMODTIME    0x00000008  // v<=3
#define SSH_FILEXFER_ATTR_CREATETIME   0x00000010
#define SSH_FILEXFER_ATTR_MODIFYTIME   0x00000020
#define SSH_FILEXFER_ATTR_ACL          0x00000040
#define SSH_FILEXFER_ATTR_OWNERGROUP   0x00000080
#define SSH_FILEXFER_ATTR_SUBSECOND_TIMES 0x00000100
#define SSH_FILEXFER_ATTR_BITS              0x00000200 // v>=5
#define SSH_FILEXFER_ATTR_ALLOCATION_SIZE   0x00000400 // v>=6
#define SSH_FILEXFER_ATTR_TEXT_HINT         0x00000800 // v>=6
#define SSH_FILEXFER_ATTR_MIME_TYPE         0x00001000 // v>=6
#define SSH_FILEXFER_ATTR_LINK_COUNT        0x00002000 // v>=6
#define SSH_FILEXFER_ATTR_UNTRANSLATED_NAME 0x00004000 // v>=6
#define SSH_FILEXFER_ATTR_CTIME             0x00008000 // v>=6
#define SSH_FILEXFER_ATTR_EXTENDED     0x80000000

#define SSH_FILEXFER_ATTR_MASK_V3      0x8000000F
#define SSH_FILEXFER_ATTR_MASK_V4      0x800001FD
#define SSH_FILEXFER_ATTR_MASK_V5      0x800003FD
#define SSH_FILEXFER_ATTR_MASK_V6      0x8000FFFD

// BITS values (v>=5)
#define SSH_FILEXFER_ATTR_FLAGS_READONLY         0x00000001
#define SSH_FILEXFER_ATTR_FLAGS_SYSTEM           0x00000002
#define SSH_FILEXFER_ATTR_FLAGS_HIDDEN           0x00000004
#define SSH_FILEXFER_ATTR_FLAGS_CASE_INSENSITIVE 0x00000008
#define SSH_FILEXFER_ATTR_FLAGS_ARCHIVE          0x00000010
#define SSH_FILEXFER_ATTR_FLAGS_ENCRYPTED        0x00000020
#define SSH_FILEXFER_ATTR_FLAGS_COMPRESSED       0x00000040
#define SSH_FILEXFER_ATTR_FLAGS_SPARSE           0x00000080
#define SSH_FILEXFER_ATTR_FLAGS_APPEND_ONLY      0x00000100
#define SSH_FILEXFER_ATTR_FLAGS_IMMUTABLE        0x00000200
#define SSH_FILEXFER_ATTR_FLAGS_SYNC             0x00000400

enum sftp_file_type {
   SSH_FILEXFER_TYPE_REGULAR	 =1,
   SSH_FILEXFER_TYPE_DIRECTORY	 =2,
   SSH_FILEXFER_TYPE_SYMLINK	 =3,
   SSH_FILEXFER_TYPE_SPECIAL	 =4,
   SSH_FILEXFER_TYPE_UNKNOWN	 =5,
   SSH_FILEXFER_TYPE_SOCKET      =6, // v>=5
   SSH_FILEXFER_TYPE_CHAR_DEVICE =7, // v>=5
   SSH_FILEXFER_TYPE_BLOCK_DEVICE=8, // v>=5
   SSH_FILEXFER_TYPE_FIFO        =9  // v>=5
};

// open modes (v<=4)
#define SSH_FXF_READ		       0x00000001
#define SSH_FXF_WRITE		       0x00000002
#define SSH_FXF_APPEND		       0x00000004
#define SSH_FXF_CREAT		       0x00000008
#define SSH_FXF_TRUNC		       0x00000010
#define SSH_FXF_EXCL		       0x00000020

// open flags values (v>=5)
#define SSH_FXF_ACCESS_DISPOSITION        0x00000007
#define     SSH_FXF_CREATE_NEW            0x00000000
#define     SSH_FXF_CREATE_TRUNCATE       0x00000001
#define     SSH_FXF_OPEN_EXISTING         0x00000002
#define     SSH_FXF_OPEN_OR_CREATE        0x00000003
#define     SSH_FXF_TRUNCATE_EXISTING     0x00000004
#define SSH_FXF_ACCESS_APPEND_DATA        0x00000008
#define SSH_FXF_ACCESS_APPEND_DATA_ATOMIC 0x00000010
#define SSH_FXF_ACCESS_TEXT_MODE          0x00000020
#define SSH_FXF_ACCESS_READ_LOCK          0x00000040
#define SSH_FXF_ACCESS_WRITE_LOCK         0x00000080
#define SSH_FXF_ACCESS_DELETE_LOCK        0x00000100
#define SSH_FXF_ACCESS_BLOCK_ADVISORY     0x00000200  // v>=6
#define SSH_FXF_ACCESS_NOFOLLOW           0x00000400  // v>=6
#define SSH_FXF_ACCESS_DELETE_ON_CLOSE    0x00000800  // v>=6

// ACL masks
#define ACE4_READ_DATA         0x00000001
#define ACE4_LIST_DIRECTORY    0x00000001
#define ACE4_WRITE_DATA        0x00000002
#define ACE4_ADD_FILE          0x00000002
#define ACE4_APPEND_DATA       0x00000004
#define ACE4_ADD_SUBDIRECTORY  0x00000004
#define ACE4_READ_NAMED_ATTRS  0x00000008
#define ACE4_WRITE_NAMED_ATTRS 0x00000010
#define ACE4_EXECUTE           0x00000020
#define ACE4_DELETE_CHILD      0x00000040
#define ACE4_READ_ATTRIBUTES   0x00000080
#define ACE4_WRITE_ATTRIBUTES  0x00000100
#define ACE4_DELETE            0x00010000
#define ACE4_READ_ACL          0x00020000
#define ACE4_WRITE_ACL         0x00040000
#define ACE4_WRITE_OWNER       0x00080000
#define ACE4_SYNCHRONIZE       0x00100000

// RENAME flags (v>=5)
#define SSH_FXF_RENAME_OVERWRITE  0x00000001
#define SSH_FXF_RENAME_ATOMIC     0x00000002
#define SSH_FXF_RENAME_NATIVE     0x00000004

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
   SSH_FX_NO_MEDIA           =13,
   SSH_FX_NO_SPACE_ON_FILESYSTEM    =14,
   SSH_FX_QUOTA_EXCEEDED	    =15,
   SSH_FX_UNKNOWN_PRINCIPAL	    =16,
   SSH_FX_LOCK_CONFLICT		    =17,
   SSH_FX_DIR_NOT_EMPTY		    =18,
   SSH_FX_NOT_A_DIRECTORY	    =19,
   SSH_FX_INVALID_FILENAME	    =20,
   SSH_FX_LINK_LOOP		    =21,
   SSH_FX_CANNOT_DELETE		    =22,
   SSH_FX_INVALID_PARAMETER	    =23,
   SSH_FX_FILE_IS_A_DIRECTORY	    =24,
   SSH_FX_BYTE_RANGE_LOCK_CONFLICT  =25,
   SSH_FX_BYTE_RANGE_LOCK_REFUSED   =26,
   SSH_FX_DELETE_PENDING            =27,
   SSH_FX_FILE_CORRUPT              =28,
   SSH_FX_OWNER_INVALID             =29,
   SSH_FX_GROUP_INVALID             =30
};

private:
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
   unsigned ssh_id;
   xstring handle;

   void Init();

   void	 SendMethod();
   void	 SendArrayInfoRequests();

   Ref<DirectedBuffer> send_translate;
   Ref<DirectedBuffer> recv_translate;

   Ref<Buffer> file_buf;
   Ref<FileSet> file_set;
   Timer flush_timer;

   void DisconnectLL();
   int IsConnected() const
      {
	 if(state==DISCONNECTED)
	    return 0;
	 if(state==CONNECTING)
	    return 1;
	 return 2;
      }

   const char *SkipHome(const char *path);
   const char *WirePath(const char *path);

public:
   enum unpack_status_t
   {
      UNPACK_SUCCESS=0,
      UNPACK_WRONG_FORMAT=-1,
      UNPACK_PREMATURE_EOF=-2,
      UNPACK_NO_DATA_YET=1
   };
   class Packet
   {
      static bool is_valid_reply(int p)
      {
	 return p==SSH_FXP_VERSION
	    || (p>=101 && p<=105)
	    || p==SSH_FXP_EXTENDED_REPLY;
      }
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
      bool HasID();
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
      virtual unpack_status_t Unpack(const Buffer *b);
      virtual ~Packet() {}
      int GetLength() { return length; }
      packet_type GetPacketType() { return type; }
      const char *GetPacketTypeText();
      unsigned GetID() const { return id; }
      const xstring& GetKey() { return xstring::get_tmp((const char*)&id,sizeof(id)); }
      void SetID(unsigned new_id) { id=new_id; }
      void DropData(Buffer *b) { b->Skip(4+(length>0?length:0)); }
      bool TypeIs(packet_type t) const { return type==t; }
      static unpack_status_t UnpackString(const Buffer *b,int *offset,int limit,xstring *str_out);
      static void PackString(Buffer *b,const char *str,int len=-1);
   };
private:
   unpack_status_t UnpackPacket(Buffer *,Packet **);
   class PacketUINT32 : public Packet
   {
   protected:
      unsigned data;
      PacketUINT32(packet_type t,unsigned d=0) : Packet(t)
	 { data=d; length+=4; }
      unpack_status_t Unpack(const Buffer *b)
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
      xstring string;
      PacketSTRING(packet_type t) : Packet(t)
	 {
	    length=4;
	 }
      PacketSTRING(packet_type t,const xstring &s) : Packet(t)
	 {
	    string.set(s);
	    length+=4+string.length();
	 }
      unpack_status_t Unpack(const Buffer *b);
      void ComputeLength() { Packet::ComputeLength(); length+=4+string.length(); }
      void Pack(Buffer *b)
	 {
	    Packet::Pack(b);
	    Packet::PackString(b,string,string.length());
	 }
      const char *GetString() { return string; }
      int GetStringLength() { return string.length(); }
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
      unpack_status_t Unpack(const Buffer *b)
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
   class Request_FSTAT : public PacketSTRING
   {
      unsigned flags;
      int protocol_version;
   public:
      Request_FSTAT(const xstring &h,unsigned f,int pv) : PacketSTRING(SSH_FXP_FSTAT,h)
	 {
	    flags=f;
	    protocol_version=pv;
	 }
      void ComputeLength()
	 {
	    PacketSTRING::ComputeLength();
	    if(protocol_version>=4)
	       length+=4;
	 }
      void Pack(Buffer *b)
	 {
	    PacketSTRING::Pack(b);
	    if(protocol_version>=4)
	       b->PackUINT32BE(flags);
	 }
   };
   class Request_STAT : public Request_FSTAT
   {
   public:
      Request_STAT(const char *p,unsigned f,int pv) : Request_FSTAT(p,f,pv)
	 {
	    type=SSH_FXP_STAT;
	 }
      const char *GetName() { return string; }
   };
public:
   struct FileAttrs
   {
      struct ExtFileAttr
      {
	 xstring extended_type;
	 xstring extended_data;
	 unpack_status_t Unpack(const Buffer *b,int *offset,int limit);
	 void Pack(Buffer *b);
      };
      struct FileACE
      {
	 unsigned ace_type;
	 unsigned ace_flag;
	 unsigned ace_mask;
	 xstring who;
	 FileACE() { ace_type=ace_flag=ace_mask=0; }
	 unpack_status_t Unpack(const Buffer *b,int *offset,int limit);
	 void Pack(Buffer *b);
      };

      unsigned flags;
      int      type;		    // v>=4
      off_t    size;		    // present only if flag SIZE
      xstring  owner;		    // present only if flag OWNERGROUP // v>=4
      xstring  group;		    // present only if flag OWNERGROUP // v>=4
      unsigned uid;		    // present only if flag UIDGID // v<=3
      unsigned gid;		    // present only if flag UIDGID // v<=3
      unsigned permissions;	    // present only if flag PERMISSIONS
      time_t   atime;		    // present only if flag ACCESSTIME (ACMODTIME)
      unsigned atime_nseconds;	    // present only if flag SUBSECOND_TIMES
      time_t   createtime;	    // present only if flag CREATETIME
      unsigned createtime_nseconds; // present only if flag SUBSECOND_TIMES
      time_t   mtime;		    // present only if flag MODIFYTIME (ACMODTIME)
      unsigned mtime_nseconds;	    // present only if flag SUBSECOND_TIMES
      time_t   ctime;		    // present only if flag CTIME // v>=6
      unsigned ctime_nseconds;	    // present only if flag SUBSECOND_TIMES // v>=6
      unsigned ace_count;	    // present only if flag ACL
      FileACE  *ace;
      unsigned attrib_bits;         // if flag BITS		  // v>=5
      unsigned attrib_bits_valid;   // if flag BITS		  // v>=6
      unsigned char text_hint;      // if flag TEXT_HINT	  // v>=6
      xstring  mime_type;	    // if flag MIME_TYPE	  // v>=6
      unsigned link_count;          // if flag LINK_COUNT	  // v>=6
      xstring  untranslated_name;   // if flag UNTRANSLATED_NAME  // v>=6
      unsigned extended_count;	    // present only if flag EXTENDED
      ExtFileAttr *extended_attrs;

      FileAttrs()
      {
	 flags=0; type=0; size=NO_SIZE; uid=gid=0;
	 permissions=0;
	 atime=createtime=mtime=ctime=NO_DATE;
	 atime_nseconds=createtime_nseconds=mtime_nseconds=ctime_nseconds=0;
	 extended_count=0; extended_attrs=0;
	 ace_count=0; ace=0;
	 attrib_bits=attrib_bits_valid=0; text_hint=0;
	 link_count=0;
      }
      ~FileAttrs();
      unpack_status_t Unpack(const Buffer *b,int *offset,int limit,int proto_version);
      void Pack(Buffer *b,int proto_version);
      int ComputeLength(int v);
   };
   struct NameAttrs
   {
      xstring name;
      xstring longname;
      FileAttrs attrs;
      unpack_status_t Unpack(const Buffer *b,int *offset,int limit,int proto_version);
   };
private:
   class Reply_NAME : public Packet
   {
      int protocol_version;
      int count;
      NameAttrs *names;
      bool eof;
   public:
      Reply_NAME(int pv) : Packet(SSH_FXP_NAME) { protocol_version=pv; eof=false; }
      ~Reply_NAME() { delete[] names; }
      unpack_status_t Unpack(const Buffer *b);
      int GetCount() { return count; }
      const NameAttrs *GetNameAttrs(int index)
	 {
	    if(index>count)
	       return 0;
	    return &names[index];
	 }
      bool Eof() { return eof; }
   };
   class Reply_ATTRS : public Packet
   {
      int protocol_version;
      FileAttrs attrs;
   public:
      Reply_ATTRS(int pv) : Packet(SSH_FXP_ATTRS) { protocol_version=pv; }
      unpack_status_t Unpack(const Buffer *b);
      const FileAttrs *GetAttrs() { return &attrs; }
   };
   class PacketSTRING_ATTRS : public PacketSTRING
   {
   protected:
      int protocol_version;
   public:
      FileAttrs attrs;
      PacketSTRING_ATTRS(packet_type type,const xstring &h,int pv)
       : PacketSTRING(type,h)
	 {
	    protocol_version=pv;
	 }
      void ComputeLength()
	 {
	    PacketSTRING::ComputeLength();
	    length+=attrs.ComputeLength(protocol_version);
	 }
      void Pack(Buffer *b)
	 {
	    PacketSTRING::Pack(b);
	    attrs.Pack(b,protocol_version);
	 }
   };
   class Request_FSETSTAT : public PacketSTRING_ATTRS
   {
   public:
      Request_FSETSTAT(const xstring &h,int pv)
       : PacketSTRING_ATTRS(SSH_FXP_FSETSTAT,h,pv) {}
   };
   class Request_OPEN : public PacketSTRING_ATTRS
   {
      unsigned pflags;		 // v<=4
      unsigned desired_access;	 // v>=5
      unsigned flags;		 // v>=5
   public:
      Request_OPEN(const char *fn,unsigned pf,unsigned da,unsigned f,int pv)
       : PacketSTRING_ATTRS(SSH_FXP_OPEN,fn,pv)
	 {
	    pflags=pf;
	    desired_access=da;
	    flags=f;
	 }
      void ComputeLength()
	 {
	    PacketSTRING_ATTRS::ComputeLength();
	    length+=4+4*(protocol_version>=5);
	 }
      void Pack(Buffer *b);
   };
   class Reply_HANDLE : public PacketSTRING
   {
   public:
      Reply_HANDLE() : PacketSTRING(SSH_FXP_HANDLE) {}
      const xstring &GetHandle() { return string; }
   };
   class Request_CLOSE : public PacketSTRING
   {
   public:
      Request_CLOSE(const xstring &h) : PacketSTRING(SSH_FXP_CLOSE,h) {}
   };
   class Request_OPENDIR : public PacketSTRING
   {
   public:
      Request_OPENDIR(const char *name) : PacketSTRING(SSH_FXP_OPENDIR,name) {}
   };
   class Request_READDIR : public PacketSTRING
   {
   public:
      Request_READDIR(const xstring &h) : PacketSTRING(SSH_FXP_READDIR,h) {}
   };
   class Reply_STATUS : public Packet
   {
      int protocol_version;
      unsigned code;
      xstring message;
      xstring language;
   public:
      Reply_STATUS(int pv) { protocol_version=pv; code=0; }
      unpack_status_t Unpack(const Buffer *b);
      int GetCode() { return code; }
      const char *GetCodeText();
      const char *GetMessage() { return message; }
   };
   class Request_READ : public PacketSTRING
   {
   public:
      off_t pos;
      unsigned len;
      Request_READ(const xstring &h,off_t p,unsigned l)
       : PacketSTRING(SSH_FXP_READ,h) { pos=p; len=l; }
      void ComputeLength() { PacketSTRING::ComputeLength(); length+=8+4; }
      void Pack(Buffer *b);
   };
   class Reply_DATA : public PacketSTRING
   {
      bool eof;
   public:
      Reply_DATA() : PacketSTRING(SSH_FXP_DATA) { eof=false; }
      void GetData(const char **b,int *s) { *b=string; *s=string.length(); }
      unpack_status_t Unpack(const Buffer *b);
      bool Eof() { return eof; }
   };
   class Request_WRITE : public PacketSTRING
   {
   public:
      off_t pos;
      xstring data;
      Request_WRITE(const xstring &h,off_t p,const char *d,unsigned l)
       : PacketSTRING(SSH_FXP_WRITE,h) { pos=p; data.nset(d,l); }
      void ComputeLength() { PacketSTRING::ComputeLength(); length+=8+4+data.length(); }
      void Pack(Buffer *b);
   };
   class Request_MKDIR : public PacketSTRING_ATTRS
   {
   public:
      Request_MKDIR(const char *fn,int pv)
       : PacketSTRING_ATTRS(SSH_FXP_MKDIR,fn,pv) {}
   };
   class Request_SETSTAT : public PacketSTRING_ATTRS
   {
   public:
      Request_SETSTAT(const char *fn,int pv)
       : PacketSTRING_ATTRS(SSH_FXP_SETSTAT,fn,pv) {}
   };
   class Request_RMDIR : public PacketSTRING
   {
   public:
      Request_RMDIR(const char *fn) : PacketSTRING(SSH_FXP_RMDIR,fn) {}
   };
   class Request_REMOVE : public PacketSTRING
   {
   public:
      Request_REMOVE(const char *fn) : PacketSTRING(SSH_FXP_REMOVE,fn) {}
   };
   class Request_RENAME : public Packet
   {
      int protocol_version;
      xstring oldpath;
      xstring newpath;
      unsigned flags;
   public:
      Request_RENAME(const char *o,const char *n,unsigned f,int pv)
      : Packet(SSH_FXP_RENAME), oldpath(o), newpath(n)
	 {
	    protocol_version=pv;
	    flags=f;
	 }
      void ComputeLength();
      void Pack(Buffer *b);
   };
   class Request_READLINK : public PacketSTRING
   {
   public:
      Request_READLINK(const char *name) : PacketSTRING(SSH_FXP_READLINK,name) {}
   };
   class Request_SYMLINK : public Packet
   {
      xstring oldpath;
      xstring newpath;
   public:
      Request_SYMLINK(const char *o,const char *n)
      : Packet(SSH_FXP_SYMLINK), oldpath(o), newpath(n) {}
      void ComputeLength()
	 {
	    Packet::ComputeLength();
	    length+=4+strlen(oldpath)+4+strlen(newpath);
	 }
      void Pack(Buffer *b);
   };
   class Request_LINK : public Packet
   {
      xstring oldpath;
      xstring newpath;
      bool symbolic;
   public:
      Request_LINK(const char *o,const char *n,bool s)
      : Packet(SSH_FXP_LINK), oldpath(o), newpath(n), symbolic(s) {}
      void ComputeLength()
	 {
	    Packet::ComputeLength();
	    length+=4+strlen(oldpath)+4+strlen(newpath)+1;
	 }
      void Pack(Buffer *b);
   };

   struct Expect;
   friend struct SFtp::Expect; // grant access to Packet.
   struct Expect
   {
      enum expect_t
      {
	 HOME_PATH,
	 FXP_VERSION,
	 CWD,
	 HANDLE,
	 HANDLE_STALE,
	 DATA,
	 INFO,
	 INFO_READLINK,
	 DEFAULT,
	 WRITE_STATUS,
	 IGNORE
      };

      Ref<Packet> request;
      Ref<Packet> reply;
      int i;
      expect_t tag;
      Expect(Packet *req,expect_t t,int j=0) : request(req), i(j), tag(t) {}

      bool has_data_at_pos(off_t pos) const {
	 if(!reply->TypeIs(SSH_FXP_DATA) || !request->TypeIs(SSH_FXP_READ))
	    return false;
	 return request.Cast<Request_READ>()->pos==pos;
      }
   };

   void PushExpect(Expect *);
   int HandleReplies();
   int HandlePty();
   void HandleExpect(Expect *);
   void CloseExpectQueue();
   bool HasExpect(Expect::expect_t tag);
   bool HasExpectBefore(unsigned id,Expect::expect_t tag);
   void CloseHandle(Expect::expect_t e);
   int ReplyLogPriority(int);

   xmap_p<Expect> expect_queue;

   Expect *FindExpectExclusive(Packet *reply);
   xarray_p<Expect> ooo_chain; 	// out of order replies buffered

   int	 RespQueueSize() const { return expect_queue.count(); }
   int   RespQueueIsEmpty() const { return RespQueueSize()==0; }
   void  EmptyRespQueue() {
      expect_queue.empty();
      ooo_chain.truncate();
   }

   bool GetBetterConnection(int level,bool limit_reached);
   void MoveConnectionHere(SFtp *o);

   bool	 eof;

   void	 SendRequest();
   void	 SendRequest(Packet *req,Expect::expect_t exp,int i=0);
   void	 SendRequestGeneric(int type);
   void	 RequestMoreData();
   off_t request_pos;

   void MergeAttrs(FileInfo *fi,const FileAttrs *a);
   FileInfo *MakeFileInfo(const NameAttrs *a);

   int max_packets_in_flight;
   int max_packets_in_flight_slow_start;
   int size_read;
   int size_write;
   bool use_full_path;

protected:
   void SetError(int code,const Packet *reply);
   void SetError(int code,const char *mess=0) { FA::SetError(code,mess); }

public:
   static void ClassInit();

   SFtp();
   SFtp(const SFtp*);
   ~SFtp();

   const char *GetProto() const { return "sftp"; }

   FileAccess *Clone() const { return new SFtp(this); }
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
   ListInfo *MakeListInfo(const char *dir);

   bool NeedSizeDateBeforehand() { return false; }

   void SuspendInternal();
   void ResumeInternal();

   FileSet *GetFileSet();
};

class SFtpDirList : public DirList
{
   SMTaskRef<IOBuffer> ubuf;
   const char *dir;
   bool use_file_set;
   Ref<FileSet> fset;
   LsOptions ls_options;

public:
   SFtpDirList(SFtp *s,ArgV *a);
   const char *Status();
   int Do();

   void SuspendInternal();
   void ResumeInternal();
};

class SFtpListInfo : public ListInfo
{
   SMTaskRef<IOBuffer> ubuf;

public:
   SFtpListInfo(SFtp *session,const char *dir)
      : ListInfo(session,dir) {}
   int Do();
   const char *Status();
};

#endif
