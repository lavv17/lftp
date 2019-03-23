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

#ifndef TORRENT_H
#define TORRENT_H

#include "FileAccess.h"
#include "Bencode.h"
#include "Error.h"
#include "ProtoLog.h"
#include "network.h"
#include "RateLimit.h"
#include "Resolver.h"
#include "FileCopy.h"
#include "DHT.h"

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
   void set_range(int from,int to,bool value);
};

class TorrentBuild : public SMTask, public ProtoLog
{
   xstring_c top_path;
   xstring name;
   FileSet files;
   StringSet dirs_to_scan;
   bool done;
   Ref<Error> error;

   Ref<DirectedBuffer> translate;
   const char *lc_to_utf8(const char *s);

   friend class Torrent;
   Ref<BeNode> info;
   xstring pieces;
   off_t total_length;
   unsigned piece_length;

   const char *CurrPath() const { return dirs_to_scan[0]; }
   void NextDir() { dirs_to_scan.Remove(0); }
   void QueueDir(const char *dir) { dirs_to_scan.Append(dir); }
   void AddFile(const char *path,struct stat *st);
   void Finish();

public:
   TorrentBuild(const char *top);
   int Do();
   bool Done() const { return done || error; }
   bool Failed() const { return error; }
   const char *ErrorText() const { return error->Text(); }
   BeNode *GetFilesNode() const { return info->lookup("files"); }
   void SetPiece(unsigned p,const xstring& sha1);
   const xstring& GetMetadata();
   const char *GetBaseDirectory() const { return dirname(top_path); }
   const xstring& Status() const;
};

class TorrentPiece
{
   unsigned sources_count;	    // how many peers have the piece
   unsigned downloader_count;	    // how many downloaders of the piece are there
   float ratio;
   RefToArray<const TorrentPeer*> downloader; // which peers download the blocks
   Ref<BitField> block_map;	    // which blocks are present.

public:
   TorrentPiece() : sources_count(0), downloader_count(0), ratio(0) {}
   ~TorrentPiece() {}

   unsigned get_sources_count() const { return sources_count; }
   void add_sources_count(int diff) { sources_count+=diff; }
   bool has_no_sources() const { return sources_count==0; }

   bool has_a_downloader() const { return downloader_count>0; }
   void set_downloader(unsigned block,const TorrentPeer *o,const TorrentPeer *n,unsigned blk_count) {
      if(!downloader) {
	 if(o || !n)
	    return;
	 downloader=new const TorrentPeer*[blk_count];
	 for(unsigned i=0; i<blk_count; i++)
	    downloader[i]=0;
      }
      const TorrentPeer*& d=downloader[block];
      if(d==o) {
	 d=n;
	 downloader_count+=(n!=0)-(o!=0);
      }
   }
   void cleanup() {
      if(downloader_count==0 && downloader)
	 downloader=0;
   }
   const TorrentPeer *downloader_for(unsigned block) {
      return downloader ? downloader[block] : 0;
   }

   void set_block_present(unsigned block,unsigned blk_count) {
      if(!block_map)
	 block_map=new BitField(blk_count);
      block_map->set_bit(block,1);
   }
   void set_blocks_absent() {
      block_map=0;
   }
   void free_block_map() {
      block_map=0;
   }
   bool block_present(unsigned block) const {
      return block_map && block_map->get_bit(block);
   }
   bool all_blocks_present(unsigned blk_count) const {
      return block_map && block_map->has_all_set(0,blk_count);
   }
   bool any_blocks_present() const {
      return block_map; // it's allocated when setting any bit
   }

   float get_ratio() const { return ratio; }
   void add_ratio(float add) { ratio+=add; }
};

struct TorrentFile
{
   char *path;
   off_t pos;
   off_t length;
   void set(const char *n,off_t p,off_t l) {
      path=xstrdup(n);
      pos=p;
      length=l;
   }
   void unset() {
      xfree(path); path=0;
   }
   bool contains_pos(off_t p) const {
      return p>=pos && p<pos+length;
   }
};

class TorrentFiles : public xarray<TorrentFile>
{
   static int pos_cmp(const TorrentFile *a, const TorrentFile *b) {
      if(a->pos < b->pos)
	 return -1;
      if(a->pos > b->pos)
	 return 1;
      // we want zero-sized files to placed before non-zero ones.
      if(a->length != b->length)
	 return a->length < b->length ? -1 : 1;
      return 0;
   }
public:
   TorrentFile *file(int i) { return get_non_const()+i; }
   TorrentFiles(const BeNode *f_node,const Torrent *t);
   ~TorrentFiles() {
      for(int i=0; i<length(); i++)
	 file(i)->unset();
   }
   TorrentFile *FindByPosition(off_t p);
};

class TorrentListener : public SMTask, protected ProtoLog, protected Networker
{
   Ref<Error> error;
   int af;
   int type;
   int sock;
   sockaddr_u addr;
   Speedometer rate;
   void FillAddress(int port);
   Time last_sent_udp;
   int  last_sent_udp_count;
public:
   TorrentListener(int a,int type=SOCK_STREAM);
   ~TorrentListener();
   int Do();
   int GetPort() const { return addr.port(); }
   const char *GetAddress() const { return addr.address(); }
   const char *GetLogContext() { return type==SOCK_DGRAM?(af==AF_INET?"torrent(udp)":"torrent(udp6)"):"torrent"; }

   int SendUDP(const sockaddr_u& a,const xstring& buf);
   bool MaySendUDP();
};

class TorrentTracker;

class Torrent : public SMTask, protected ProtoLog, public ResClient, protected Networker
{
   friend class TorrentPeer;
   friend class TorrentDispatcher;
   friend class TorrentListener;
   friend class TorrentFiles;
   friend class DHT;

   bool shutting_down;
   bool complete;
   bool end_game;
   bool is_private;
   bool validating;
   bool force_valid;
   bool build_md;
   bool stop_if_complete;
   bool stop_if_known;
   bool md_saved;
   unsigned validate_index;
   Ref<Error> invalid_cause;

   static const unsigned PEER_ID_LEN = 20;
   static xstring my_peer_id;
   static xstring my_key;
   static unsigned my_key_num;
   static xmap<Torrent*> torrents;
   static SMTaskRef<TorrentListener> listener;
   static SMTaskRef<TorrentListener> listener_udp;
   static SMTaskRef<DHT> dht;
#if INET6
   static SMTaskRef<TorrentListener> listener_ipv6;
   static SMTaskRef<TorrentListener> listener_ipv6_udp;
   static SMTaskRef<DHT> dht_ipv6;
#endif
   static SMTaskRef<FDCache> fd_cache;
   static Ref<TorrentBlackList> black_list;

   static const SMTaskRef<DHT>& GetDHT(int af)
   {
#if INET6
      if(af==AF_INET6 && dht_ipv6)
	 return dht_ipv6;
#endif
      return dht;
   }
   static const SMTaskRef<DHT>& GetDHT(const sockaddr_u& a) { return GetDHT(a.family()); }
   static const SMTaskRef<TorrentListener>& GetUDPSocket(int af)
   {
#if INET6
      if(af==AF_INET6)
	 return listener_ipv6_udp;
#endif
      return listener_udp;
   }
   static const SMTaskRef<TorrentListener>& GetUDPSocket(const sockaddr_u& a) { return GetUDPSocket(a.family()); }

   static Torrent *FindTorrent(const xstring& info_hash) { return torrents.lookup(info_hash); }
   static void AddTorrent(Torrent *t);
   static void RemoveTorrent(Torrent *t);
   static int GetTorrentsCount() { return torrents.count(); }
   static void Dispatch(const xstring& info_hash,int s,const sockaddr_u *remote_addr,IOBuffer *recv_buf);
   static void DispatchUDP(const char *buf,int len,const sockaddr_u& src);

   xstring md_download;
   size_t metadata_size;
   void FetchMetadataFromURL(const char *url);
   void StartMetadataDownload();
   void MetadataDownloaded();
   bool SetMetadata(const xstring& md);
   void ParseMagnet(const char *p);
   const char *GetMetadataPath() const;
   bool SaveMetadata() const;
   bool LoadMetadata(const char *path);

   void Startup();

   void SetTotalLength(off_t);
   void StartValidating();

   xstring_c metainfo_url;
   SMTaskRef<FileCopy> metainfo_copy;
   SMTaskRef<TorrentBuild> building;
   Ref<BeNode> metainfo_tree;
   BeNode *info;
   xstring metadata;
   xstring info_hash;
   const xstring *pieces;
   xstring name;
   Ref<TorrentFiles> files;

   Ref<DirectedBuffer> recv_translate;
   Ref<DirectedBuffer> recv_translate_utf8;
   void InitTranslation();
   void TranslateString(BeNode *node) const;
   void TranslateStringFromUTF8(BeNode *node) const;

   TaskRefArray<TorrentTracker> trackers;
   bool TrackersDone() const;
   void StartTrackers();
   void ShutdownTrackers() const;
   void SendTrackersRequest(const char *e) const;
   static void StartListener();
   static void StartListenerUDP();
   static void StopListener();
   static void StopListenerUDP();
   static void StartDHT();
   static void StopDHT();
   void AnnounceDHT();
   void DenounceDHT();

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

   void AccountSend(unsigned p,unsigned len);
   void AccountRecv(unsigned p,unsigned len);

   void SetError(Error *);
   void SetError(const char *);

   BeNode *Lookup(xmap_p<BeNode>& d,const char *name,BeNode::be_type_t type);
   BeNode *Lookup(BeNode *d,const char *name,BeNode::be_type_t type) { return Lookup(d->dict,name,type); }
   BeNode *Lookup(Ref<BeNode>& d,const char *name,BeNode::be_type_t type) { return Lookup(d->dict,name,type); }

   TaskRefArray<TorrentPeer> peers;
   static int PeersCompareActivity(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);
   static int PeersCompareRecvRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);
   static int PeersCompareSendRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2);

   RefToArray<TorrentPiece> piece_info;
   unsigned blocks_in_piece;
   unsigned blocks_in_last_piece;
   bool BlockPresent(unsigned piece,unsigned block) const {
      return piece_info[piece].block_present(block);
   }
   bool AllBlocksPresent(unsigned piece) const {
      return piece_info[piece].all_blocks_present(BlocksInPiece(piece));
   }
   bool AnyBlocksPresent(unsigned piece) const {
      return piece_info[piece].any_blocks_present();
   }
   bool AllBlocksAbsent(unsigned piece) const {
      return !AnyBlocksPresent(piece);
   }
   void SetBlocksAbsent(unsigned piece) {
      piece_info[piece].set_blocks_absent();
   }
   void SetBlockPresent(unsigned piece,unsigned block) {
      piece_info[piece].set_block_present(block,BlocksInPiece(piece));
   }

   void RebuildPiecesNeeded();
   Timer pieces_timer; // for periodic pieces scanning
   xarray<unsigned> pieces_needed;
   static int PiecesNeededCmp(const unsigned *a,const unsigned *b);
   unsigned last_piece;

   unsigned min_piece_sources;
   unsigned avg_piece_sources;
   unsigned pieces_available_pct;
   float current_min_ppr;
   float current_max_ppr;

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
   float stop_min_ppr;
   Timer seed_timer;
   Timer timeout_timer;

   Timer decline_timer;
   Timer optimistic_unchoke_timer;
   Timer peers_scan_timer;
   Timer am_interested_timer;
   Timer shutting_down_timer;

   Timer dht_announce_timer;
   int dht_announce_count;
   int dht_announce_count_ipv6;

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
   static bool BlackListed(const TorrentPeer *peer);
   TorrentPeer *FindPeerById(const xstring& p_id);

public:
   static void ClassInit();

   Torrent(const char *mf,const char *cwd,const char *output_dir);
   ~Torrent();

   int Do();
   int Done() const;

   const xstring& Status();

   const Error *GetInvalidCause() const { return invalid_cause; }

   void Shutdown();
   bool ShuttingDown() const { return shutting_down; }
   void PrepareToDie();

   bool CanAccept() const;
   void Accept(int s,const sockaddr_u *a,IOBuffer *rb);
   static bool NoTorrentCanAccept();

   static void SHA1(const xstring& str,xstring& buf);
   void ValidatePiece(unsigned p);
   unsigned PieceLength(unsigned p) const { return p==total_pieces-1 ? last_piece_length : piece_length; }
   unsigned BlocksInPiece(unsigned p) const { return p==total_pieces-1 ? blocks_in_last_piece : blocks_in_piece; }

   const TaskRefArray<TorrentPeer>& GetPeers() const { return peers; }
   void AddPeer(TorrentPeer *);
   void CleanPeers();

   const xstring& GetInfoHash() const { return info_hash; }
   int GetPeersCount() const { return peers.count(); }
   int GetConnectedPeersCount() const { return connected_peers_count; }
   int GetActivePeersCount() const { return active_peers_count; }
   int GetCompletePeersCount() const { return complete_peers_count; }

   bool Complete() const { return complete; }
   bool Private() const { return is_private; }
   double GetRatio() const;
   void CalcPiecesStats();
   void CalcPerPieceRatio();
   float GetMinPerPieceRatio() const { return current_min_ppr; }
   float GetMaxPerPieceRatio() const { return current_max_ppr; }
   unsigned MinPieceSources() const { return min_piece_sources; }
   double AvgPieceSources() const { return avg_piece_sources/256.; }
   unsigned PiecesAvailablePct() const { return pieces_available_pct; }
   unsigned long long TotalLength() const { return total_length; }
   unsigned PieceLength() const { return piece_length; }
   const char *GetName() const { return name?name.get():metainfo_url.get(); }
   bool IsDownloading() const { return HasMetadata() && !IsValidating() && !Complete() && !ShuttingDown(); }

   void Reconfig(const char *name);
   const char *GetLogContext() { return GetName(); }

   void ForceValid() { force_valid=true; }
   bool IsValidating() const { return validating; }
   void Share() { build_md=true; }
   bool IsSharing() const { return build_md; }

   void StopIfComplete() { stop_if_complete=true; }
   void StopIfKnown() { stop_if_known=stop_if_complete=true; }

   static int GetPort();
   static int GetPortIPv4() { return listener?listener->GetPort():0; }
#if INET6
   static int GetPortIPv6() { return listener_ipv6?listener_ipv6->GetPort():0; }
   static const char *GetAddressIPv6() { return listener_ipv6?listener_ipv6->GetAddress():"::"; }
#endif
   int GetWantedPeersCount() const;
   static const xstring& GetMyPeerId() { return my_peer_id; }
   static const xstring& GetMyKey() { return my_key; }
   static unsigned GetMyKeyNum() { return my_key_num; }

   unsigned long long GetTotalSent() { return total_sent; }
   unsigned long long GetTotalRecv() { return total_recv; }
   unsigned long long GetTotalLeft() { return total_left; }

   const TaskRefArray<TorrentTracker>& Trackers() { return trackers; }
   bool HasMetadata() const { return metadata!=0; }
   void RestartPeers();

   static void BootstrapDHT(const char *n) {
      StartDHT();
      if(dht)
	 dht->AddBootstrapNode(n);
   }

   static bool HasDHT() {
#if INET6
      if(dht_ipv6)
	 return true;
#endif
      if(dht)
	 return true;
      return false;
   }
   static bool HasDHT(int af) {
#if INET6
      if(af==AF_INET6 && dht_ipv6)
	 return true;
#endif
      if(af==AF_INET && dht)
	 return true;
      return false;
   }
   const char *DHT_Status() const;
   void DHT_Announced(int af);	 // called from DHT to count announces
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
   int udp_port;
   bool connected;
   bool passive;

   xstring_c last_dc;

   Timer timeout_timer;
   Timer retry_timer;
   Timer keepalive_timer;
   Timer choke_timer;
   Timer interest_timer;
   Timer activity_timer;

   SMTaskRef<IOBuffer> recv_buf;
   SMTaskRef<IOBuffer> send_buf;

   unsigned long long peer_recv;
   unsigned long long peer_sent;

   Speedometer peer_recv_rate;
   Speedometer peer_send_rate;

   xstring peer_id;
   unsigned char extensions[8];
   TorrentPeer *duplicate;
   bool myself;

   bool FastExtensionEnabled() const { return extensions[7]&0x04; }
   bool LTEPExtensionEnabled() const { return extensions[5]&0x10; }
   bool DHT_Enabled() const { return extensions[7]&0x01; }

   bool am_choking;
   bool am_interested;
   bool peer_choking;
   bool peer_interested;

   bool upload_only;

   Ref<BitField> peer_bitfield;
   unsigned peer_complete_pieces;

   xqueue<unsigned,xarray<unsigned> > fast_set;
   bool InFastSet(unsigned) const;
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
   enum msg_ext_id
   {
      MSG_EXT_HANDSHAKE=0,
      MSG_EXT_PEX=1,
      MSG_EXT_METADATA=2,
   };
   enum ut_metadata_msg_id
   {
      UT_METADATA_REQUEST=0,
      UT_METADATA_DATA=1,
      UT_METADATA_REJECT=2,
   };
public:
   enum { TR_ACCEPTED=-1, TR_DHT=-2, TR_PEX=-3 };  // special values for tracker_no
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
      virtual void Pack(SMTaskRef<IOBuffer>& b);
      virtual unpack_status_t Unpack(const Buffer *b);
      virtual ~Packet() {}
      int GetLength() const { return length; }
      packet_type GetPacketType() const { return type; }
      const char *GetPacketTypeText() const;
      void DropData(SMTaskRef<IOBuffer>& b) { b->Skip(4+length); }
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
      void Pack(SMTaskRef<IOBuffer>& b) { Packet::Pack(b); b->PackUINT32BE(piece); }
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
      void Pack(SMTaskRef<IOBuffer>& b);
   };
   class _PacketIBL : public Packet
   {
   public:
      unsigned index,begin,req_length;
      _PacketIBL(packet_type t,unsigned i,unsigned b,unsigned l);
      unpack_status_t Unpack(const Buffer *b);
      void ComputeLength();
      void Pack(SMTaskRef<IOBuffer>& b);
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
      void Pack(SMTaskRef<IOBuffer>& b) {
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
      void Pack(SMTaskRef<IOBuffer>& b) { Packet::Pack(b); b->PackUINT16BE(port); }
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
      xstring appendix;
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
	    if(unpacked<length+4) {
	       appendix.nset(b->Get()+unpacked,length+4-unpacked);
	       unpacked=length+4;
	    }
	    return res;
	 }
      void ComputeLength() { Packet::ComputeLength(); length++; if(data) length+=data->ComputeLength(); length+=appendix.length(); }
      void Pack(SMTaskRef<IOBuffer>& b) { Packet::Pack(b); b->PackUINT8(code); if(data) data->Pack(b); b->Put(appendix); }
      void SetAppendix(const char *s,int len) { appendix.nset(s,len); length+=len; }
   };

private:
   unpack_status_t UnpackPacket(SMTaskRef<IOBuffer>& ,Packet **);
   void HandlePacket(Packet *);
   void HandleExtendedMessage(PacketExtended *);

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
   void Disconnect(const char *dc=0);
   void Restart();
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

   int msg_ext_metadata;
   int msg_ext_pex;

   size_t metadata_size;
   void SendMetadataRequest();

   struct ut_pex_data
   {
      xmap<char> sent; // key is compact addr
      Timer send_timer;
      Timer recv_timer;
      enum flags { ENCRYPTION=1, SEED=2, UTP=4, HOLEPUNCH=8, CONNECTABLE=16 };
      ut_pex_data() : send_timer(60), recv_timer(59) {}
   } pex;
   void AddPEXPeers(BeNode *added,BeNode *added_f,int addr_size);
   void SendPEXPeers();

public:
   int Do();
   TorrentPeer(Torrent *p,const sockaddr_u *a,int tracker_no);
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
   bool Complete() const { return peer_complete_pieces==parent->total_pieces && parent->total_pieces>0; }
   bool Seed() const { return Complete() || upload_only; }
   bool AddressEq(const TorrentPeer *o) const;
   bool IsPassive() const { return passive; }
   const sockaddr_u& GetAddress() const { return addr; }

   const char *Status();
};

class TorrentBlackList : private ProtoLog
{
   xmap_p<Timer> bl;
   void check_expire();
public:
   bool Listed(const sockaddr_u &a);
   void Add(const sockaddr_u &a,const char *t="1h");
};

class TorrentDispatcher : public SMTask, protected ProtoLog
{
   int sock;
   const sockaddr_u addr;
   SMTaskRef<IOBuffer> recv_buf;
   Timer timeout_timer;
   xstring_c peer_name;
public:
   TorrentDispatcher(int s,const sockaddr_u *a);
   ~TorrentDispatcher();
   int Do();
   const char *GetLogContext() { return peer_name; }
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
