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

/* $Id: Torrent.h,v 1.32 2011/05/10 05:55:14 lav Exp $ */

#ifndef TORRENT_H
#define TORRENT_H

#include "FileAccess.h"
#include "Bencode.h"
#include "Error.h"
#include "ProtoLog.h"
#include "network.h"
#include "RateLimit.h"

class FDCache;
class TorrentBlackList;
class Torrent;
class TorrentPeer;

class BitField : public xarray<unsigned char>
{
   int bit_length;
public:
   BitField() { bit_length=0; }
   BitField(int bits);
   bool valid_index(int i) const {
      return i>=0 && i<bit_length;
   }
   bool get_bit(int i) const;
   void set_bit(int i,bool value);
   bool has_any_set(int from,int to) const;
   bool has_all_set(int from,int to) const;
   bool has_any_set() const { return has_any_set(0,bit_length); }
   bool has_all_set() const { return has_all_set(0,bit_length); }
   int get_bit_length() const { return bit_length; }
   void set_bit_length(int b) { bit_length=b; set_length((b+7)/8); }
   void clear() { memset(buf,0,length()); }
};

struct TorrentPiece
{
   unsigned sources_count;	    // how many peers have the piece

   BitField block_map;		    // which blocks are present
   xarray<const TorrentPeer*> downloader; // which peers download the blocks

   TorrentPiece(unsigned b)
      : sources_count(0), block_map(b)
      { downloader.allocate(b,0); }

   bool has_a_downloader() const;
};

class TorrentListener : public SMTask, protected ProtoLog, protected Networker
{
   Ref<Error> error;
   int af;
   int sock;
   sockaddr_u addr;
   Speedometer rate;
   void FillAddress(int port);
public:
   TorrentListener(int a);
   ~TorrentListener();
   int Do();
   int GetPort() const { return addr.port(); }
   const char *GetAddress() const { return addr.address(); }
};

class TorrentTracker : public SMTask, protected ProtoLog
{
   friend class Torrent;

   Torrent *parent;

   xstring tracker_url;
   FileAccessRef t_session;
   Timer tracker_timer;
   SMTaskRef<IOBuffer> tracker_reply;
   xstring tracker_id;
   bool started;
   Ref<Error> error;
   int tracker_no;

   TorrentTracker(Torrent *p,const char *url);
   void AddURL(const char *url);
   int Do();

   void Start();
   void Shutdown();

   void SendTrackerRequest(const char *event);
   int HandleTrackerReply();

   void SetError(const char *e) { error=new Error(-1,e,true); }
   bool Failed() const { return error!=0; }
   const char *ErrorText() const { return error->Text(); }

public:
   ~TorrentTracker() {}
   const char *NextRequestIn() const {
      return tracker_timer.TimeLeft().toString(
	 TimeInterval::TO_STR_TRANSLATE|TimeInterval::TO_STR_TERSE);
   }
   const char *GetURL() const { return tracker_url; }
   const char *Status() const;
};

class Torrent : public SMTask, protected ProtoLog, public ResClient
{
   friend class TorrentPeer;
   friend class TorrentDispatcher;

   bool shutting_down;
   bool complete;
   bool end_game;
   bool validating;
   bool force_valid;
   unsigned validate_index;
   Ref<Error> invalid_cause;

   static const unsigned PEER_ID_LEN = 20;
   static xstring my_peer_id;
   static xstring my_key;
   static xmap<Torrent*> torrents;
   static Ref<TorrentListener> listener;
   static Ref<TorrentListener> listener_ipv6;
   static Ref<FDCache> fd_cache;
   static Ref<TorrentBlackList> black_list;

   static Torrent *FindTorrent(const xstring& info_hash) { return torrents.lookup(info_hash); }
   static void AddTorrent(Torrent *t) { torrents.add(t->GetInfoHash(),t); }
   static void RemoveTorrent(Torrent *t) { torrents.remove(t->GetInfoHash()); }
   static int GetTorrentsCount() { return torrents.count(); }
   static void Dispatch(const xstring& info_hash,int s,const sockaddr_u *remote_addr,IOBuffer *recv_buf);

   xstring_c metainfo_url;
   FileAccessRef metainfo_fa;
   SMTaskRef<IOBuffer> metainfo_data;
   Ref<BeNode> metainfo_tree;
   BeNode *info;
   xstring info_hash;
   const xstring *pieces;
   const xstring *name;

   Ref<DirectedBuffer> recv_translate;
   void InitTranslation();
   void TranslateString(BeNode *node) const;

   TaskRefArray<TorrentTracker> trackers;
   bool TrackersDone() const;
   bool TrackersFailed() const;
   void StartTrackers() const;
   void ShutdownTrackers() const;
   void SendTrackersRequest(const char *e) const;

   bool single_file;
   unsigned piece_length;
   unsigned last_piece_length;
   unsigned total_pieces;
   unsigned complete_pieces;
   Ref<BitField> my_bitfield;

   static const unsigned BLOCK_SIZE = 0x4000;

   unsigned long long total_length;
   unsigned long long total_recv;
   unsigned long long total_sent;
   unsigned long long total_left;

   void SetError(Error *);
   void SetError(const char *);

   BeNode *Lookup(xmap_p<BeNode>& d,const char *name,BeNode::be_type_t type);
   BeNode *Lookup(BeNode *d,const char *name,BeNode::be_type_t type) { return Lookup(d->dict,name,type); }
   BeNode *Lookup(Ref<BeNode>& d,const char *name,BeNode::be_type_t type) { return Lookup(d->dict,name,type); }

   TaskRefArray<TorrentPeer> peers;
   RefArray<TorrentPiece> piece_info;
   static int PeersCompareActivity(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);
   static int PeersCompareRecvRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);
   static int PeersCompareSendRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);

   Timer pieces_needed_rebuild_timer;
   xarray<unsigned> pieces_needed;
   static int PiecesNeededCmp(const unsigned *a,const unsigned *b);
   unsigned last_piece;

   void SetPieceNotWanted(unsigned piece);
   void SetDownloader(unsigned piece,unsigned block,const TorrentPeer *o,const TorrentPeer *n);

   xstring_c cwd;
   xstring_c output_dir;

   const char *FindFileByPosition(unsigned piece,unsigned begin,off_t *f_pos,off_t *f_tail) const;
   const char *MakePath(BeNode *p) const;
   int OpenFile(const char *f,int m,off_t size=0);
   void CloseFile(const char *f) const;

   void StoreBlock(unsigned piece,unsigned begin,unsigned len,const char *buf,TorrentPeer *src_peer);
   const xstring& RetrieveBlock(unsigned piece,unsigned begin,unsigned len);

   Speedometer recv_rate;
   Speedometer send_rate;

   RateLimit rate_limit;
   bool RateLow(RateLimit::dir_t dir) { return rate_limit.Relaxed(dir); }

   int connected_peers_count;
   int active_peers_count;
   int complete_peers_count;
   int am_interested_peers_count;
   int am_not_choking_peers_count;
   int max_peers;
   int seed_min_peers;

   bool SeededEnough() const;
   float stop_on_ratio;
   Timer seed_timer;

   Timer decline_timer;
   Timer optimistic_unchoke_timer;
   Timer peers_scan_timer;
   Timer am_interested_timer;

   static const int max_uploaders = 20;
   static const int min_uploaders = 1;
   static const int max_downloaders = 20;
   static const int min_downloaders = 4;

   bool NeedMoreUploaders();
   bool AllowMoreDownloaders();
   void UnchokeBestUploaders();
   void ScanPeers();
   void OptimisticUnchoke();
   void ReducePeers();
   void ReduceUploaders();
   void ReduceDownloaders();

   int PeerBytesAllowed(const TorrentPeer *peer,RateLimit::dir_t dir);
   void PeerBytesUsed(int b,RateLimit::dir_t dir);
   void PeerBytesGot(int b) { PeerBytesUsed(b,RateLimit::GET); }

   static void BlackListPeer(const TorrentPeer *peer,const char *timeout);
   TorrentPeer *FindPeerById(const xstring& p_id);

public:
   static void ClassInit();

   Torrent(const char *mf,const char *cwd,const char *output_dir);

   int Do();
   int Done() const;

   const xstring& Status();

   const Error *GetInvalidCause() const { return invalid_cause; }

   void Shutdown();
   bool ShuttingDown() { return shutting_down; }
   void PrepareToDie();

   bool CanAccept() const;
   void Accept(int s,const sockaddr_u *a,IOBuffer *rb);
   static bool NoTorrentCanAccept();

   static void SHA1(const xstring& str,xstring& buf);
   void ValidatePiece(unsigned p);
   unsigned PieceLength(unsigned p) const { return p==total_pieces-1 ? last_piece_length : piece_length; }
   unsigned BlocksInPiece(unsigned p) const { return (PieceLength(p)+BLOCK_SIZE-1)/BLOCK_SIZE; }

   const TaskRefArray<TorrentPeer>& GetPeers() const { return peers; }
   void AddPeer(TorrentPeer *);
   void CleanPeers();

   const xstring& GetInfoHash() const { return info_hash; }
   int GetPeersCount() const { return peers.count(); }
   int GetConnectedPeersCount() const { return connected_peers_count; }
   int GetActivePeersCount() const { return active_peers_count; }
   int GetCompletePeersCount() const { return complete_peers_count; }

   bool Complete() const { return complete; }
   double GetRatio() const;
   unsigned long long TotalLength() const { return total_length; }
   unsigned PieceLength() const { return piece_length; }
   const char *GetName() const { return name?name->get():metainfo_url.get(); }

   void Reconfig(const char *name);
   const char *GetLogContext() { return GetName(); }

   void ForceValid() { force_valid=true; }
   bool IsValidating() const { return validating; }

   static int GetPort();
   static int GetPortIPv4() { return listener?listener->GetPort():0; }
   static int GetPortIPv6() { return listener_ipv6?listener_ipv6->GetPort():0; }
   static const char *GetAddressIPv6() { return listener_ipv6?listener_ipv6->GetAddress():"::"; }
   int GetWantedPeersCount() const;
   static const xstring& GetMyPeerId() { return my_peer_id; }
   static const xstring& GetMyKey() { return my_key; }

   unsigned long long GetTotalSent() { return total_sent; }
   unsigned long long GetTotalRecv() { return total_recv; }
   unsigned long long GetTotalLeft() { return total_left; }

   const TaskRefArray<TorrentTracker>& Trackers() { return trackers; }
};

class FDCache : public SMTask, public ResClient
{
   struct FD
   {
      int fd;
      int saved_errno;
      time_t last_used;
   };
   int max_count;
   int max_time;
   xmap<FD> cache[3];
   Timer clean_timer;

public:
   int OpenFile(const char *name,int mode,off_t size=0);
   void Close(const char *name);
   int Count() const;
   void Clean();
   bool CloseOne();
   void CloseAll();
   FDCache();
   ~FDCache();

   int Do();
};

class TorrentPeer : public SMTask, protected ProtoLog, public Networker
{
   friend class Torrent;

   Ref<Error> error;
   Torrent *parent;

   int tracker_no;

   sockaddr_u addr;
   int sock;
   bool connected;
   bool passive;

   Timer timeout_timer;
   Timer retry_timer;
   Timer keepalive_timer;
   Timer choke_timer;
   Timer interest_timer;
   Timer activity_timer;

   Ref<IOBuffer> recv_buf;
   Ref<IOBuffer> send_buf;

   unsigned long long peer_recv;
   unsigned long long peer_sent;

   Speedometer peer_recv_rate;
   Speedometer peer_send_rate;

   xstring peer_id;
   unsigned char extensions[8];
   TorrentPeer *duplicate;
   bool myself;

   bool FastExtensionEnabled() { return extensions[7]&0x04; }
   bool LTEPExtensionEnabled() { return extensions[5]&0x10; }

   bool am_choking;
   bool am_interested;
   bool peer_choking;
   bool peer_interested;

   Ref<BitField> peer_bitfield;
   unsigned peer_complete_pieces;

   xqueue<unsigned,xarray<unsigned> > fast_set;
   bool InFastSet(unsigned);
   xqueue<unsigned,xarray<unsigned> > suggested_set;

   enum packet_type
   {
      MSG_KEEPALIVE=-1,
      MSG_CHOKE=0,
      MSG_UNCHOKE=1,
      MSG_INTERESTED=2,
      MSG_UNINTERESTED=3,
      MSG_HAVE=4,
      MSG_BITFIELD=5,
      MSG_REQUEST=6,
      MSG_PIECE=7,
      MSG_CANCEL=8,
      MSG_PORT=9,
      MSG_SUGGEST_PIECE=13,
      MSG_HAVE_ALL=14,
      MSG_HAVE_NONE=15,
      MSG_REJECT_REQUEST=16,
      MSG_ALLOWED_FAST=17,
      MSG_EXTENDED=20,
   };
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
	 return (p>=0 && p<=MSG_PORT)
	    || (p>=MSG_SUGGEST_PIECE && p<=MSG_ALLOWED_FAST)
	    || p==MSG_EXTENDED;
      }
   protected:
      int length;
      int unpacked;
      packet_type type;
   public:
      Packet(packet_type t);
      Packet() { length=0; }
      virtual void ComputeLength() { length=(type>=0); }
      virtual void Pack(Ref<IOBuffer>& b);
      virtual unpack_status_t Unpack(const Buffer *b);
      virtual ~Packet() {}
      int GetLength() const { return length; }
      packet_type GetPacketType() const { return type; }
      const char *GetPacketTypeText() const;
      void DropData(Ref<IOBuffer>& b) { b->Skip(4+length); }
      bool TypeIs(packet_type t) const { return type==t; }
      static unpack_status_t UnpackBencoded(const Buffer *b,int *offset,int limit,Ref<BeNode> *out);
   };
   class _PacketPiece : public Packet
   {
   public:
      unsigned piece;
      _PacketPiece(packet_type t,unsigned p) : Packet(t), piece(p) { length+=4; }
      unpack_status_t Unpack(const Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    piece=b->UnpackUINT32BE(unpacked);
	    unpacked+=4;
	    return UNPACK_SUCCESS;
	 }
      void ComputeLength() { Packet::ComputeLength(); length+=4; }
      void Pack(Ref<IOBuffer>& b) { Packet::Pack(b); b->PackUINT32BE(piece); }
   };
   class PacketHave : public _PacketPiece {
   public:
      PacketHave(unsigned p=0) : _PacketPiece(MSG_HAVE,p) {}
   };
   class PacketBitField : public Packet
   {
   public:
      Ref<BitField> bitfield;
      PacketBitField() : Packet(MSG_BITFIELD) {}
      PacketBitField(const BitField *bf);
      ~PacketBitField();
      unpack_status_t Unpack(const Buffer *b);
      void ComputeLength();
      void Pack(Ref<IOBuffer>& b);
   };
   class _PacketIBL : public Packet
   {
   public:
      unsigned index,begin,req_length;
      _PacketIBL(packet_type t,unsigned i,unsigned b,unsigned l);
      unpack_status_t Unpack(const Buffer *b);
      void ComputeLength();
      void Pack(Ref<IOBuffer>& b);
   };
   class PacketRequest : public _PacketIBL
   {
   public:
      PacketRequest(unsigned i=0,unsigned b=0,unsigned l=0)
	 : _PacketIBL(MSG_REQUEST,i,b,l) {}
   };
   class PacketCancel : public _PacketIBL {
   public:
      PacketCancel(unsigned i=0,unsigned b=0,unsigned l=0)
	 : _PacketIBL(MSG_CANCEL,i,b,l) {}
   };
   class PacketPiece : public Packet
   {
   public:
      unsigned index,begin;
      xstring data;
      PacketPiece() : Packet(MSG_PIECE), index(0), begin(0) {}
      PacketPiece(unsigned i,unsigned b,const xstring &s)
	 : Packet(MSG_PIECE), index(i), begin(b) { data.set(s); length+=8+data.length(); }
      unpack_status_t Unpack(const Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    index=b->UnpackUINT32BE(unpacked);unpacked+=4;
	    begin=b->UnpackUINT32BE(unpacked);unpacked+=4;
	    unsigned bytes=length+4-unpacked;
	    data.nset(b->Get()+unpacked,bytes);
	    unpacked+=bytes;
	    return UNPACK_SUCCESS;
	 }
      void ComputeLength() { Packet::ComputeLength(); length+=8+data.length(); }
      void Pack(Ref<IOBuffer>& b) {
	 Packet::Pack(b);
	 b->PackUINT32BE(index);
	 b->PackUINT32BE(begin);
	 b->Put(data);
      }
   };
   class PacketPort : public Packet
   {
   public:
      unsigned port;
      PacketPort(unsigned p=0) : Packet(MSG_PORT), port(p) { length+=2; }
      unpack_status_t Unpack(const Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    port=b->UnpackUINT16BE(unpacked);
	    unpacked+=2;
	    return UNPACK_SUCCESS;
	 }
      void ComputeLength() { Packet::ComputeLength(); length+=2; }
      void Pack(Ref<IOBuffer>& b) { Packet::Pack(b); b->PackUINT16BE(port); }
   };
   class PacketSuggestPiece : public _PacketPiece {
   public:
      PacketSuggestPiece(unsigned p=0) : _PacketPiece(MSG_SUGGEST_PIECE,p) {}
   };
   class PacketAllowedFast : public _PacketPiece {
   public:
      PacketAllowedFast(unsigned p=0) : _PacketPiece(MSG_ALLOWED_FAST,p) {}
   };
   class PacketRejectRequest : public _PacketIBL {
   public:
      PacketRejectRequest(unsigned i=0,unsigned b=0,unsigned l=0)
	 : _PacketIBL(MSG_REJECT_REQUEST,i,b,l) {}
   };
   class PacketExtended : public Packet
   {
   public:
      unsigned char code;
      Ref<BeNode> data;
      PacketExtended(unsigned char c='\0',BeNode *d=0)
	 : Packet(MSG_EXTENDED), code(c), data(d) { length++; if(data) length+=data->ComputeLength(); }
      unpack_status_t Unpack(const Buffer *b)
	 {
	    unpack_status_t res;
	    res=Packet::Unpack(b);
	    if(res!=UNPACK_SUCCESS)
	       return res;
	    code=b->UnpackUINT8(unpacked); unpacked++;
	    res=UnpackBencoded(b,&unpacked,length+4,&data);
	    return res;
	 }
      void ComputeLength() { Packet::ComputeLength(); length++; if(data) length+=data->ComputeLength(); }
      void Pack(Ref<IOBuffer>& b) { Packet::Pack(b); b->PackUINT8(code); if(data) data->Pack(b); }
   };

private:
   unpack_status_t UnpackPacket(Ref<IOBuffer>& ,Packet **);
   void HandlePacket(Packet *);

   static const int MAX_QUEUE_LEN = 16;
   RefQueue<PacketRequest> recv_queue;
   RefQueue<PacketRequest> sent_queue;

   unsigned last_piece;
   static const unsigned NO_PIECE = ~0U;
   void SetLastPiece(unsigned p);
   unsigned GetLastPiece() const;
   bool HasNeededPieces();
   void SetPieceHaving(unsigned p,bool have);
   void SetAmInterested(bool);
   void SetAmChoking(bool);

   void ClearSentQueue(int i);
   void ClearSentQueue() { ClearSentQueue(sent_queue.count()-1); }

   int FindRequest(unsigned piece,unsigned begin) const;

   void SetError(const char *);
   void SendHandshake();
   unpack_status_t RecvHandshake();
   void SendExtensions();
   void Disconnect();
   int SendDataRequests(unsigned p);
   void SendDataRequests();
   void Have(unsigned p);
   void SendDataReply();
   void CancelBlock(unsigned p,unsigned b);

   void MarkPieceInvalid(unsigned p);
   unsigned invalid_piece_count;

   int peer_bytes_pool[2];
   int BytesAllowed(RateLimit::dir_t dir);
   bool BytesAllowed(RateLimit::dir_t dir,unsigned bytes);
   bool BytesAllowedToGet(unsigned b) { return BytesAllowed(RateLimit::GET,b); }
   bool BytesAllowedToPut(unsigned b) { return BytesAllowed(RateLimit::PUT,b); }
   void BytesUsed(int bytes,RateLimit::dir_t dir);
   void BytesGot(int b) { BytesUsed(b,RateLimit::GET); }
   void BytesPut(int b) { BytesUsed(b,RateLimit::PUT); }

public:
   int Do();
   TorrentPeer(Torrent *p,const sockaddr_u *a,int tracker_no=-1);
   ~TorrentPeer();
   void PrepareToDie();
   void Connect(int s,IOBuffer *rb);

   bool Failed() const { return error!=0; }
   const char *ErrorText() const { return error->Text(); }
   const char *GetName() const;
   const char *GetLogContext() { return GetName(); }

   bool ActivityTimedOut() const { return activity_timer.Stopped(); }
   bool NotConnected() const { return sock==-1; }
   bool Disconnected() const { return passive && NotConnected(); }
   bool Connected() const { return peer_id && send_buf && recv_buf; }
   bool Active() const { return Connected() && (am_interested || peer_interested); }
   bool Complete() const { return peer_complete_pieces==parent->total_pieces; }
   bool AddressEq(const TorrentPeer *o) const;
   bool IsPassive() const { return passive; }
   const sockaddr_u& GetAddress() const { return addr; }

   const char *Status();
};

class TorrentBlackList
{
   xmap<Timer*> bl;
   void check_expire();
public:
   bool Listed(const sockaddr_u &a);
   void Add(const sockaddr_u &a,const char *t="1h");
};

class TorrentDispatcher : public SMTask, protected ProtoLog
{
   int sock;
   const sockaddr_u addr;
   Ref<IOBuffer> recv_buf;
   Timer timeout_timer;
public:
   TorrentDispatcher(int s,const sockaddr_u *a);
   ~TorrentDispatcher();
   int Do();
};

#include "Job.h"

class TorrentJob : public Job
{
   SMTaskRef<Torrent> torrent;
   bool completed;
   bool done;
public:
   TorrentJob(Torrent *);
   ~TorrentJob();
   int Do();
   int Done() { return done; }
   xstring& FormatStatus(xstring&,int v,const char *tab);
   void ShowRunStatus(const SMTaskRef<StatusLine>& s);
   int AcceptSig(int);
   void PrepareToDie();
};

#endif//TORRENT_H
