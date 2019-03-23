/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2019 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <config.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sha1.h>
#include <dirent.h>

#include "Torrent.h"
#include "TorrentTracker.h"
#include "DHT.h"
#include "log.h"
#include "url.h"
#include "misc.h"
#include "plural.h"
CDECL_BEGIN
#include "human.h"
CDECL_END

static ResType torrent_vars[] = {
   {"torrent:port-range", "6881-6889", ResMgr::RangeValidate, ResMgr::NoClosure},
   {"torrent:max-peers", "60", ResMgr::UNumberValidate},
   {"torrent:save-metadata", "yes", ResMgr::BoolValidate, ResMgr::NoClosure},
   {"torrent:stop-min-ppr", "1.4", ResMgr::FloatValidate},
   {"torrent:stop-on-ratio", "2.0", ResMgr::FloatValidate},
   {"torrent:seed-max-time", "30d", ResMgr::TimeIntervalValidate},
   {"torrent:seed-min-peers", "3", ResMgr::UNumberValidate},
   {"torrent:ip", "", ResMgr::IPv4AddrValidate, ResMgr::NoClosure},
   {"torrent:retracker", ""},
   {"torrent:use-dht", "yes", ResMgr::BoolValidate, ResMgr::NoClosure},
   {"torrent:timeout", "7d", ResMgr::TimeIntervalValidate, ResMgr::NoClosure},
#if INET6
   {"torrent:ipv6", "", ResMgr::IPv6AddrValidate, ResMgr::NoClosure},
#endif
   {0}
};
static ResDecls torrent_vars_register(torrent_vars);

void Torrent::ClassInit()
{
   static bool inited;
   if(inited)
      return;
   inited=true;

#if INET6
   const char *ipv6=ResMgr::Query("torrent:ipv6",0);
   if(!*ipv6)
   {
      ipv6=FindGlobalIPv6Address();
      if(ipv6) {
	 ProtoLog::LogNote(9,"found IPv6 address: %s",ipv6);
	 ResMgr::Set("torrent:ipv6",0,ipv6);
      }
   }
#endif
}

xstring Torrent::my_peer_id;
xstring Torrent::my_key;
unsigned Torrent::my_key_num;
xmap<Torrent*> Torrent::torrents;
SMTaskRef<TorrentListener> Torrent::listener;
SMTaskRef<TorrentListener> Torrent::listener_udp;
SMTaskRef<DHT> Torrent::dht;
#if INET6
SMTaskRef<TorrentListener> Torrent::listener_ipv6;
SMTaskRef<TorrentListener> Torrent::listener_ipv6_udp;
SMTaskRef<DHT> Torrent::dht_ipv6;
#endif
SMTaskRef<FDCache> Torrent::fd_cache;
Ref<TorrentBlackList> Torrent::black_list;

void Torrent::StartDHT()
{
   if(!ResMgr::QueryBool("torrent:use-dht",0)) {
      StopDHT();
      StopListenerUDP();
      return;
   }

   if(dht)
      return;

   StartListenerUDP();

   const char *home=get_lftp_cache_dir();
   const char *nodename=get_nodename();

   mkdir(xstring::format("%s/DHT",home),0700);

   const char *ip=ResMgr::Query("torrent:ip",0);
   if(!ip || !ip[0])
      ip="127.0.0.1";
   sockaddr_compact ip_packed;
   ip_packed.get_space(4);
   inet_pton(AF_INET,ip,ip_packed.get_non_const());
   ip_packed.set_length(4);
   xstring node_id;
   DHT::MakeNodeId(node_id,ip_packed);
   dht=new DHT(AF_INET,node_id);
   dht->state_file.setf("%s/DHT/ipv4-%s",home,nodename);
   if(listener_udp->GetPort())
      dht->Load();

#if INET6
   ip=ResMgr::Query("torrent:ipv6",0);
   if(!ip || !ip[0])
      ip="::1";
   ip_packed.get_space(16);
   inet_pton(AF_INET6,ip,ip_packed.get_non_const());
   ip_packed.set_length(16);
   DHT::MakeNodeId(node_id,ip_packed);
   dht_ipv6=new DHT(AF_INET6,node_id);
   dht_ipv6->state_file.setf("%s/DHT/ipv6-%s",home,nodename);
   if(listener_ipv6_udp->GetPort())
      dht_ipv6->Load();
#endif
}
void Torrent::StopDHT()
{
   if(!dht)
      return;
   dht->Save();
   dht=0;
#if INET6
   dht_ipv6->Save();
   dht_ipv6=0;
#endif
}

void Torrent::StartListener()
{
   if(listener)
      return;

   listener=new TorrentListener(AF_INET);
   listener->Roll(); // try to allocate ipv4 port first
#if INET6
   listener_ipv6=new TorrentListener(AF_INET6);
#endif
}
void Torrent::StopListener()
{
   listener=0;
#if INET6
   listener_ipv6=0;
#endif
}
void Torrent::StartListenerUDP()
{
   if(listener_udp)
      return;
   listener_udp=new TorrentListener(AF_INET,SOCK_DGRAM);
#if INET6
   listener_ipv6_udp=new TorrentListener(AF_INET6,SOCK_DGRAM);
#endif
}
void Torrent::StopListenerUDP()
{
   listener_udp=0;
#if INET6
   listener_ipv6_udp=0;
#endif
}

Torrent::Torrent(const char *mf,const char *c,const char *od)
   : metainfo_url(mf),
     pieces_timer(10),
     cwd(c), output_dir(od), rate_limit(mf),
     seed_timer("torrent:seed-max-time",0),
     timeout_timer("torrent:timeout",0),
     optimistic_unchoke_timer(30), peers_scan_timer(1),
     am_interested_timer(1), shutting_down_timer(60),
     dht_announce_timer(10*60),
     dht_announce_count(0), dht_announce_count_ipv6(0)
{
   shutting_down=false;
   complete=false;
   end_game=false;
   is_private=false;
   validating=false;
   force_valid=false;
   build_md=false;
   stop_if_complete=false;
   stop_if_known=false;
   md_saved=false;
   validate_index=0;
   metadata_size=0;
   info=0;
   pieces=0;
   piece_length=0;
   total_pieces=0;
   last_piece_length=0;
   total_length=0;
   total_sent=0;
   total_recv=0;
   total_left=0;
   complete_pieces=0;
   connected_peers_count=0;
   active_peers_count=0;
   complete_peers_count=0;
   am_interested_peers_count=0;
   am_not_choking_peers_count=0;
   max_peers=60;
   seed_min_peers=3;
   stop_on_ratio=2;
   stop_min_ppr=1,
   last_piece=TorrentPeer::NO_PIECE;
   min_piece_sources=0;
   avg_piece_sources=0;
   pieces_available_pct=0;
   current_min_ppr=0;
   current_max_ppr=0;
   Reconfig(0);

   if(!my_peer_id) {
      my_peer_id.set("-lftp47-");
      my_peer_id.appendf("%04x",(unsigned)getpid() & 0xffff);
      my_peer_id.appendf("%08x",(unsigned)now.UnixTime());
      assert(my_peer_id.length()==PEER_ID_LEN);
   }
   if(!my_key) {
      for(int i=0; i<10; i++)
	 my_key.appendf("%02x",unsigned(random()/13%256));
      my_key_num=random();
   }
   dht_announce_timer.Stop();
}

Torrent::~Torrent()
{
}

bool Torrent::TrackersDone() const
{
   if(shutting_down && shutting_down_timer.Stopped())
      return true;
   for(int i=0; i<trackers.count(); i++) {
      if(trackers[i]->IsActive())
	 return false;
   }
   return true;
}
int Torrent::Done() const
{
   return (shutting_down && TrackersDone());
}

void Torrent::ShutdownTrackers() const
{
   for(int i=0; i<trackers.count(); i++) {
      trackers[i]->Shutdown();
   }
}
void Torrent::Shutdown()
{
   if(shutting_down)
      return;
   Enter(this);
   LogNote(3,"Shutting down...");
   shutting_down=true;
   shutting_down_timer.Reset();
   ShutdownTrackers();
   DenounceDHT();
   PrepareToDie();
   Leave();
}

void Torrent::AddTorrent(Torrent *t)
{
   if(FindTorrent(t->GetInfoHash()))
      return;
   if(GetTorrentsCount()==0) {
      StartListener();
      StartDHT();
   }
   torrents.add(t->GetInfoHash(),t);
}
void Torrent::RemoveTorrent(Torrent *t)
{
   if(t!=FindTorrent(t->info_hash))
      return;
   torrents.remove(t->GetInfoHash());
   if(GetTorrentsCount()==0) {
      StopListener();
      StopDHT();
      StopListenerUDP();
      fd_cache=0;
      black_list=0;
   }
}

void Torrent::PrepareToDie()
{
   metainfo_copy=0;
   building=0;
   peers.unset();
   if(info_hash && this==FindTorrent(info_hash))
      RemoveTorrent(this);
}

void Torrent::SetError(Error *e)
{
   if(invalid_cause)
      return;
   invalid_cause=e;
   LogError(0,"%s: %s",
      invalid_cause->IsFatal()?"Fatal error":"Transient error",
      invalid_cause->Text());
   Shutdown();
}
void Torrent::SetError(const char *msg)
{
   SetError(Error::Fatal(msg));
}

double Torrent::GetRatio() const
{
   if(total_sent==0 || total_length==total_left)
      return 0;
   return total_sent/double(total_length-total_left);
}

void Torrent::SetDownloader(unsigned piece,unsigned block,const TorrentPeer *o,const TorrentPeer *n)
{
   piece_info[piece].set_downloader(block,o,n,BlocksInPiece(piece));
}

void Torrent::AccountSend(unsigned p,unsigned len)
{
   total_sent+=len;
   send_rate.Add(len);
   piece_info[p].add_ratio(float(len)/PieceLength(p));
}
void Torrent::AccountRecv(unsigned p,unsigned len)
{
   total_recv+=len;
   recv_rate.Add(len);
}

BeNode *Torrent::Lookup(xmap_p<BeNode>& dict,const char *name,BeNode::be_type_t type)
{
   BeNode *node=dict.lookup(name);
   if(!node) {
      SetError(xstring::format("Meta-data: `%s' key missing",name));
      return 0;
   }
   if(node->type!=type) {
      SetError(xstring::format("Meta-data: wrong `%s' type, must be %s",name,BeNode::TypeName(type)));
      return 0;
   }
   return node;
}
void Torrent::InitTranslation()
{
   const char *charset="UTF-8"; // default
   recv_translate_utf8=new DirectedBuffer(DirectedBuffer::GET);
   recv_translate_utf8->SetTranslation(charset,true);
   if(metainfo_tree) {
      BeNode *b_charset=metainfo_tree->lookup("encoding",BeNode::BE_STR);
      if(b_charset)
	 charset=b_charset->str;
   }
   recv_translate=new DirectedBuffer(DirectedBuffer::GET);
   recv_translate->SetTranslation(charset,true);
}
void Torrent::TranslateString(BeNode *node) const
{
   if(node->str_lc)
      return;
   recv_translate->ResetTranslation();
   recv_translate->PutTranslated(node->str);
   node->str_lc.nset(recv_translate->Get(),recv_translate->Size());
   recv_translate->Skip(recv_translate->Size());
}
void Torrent::TranslateStringFromUTF8(BeNode *node) const
{
   if(node->str_lc)
      return;
   const Ref<DirectedBuffer>& tr=recv_translate_utf8;
   tr->ResetTranslation();
   tr->PutTranslated(node->str);
   node->str_lc.nset(tr->Get(),tr->Size());
   tr->Skip(tr->Size());
}

void Torrent::SHA1(const xstring& str,xstring& buf)
{
   buf.get_space(SHA1_DIGEST_SIZE);
   sha1_buffer(str.get(),str.length(),buf.get_non_const());
   buf.set_length(SHA1_DIGEST_SIZE);
}

void Torrent::ValidatePiece(unsigned p)
{
   const xstring& buf=Torrent::RetrieveBlock(p,0,PieceLength(p));
   bool valid=false;
   if(buf.length()==PieceLength(p)) {
      xstring& sha1=xstring::get_tmp();
      SHA1(buf,sha1);
      if(building) {
	 building->SetPiece(p,sha1);
	 valid=true;
      } else {
	 valid=!memcmp(pieces->get()+p*SHA1_DIGEST_SIZE,sha1,SHA1_DIGEST_SIZE);
      }
   }
   if(!valid) {
      if(building) {
	 SetError("File validation error");
	 return;
      }
      if(buf.length()==PieceLength(p))
	 LogError(11,"piece %u digest mismatch",p);
      if(my_bitfield->get_bit(p)) {
	 total_left+=PieceLength(p);
	 complete_pieces--;
	 my_bitfield->set_bit(p,0);
      }
      SetBlocksAbsent(p);
   } else {
      LogNote(11,"piece %u ok",p);
      if(!my_bitfield->get_bit(p)) {
	 total_left-=PieceLength(p);
	 complete_pieces++;
	 my_bitfield->set_bit(p,1);
	 piece_info[p].free_block_map();
      }
   }
}

template<typename T>
static inline int cmp(T a,T b)
{
   if(a>b)
      return 1;
   if(a<b)
      return -1;
   return 0;
}

static Torrent *cmp_torrent;
int Torrent::PiecesNeededCmp(const unsigned *a,const unsigned *b)
{
   int ra=cmp_torrent->piece_info[*a].get_sources_count();
   int rb=cmp_torrent->piece_info[*b].get_sources_count();
   int c=cmp(ra,rb);
   if(c) return c;
   return cmp(*a,*b);
}
int Torrent::PeersCompareActivity(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2)
{
   TimeDiff i1((*p1)->activity_timer.TimePassed());
   TimeDiff i2((*p2)->activity_timer.TimePassed());
   return cmp(i1.Seconds(),i2.Seconds());
}
int Torrent::PeersCompareRecvRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2)
{
   float r1((*p1)->peer_recv_rate.Get());
   float r2((*p2)->peer_recv_rate.Get());
   int c=cmp(r1,r2);
   if(c) return c;
   return PeersCompareSendRate(p1,p2);
}
int Torrent::PeersCompareSendRate(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2)
{
   float r1((*p1)->peer_send_rate.Get());
   float r2((*p2)->peer_send_rate.Get());
   return cmp(r1,r2);
}

bool Torrent::SeededEnough() const
{
   return (stop_on_ratio>0 && GetRatio()>=stop_on_ratio
	   && GetMinPerPieceRatio()>=stop_min_ppr)
      || seed_timer.Stopped();
}

void Torrent::CleanPeers()
{
   Enter();
   // remove uninteresting peers and request more
   for(int i=0; i<peers.count(); i++) {
      const TorrentPeer *peer=peers[i];
      if(peer->ActivityTimedOut()) {
	 LogNote(4,"removing uninteresting peer %s (%s)",peer->GetName(),peers[i]->Status());
	 BlackListPeer(peer,"2h");
	 peers.remove(i--);
      }
   }
   Leave();
}

void Torrent::StartTrackers()
{
   for(int i=0; i<trackers.count(); i++) {
      trackers[i]->Start();
   }
}

int Torrent::GetPort()
{
   int port=0;
   if(listener && !port)
      port=listener->GetPort();
#if INET6
   if(listener_ipv6 && !port)
      port=listener_ipv6->GetPort();
#endif
   if(listener_udp && !port)
      return listener_udp->GetPort();
#if INET6
   if(listener_ipv6_udp && !port)
      port=listener_ipv6_udp->GetPort();
#endif
   return port;
}

void Torrent::AnnounceDHT()
{
   if(is_private)
      return;
   CleanPeers();
   if(dht)
      dht->AnnouncePeer(this);
#if INET6
   if(dht_ipv6)
      dht_ipv6->AnnouncePeer(this);
#endif
   dht_announce_timer.Reset();
}
void Torrent::DenounceDHT()
{
   if(is_private)
      return;
   if(dht)
      dht->DenouncePeer(this);
#if INET6
   if(dht_ipv6)
      dht_ipv6->DenouncePeer(this);
#endif
}
void Torrent::DHT_Announced(int af)
{
   if(af==AF_INET)
      dht_announce_count++;
#if INET6
   else if(af==AF_INET6)
      dht_announce_count_ipv6++;
#endif
}
const char *Torrent::DHT_Status() const
{
   if(!HasDHT() || Private())
      return "";
   static xstring status;
   status.nset("",0);
   if(dht_announce_count || dht_announce_count_ipv6) {
      status.append(_("announced via "));
      if(dht_announce_count)
	 status.appendf("ipv4:%d",dht_announce_count);
#if INET6
      if(dht_announce_count_ipv6) {
	 if(dht_announce_count)
	    status.append(", ");
	 status.appendf("ipv6:%d",dht_announce_count_ipv6);
      }
#endif
   }
   if(!dht_announce_timer.Stopped() && !validating) {
      if(status.length()>0)
	 status.append("; ");
      status.appendf(_("next announce in %s"),
	 dht_announce_timer.TimeLeft().toString(TimeInterval::TO_STR_TRANSLATE));
   }
   return status;
}

const char *Torrent::GetMetadataPath() const
{
   if(!QueryBool("torrent:save-metadata",0))
      return NULL;
   xstring& path=xstring::cat(get_lftp_data_dir(),"/torrent",NULL);
   mkdir(path,0700);
   path.append("/md");
   mkdir(path,0700);
   path.append('/');
   info_hash.hexdump_to(path);
   return path;
}

bool Torrent::SaveMetadata() const
{
   if(md_saved)
      return true;  // saved already.

   const char *path=GetMetadataPath();
   if(!path)
      return false;

   int fd=open(path,O_CREAT|O_WRONLY,0600);
   if(fd<0) {
      LogError(9,"open(%s): %s",path,strerror(errno));
      return false;
   }

   int bytes_to_write=metadata.length();
   int res=write(fd,metadata.get(),bytes_to_write);
   int saved_errno=errno;
   ftruncate(fd,bytes_to_write);

   close(fd);

   if(res==bytes_to_write)
      return true;  // no error, fd is closed.

   if(res<0)
      LogError(9,"write(%s): %s",path,strerror(saved_errno));
   else
      LogError(9,"write(%s): short write (only wrote %d bytes)",path,res);

   return false;
}
bool Torrent::LoadMetadata(const char *path)
{
   int fd=open(path,O_RDONLY);
   if(fd<0) {
      LogError(9,"open(%s): %s",path,strerror(errno));
      return false;
   }

   struct stat st;
   if(fstat(fd,&st)==-1) {
      close(fd);
      return false;
   }
   int bytes_to_read=st.st_size;

   xstring md;
   int res=read(fd,md.add_space(bytes_to_read),bytes_to_read);
   int saved_errno=errno;

   close(fd);

   if(res==bytes_to_read) {
      // no error, fd is closed.
      md.add_commit(res);

      xstring new_info_hash;
      SHA1(md,new_info_hash);
      if(info_hash && info_hash.ne(new_info_hash)) {
	 LogError(9,"cached metadata does not match info_hash");
	 return false;
      }
      LogNote(9,"got metadata from %s",path);
      if(SetMetadata(md)) {
	 md_saved=true;
	 return true;
      }
      return false;
   }

   if(res<0)
      LogError(9,"read(%s): %s",path,strerror(saved_errno));
   else
      LogError(9,"read(%s): short read (only read %d bytes)",path,res);
   return false;
}

void Torrent::FetchMetadataFromURL(const char *url)
{
   ParsedURL u(url,true);
   if(!u.proto) {
      u.proto.set("file");
      u.path.set(url);  // undo %xx translation
   }
   LogNote(9,"Retrieving meta-data from `%s'...\n",url);
   FileCopyPeer *metainfo_src=new FileCopyPeerFA(&u,FA::RETRIEVE);
   FileCopyPeer *metainfo_dst=new FileCopyPeerMemory(10000000);
   metainfo_copy=new FileCopy(metainfo_src,metainfo_dst,false);
}

void Torrent::StartMetadataDownload()
{
   const char *path=GetMetadataPath();
   if(path && access(path,R_OK)>=0) {
      // we have the metadata cached
      if(LoadMetadata(path)) {
	 if(stop_if_known) {
	    LogNote(2,"found cached metadata, stopping");
	    Shutdown();
	 } else {
	    Startup();
	 }
	 return;
      }
   }
   md_download.nset("",0);
   // add torrent without metadata to announce it and get peers to get MD from.
   AddTorrent(this);
}

void Torrent::SetTotalLength(off_t tl)
{
   total_length=tl;

   LogNote(4,"Total length is %llu",total_length);
   total_left=total_length;

   last_piece_length=total_length%piece_length;
   if(last_piece_length==0)
      last_piece_length=piece_length;

   total_pieces=(total_length+piece_length-1)/piece_length;

   my_bitfield=new BitField(total_pieces);

   blocks_in_piece=(piece_length+BLOCK_SIZE-1)/BLOCK_SIZE;
   blocks_in_last_piece=(last_piece_length+BLOCK_SIZE-1)/BLOCK_SIZE;

   piece_info=new TorrentPiece[total_pieces]();
}

void Torrent::StartValidating()
{
   validate_index=0;
   validating=true;
   recv_rate.Reset();
}

bool Torrent::SetMetadata(const xstring& md)
{
   metadata.set(md);
   timeout_timer.Reset();

   xstring new_info_hash;
   SHA1(metadata,new_info_hash);
   if(info_hash && info_hash.ne(new_info_hash)) {
      metadata.unset();
      SetError("metadata does not match info_hash");
      return false;
   }
   info_hash.set(new_info_hash);

   if(!info) {
      int rest;
      info=BeNode::Parse(metadata,metadata.length(),&rest);
      if(!info) {
	 SetError("cannot parse metadata");
	 return false;
      }
      xmap_p<BeNode> d;
      d.add("info",info);
      metainfo_tree=new BeNode(&d);
      InitTranslation();
   }

   BeNode *b_piece_length=Lookup(info,"piece length",BeNode::BE_INT);
   if(!b_piece_length || b_piece_length->num<1024 || b_piece_length->num>INT_MAX/4) {
      SetError("Meta-data: invalid piece length");
      return false;
   }
   piece_length=b_piece_length->num;
   LogNote(4,"Piece length is %u",piece_length);

   BeNode *b_name=info->lookup("name",BeNode::BE_STR);
   BeNode *b_name_utf8=info->lookup("name.utf-8",BeNode::BE_STR);
   if(b_name_utf8) {
      TranslateStringFromUTF8(b_name_utf8);
      name.set(b_name_utf8->str_lc);
   } else if(b_name) {
      TranslateString(b_name);
      name.set(b_name->str_lc);
   } else {
      name.truncate();
      info_hash.hexdump_to(name);
   }
   Reconfig(0);

   BeNode *files=info->lookup("files");
   if(!files) {
      BeNode *length=Lookup(info,"length",BeNode::BE_INT);
      if(!length || length->num<0) {
	 SetError("Meta-data: invalid or missing length");
	 return false;
      }
      total_length=length->num;
   } else {
      if(files->type!=BeNode::BE_LIST) {
	 SetError("Meta-data: wrong `info/files' type, must be LIST");
	 return false;
      }
      total_length=0;
      for(int i=0; i<files->list.length(); i++) {
	 if(files->list[i]->type!=BeNode::BE_DICT) {
	    SetError(xstring::format("Meta-data: wrong `info/files[%d]' type, must be LIST",i));
	    return false;
	 }
	 BeNode *f=Lookup(files->list[i]->dict,"length",BeNode::BE_INT);
	 if(!f || f->num<0) {
	    SetError("Meta-data: invalid or missing file length");
	    return false;
	 }
	 if(!Lookup(files->list[i]->dict,"path",BeNode::BE_LIST)) {
	    SetError("Meta-data: file path missing");
	    return false;
	 }
	 total_length+=f->num;
      }
   }
   this->files=new TorrentFiles(files,this);
   SetTotalLength(total_length);

   BeNode *b_pieces=Lookup(info,"pieces",BeNode::BE_STR);
   if(!b_pieces) {
      SetError("Meta-data: `pieces' missing");
      return false;
   }
   pieces=&b_pieces->str;
   if(pieces->length()!=SHA1_DIGEST_SIZE*total_pieces) {
      SetError("Meta-data: invalid `pieces' length");
      return false;
   }

   is_private=info->lookup_int("private");

   return true;
}

void Torrent::Startup()
{
   if(!info_hash || !metadata)
      SetError("missing metadata");
   if(shutting_down)
      return;
   Torrent *other=FindTorrent(info_hash);
   if(other) {
      if(other!=this) {
	 SetError("This torrent is already running");
	 return;
      }
   } else {
      AddTorrent(this);
   }

   if(!building)
      md_saved=SaveMetadata();

   if(!force_valid && !building) {
      StartValidating();
   } else {
      my_bitfield->set_range(0,total_pieces,1);
      complete_pieces=total_pieces;
      total_left=0;
      complete=true;
      seed_timer.Reset();
      dht_announce_timer.Stop();
   }
   RestartPeers();
}

void Torrent::RestartPeers()
{
   for(int i=0; i<peers.count(); i++)
      peers[i]->Restart();
}

static int base32_char_to_value(char c)
{
   if(c>='a' && c<='z')
      return c-'a';
   if(c>='A' && c<='Z')
      return c-'A';
   if(c>='2' && c<='7')
      return c-'2'+26;
   if(c=='=')
      return 0;
   return -1;
}
void base32_decode(const char *base32,xstring& out)
{
   unsigned data=0;
   int data_bits=0;
   int pad_bits=0;
   while(*base32) {
      char c=*base32++;
      if(c=='=' && data_bits<=pad_bits)
	 return;
      if(pad_bits>0 && c!='=')
	 return;
      int v=base32_char_to_value(c);
      if(v==-1)
	 return;
      data|=((v&31)<<(11-data_bits));
      data_bits+=5;
      if(c=='=')
	 pad_bits+=5;
      if(data_bits>=8) {
	 out.append(char((data>>8)&255));
	 data<<=8;
	 data_bits-=8;
      }
   }
   if(data_bits>0)
      out.append(char((data>>8)&255));
}

void Torrent::ParseMagnet(const char *m0)
{
   char *m=alloca_strdup(m0);
   for(char *p=strtok(m,"&"); p; p=strtok(NULL,"&")) {
      char *v=strchr(p,'=');
      if(!v)
	 continue;
      *v++=0;
      v=xstring::get_tmp(v).url_decode(URL_DECODE_PLUS).get_non_const();
      if(!strcmp(p,"xt")) {
	 if(strncmp(v,"urn:btih:",9)) {
	    SetError("Only BitTorrent magnet links are supported");
	    return;
	 }
	 v+=9;
	 xstring& btih=xstring::get_tmp(v);
	 if(btih.length()==40) {
	    btih.hex_decode();
	    if(btih.length()!=20) {
	       SetError("Invalid value of urn:btih in magnet link");
	       return;
	    }
	    info_hash.move_here(btih);
	 } else {
	    info_hash.truncate();
	    base32_decode(v,info_hash);
	    if(info_hash.length()!=20) {
	       info_hash.unset();
	       SetError("Invalid value of urn:btih in magnet link");
	       return;
	    }
	 }
      }
      else if(!strcmp(p,"tr")) {
	 SMTaskRef<TorrentTracker> new_tracker(new TorrentTracker(this,v));
	 if(!new_tracker->Failed()) {
	    new_tracker->tracker_no=trackers.count();
	    trackers.append(new_tracker.borrow());
	 }
      }
      else if(!strcmp(p,"dn")) {
	 name.set(v);
      }
   }
   if(!info_hash) {
      SetError("missing urn:btih in magnet link");
      return;
   }
   if(FindTorrent(info_hash)) {
      SetError("This torrent is already running");
      return;
   }
   StartMetadataDownload();
}

void Torrent::CalcPiecesStats()
{
   min_piece_sources=INT_MAX;
   avg_piece_sources=0;
   pieces_available_pct=0;
   for(unsigned i=0; i<total_pieces; i++) {
      if(my_bitfield->get_bit(i))
	 continue;
      unsigned sc=piece_info[i].get_sources_count();
      if(min_piece_sources>sc)
	 min_piece_sources=sc;
      if(sc==0)
	 continue;
      pieces_available_pct++;
      avg_piece_sources+=sc;
   }
   avg_piece_sources=avg_piece_sources*256/(total_pieces-complete_pieces);
   pieces_available_pct=pieces_available_pct*100/(total_pieces-complete_pieces);
   CalcPerPieceRatio();
}

void Torrent::CalcPerPieceRatio()
{
   current_min_ppr=1024;
   current_max_ppr=0;
   for(unsigned i=0; i<total_pieces; i++) {
      float ppr=piece_info[i].get_ratio();
      if(current_min_ppr>ppr)
	 current_min_ppr=ppr;
      if(current_max_ppr<ppr)
	 current_max_ppr=ppr;
   }
}

void Torrent::RebuildPiecesNeeded()
{
   pieces_needed.truncate();
   bool enter_end_game=true;
   for(unsigned i=0; i<total_pieces; i++) {
      if(!my_bitfield->get_bit(i)) {
	 if(!piece_info[i].has_a_downloader())
	    enter_end_game=false;
	 if(piece_info[i].has_no_sources())
	    continue;
	 pieces_needed.append(i);
      }
      piece_info[i].cleanup();
   }
   if(!end_game && enter_end_game) {
      LogNote(1,"entering End Game mode");
      end_game=true;
   }
   cmp_torrent=this;
   pieces_needed.qsort(PiecesNeededCmp);
   CalcPiecesStats();
   pieces_timer.Reset();
}

TorrentBuild::TorrentBuild(const char *path) :
   top_path(path),
   name(basename_ptr(path)),
   done(false),
   total_length(0),
   piece_length(0)
{
   name.rtrim('/');

   struct stat st;
   if(stat(path,&st)==-1) {
      error=SysError();
      return;
   }
   if(S_ISREG(st.st_mode)) {
      total_length=st.st_size;
      LogNote(10,"single file %s, size %lld",path,(long long)st.st_size);
      Finish();
      return;
   }
   if(!S_ISDIR(st.st_mode)) {
      error=new Error(-1,"Need a plain file or directory",true);
      return;
   }
   QueueDir("");
}
const char *TorrentBuild::lc_to_utf8(const char *s)
{
   if(!translate || !s)
      return s;

   translate->ResetTranslation();
   translate->PutTranslated(s);
   int len;
   translate->Get(&s,&len);
   translate->Skip(len);
   return xstring::get_tmp(s,len);
}
void TorrentBuild::Finish()
{
   done=true;
   LogNote(10,"scan finished, total_length=%lld",(long long)total_length);

   translate=new DirectedBuffer(DirectedBuffer::PUT);
   translate->SetTranslation("UTF-8",false);

   xmap_p<BeNode> *b_info=new xmap_p<BeNode>();
   b_info->add("name",new BeNode(lc_to_utf8(name)));

   piece_length=16*1024;
   off_t length_scan=piece_length*2200;
   while(length_scan<=total_length) {
      piece_length*=2;
      length_scan*=2;
   }
   b_info->add("piece length",new BeNode(piece_length));

   if(files.count()==0) {
      b_info->add("length",new BeNode(total_length));
   } else {
      files.Sort(FileSet::BYNAME);
      files.rewind();
      xarray_p<BeNode> *b_files=new xarray_p<BeNode>();
      for(FileInfo *fi=files.curr(); fi; fi=files.next()) {
	 xarray_p<BeNode> *path=new xarray_p<BeNode>();
	 const char *name_utf8=lc_to_utf8(fi->name);
	 char *p=alloca_strdup(name_utf8);
	 for(p=strtok(p,"/"); p; p=strtok(NULL,"/"))
	    path->append(new BeNode(p));
	 xmap_p<BeNode> *b_file=new xmap_p<BeNode>();
	 b_file->add("path",new BeNode(path));
	 b_file->add("length",new BeNode(fi->size));
	 b_files->append(new BeNode(b_file));
      }
      b_info->add("files",new BeNode(b_files));
   }
   info=new BeNode(b_info);
}
void TorrentBuild::SetPiece(unsigned p,const xstring& sha)
{
   assert(pieces.length()==p*20); // require sequential building
   pieces.append(sha);
}
const xstring& TorrentBuild::GetMetadata()
{
   info->dict.add("pieces",new BeNode(pieces));
   return info->Pack();
}
const xstring& TorrentBuild::Status() const
{
   if(Done())
      return xstring::get_tmp("");
   const char *curr=CurrPath();
   int count=files.count();
   if(curr[0])
      return xstring::format(plural("%d file$|s$ found, now scanning %s",count),count,curr);
   return xstring::format(plural("%d file$|s$ found",count),count);
}
int TorrentBuild::Do()
{
   int m=STALL;
   if(Done())
      return m;

   const char *curr_path=CurrPath();
   if(!curr_path) {
      Finish();
      return MOVED;
   }
   const char *full_path=dir_file(top_path,curr_path);
   full_path=alloca_strdup(full_path);

   DIR *dir=opendir(full_path);
   if(!dir) {
      if(NonFatalError(errno))
	 return m;
      if(dirs_to_scan.Count()<=1)
	 error=SysError();
      else
	 LogError(0,"opendir(%s): %s",full_path,strerror(errno));
      NextDir();
      return MOVED;
   }
   LogNote(10,"scanning %s",full_path);
   struct dirent *entry;
   while((entry=readdir(dir))!=0) {
      if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))
	 continue;
      struct stat st;
      const char *path=dir_file(full_path,entry->d_name);
      if(lstat(path,&st)==-1) {
	 LogError(0,"stat(%s): %s",path,strerror(errno));
	 continue;
      }
      if(S_ISREG(st.st_mode))
	 AddFile(dir_file(curr_path,entry->d_name),&st);
      else if(S_ISDIR(st.st_mode))
	 QueueDir(dir_file(curr_path,entry->d_name));
      else
	 LogNote(10,"ignoring %s (not a directory nor a plain file)",path);
   }
   closedir(dir);
   NextDir();
   return MOVED;
}
void TorrentBuild::AddFile(const char *path,struct stat *st)
{
   FileInfo *fi=new FileInfo(path);
   fi->SetSize(st->st_size);
   files.Add(fi);
   total_length+=st->st_size;
   LogNote(10,"adding %s, size %lld",path,(long long)fi->size);
}

int Torrent::Do()
{
   int m=STALL;
   if(Done() || shutting_down)
      return m;
   if(!complete && !building && timeout_timer.Stopped()) {
      SetError("timed out with no progress");
      return MOVED;
   }
   if(build_md && !files) {
      if(!building)
	 building=new TorrentBuild(metainfo_url);
      if(!building->Done())
	 return m;
      m=MOVED;
      if(building->Failed()) {
	 SetError(building->ErrorText());
	 return m;
      }
      InitTranslation();
      name.set(building->name);
      piece_length=building->piece_length;
      SetTotalLength(building->total_length);
      files=new TorrentFiles(building->GetFilesNode(),this);
      output_dir.set(building->GetBaseDirectory());
      StartValidating();
   }
   if(!metainfo_tree && metainfo_url && !md_download && !build_md) {
      // retrieve metainfo if don't have already.
      if(!metainfo_copy) {
	 if(metainfo_url.begins_with("magnet:?")) {
	    ParseMagnet(metainfo_url+8);
	    return MOVED;
	 }
	 if(metainfo_url.length()==40 && strspn(metainfo_url,"0123456789ABCDEFabcdef")==40
	 && access(metainfo_url,0)==-1) {
	    xstring& btih=xstring::get_tmp(metainfo_url);
	    btih.hex_decode();
	    assert(btih.length()==20);
	    info_hash.move_here(btih);
	    if(FindTorrent(info_hash)) {
	       SetError("This torrent is already running");
	       return MOVED;
	    }
	    StartMetadataDownload();
	    return MOVED;
	 }
	 FetchMetadataFromURL(metainfo_url);
	 m=MOVED;
      }
      if(metainfo_copy->Error()) {
	 SetError(Error::Fatal(metainfo_copy->ErrorText()));
	 metainfo_copy=0;
	 return MOVED;
      }
      if(!metainfo_copy->Done())
	 return m;
      LogNote(9,"meta-data EOF\n");
      int rest;
      const char *metainfo_buf;
      int metainfo_len;
      metainfo_copy->put->Get(&metainfo_buf,&metainfo_len);
      metainfo_tree=BeNode::Parse(metainfo_buf,metainfo_len,&rest);
      metainfo_copy=0;
      if(!metainfo_tree) {
	 SetError("Meta-data parse error");
	 return MOVED;
      }
      if(rest>0) {
	 SetError("Junk at the end of Meta-data");
	 return MOVED;
      }

      InitTranslation();

      LogNote(10,"Received meta-data:");
      Log::global->Write(10,metainfo_tree->Format());

      if(metainfo_tree->type!=BeNode::BE_DICT) {
	 SetError("Meta-data: wrong top level type, must be DICT");
         return MOVED;
      }

      const char *retracker=ResMgr::Query("torrent:retracker",GetName());
      int retracker_len=xstrlen(retracker);
      BeNode *announce_list=metainfo_tree->lookup("announce-list",BeNode::BE_LIST);
      if(announce_list) {
	 for(int i=0; i<announce_list->list.length(); i++) {
	    BeNode *announce_list1=announce_list->list[i];
	    if(announce_list1->type!=BeNode::BE_LIST)
	       continue;
	    SMTaskRef<TorrentTracker> new_tracker;
	    for(int j=0; j<announce_list1->list.length(); j++) {
	       BeNode *announce=announce_list1->list[j];
	       if(announce->type!=BeNode::BE_STR)
		  continue;
	       if(retracker_len && !strncmp(retracker,announce->str,retracker_len))
		  retracker=0, retracker_len=0;
	       if(!new_tracker)
		  new_tracker=new TorrentTracker(this,announce->str);
	       else
		  new_tracker->AddURL(announce->str);
	    }
	    if(new_tracker && !new_tracker->Failed()) {
	       new_tracker->tracker_no=trackers.count();
	       trackers.append(new_tracker.borrow());
	    }
	 }
      }

      if(trackers.count()==0) {
	 const xstring& announce=metainfo_tree->lookup_str("announce");
	 if(announce) {
	    SMTaskRef<TorrentTracker> new_tracker(
	       new TorrentTracker(this,announce));
	    if(!new_tracker->Failed())
	       trackers.append(new_tracker.borrow());
	 }
      }

      if(retracker_len) {
	 SMTaskRef<TorrentTracker> new_tracker(new TorrentTracker(this,retracker));
	 if(!new_tracker->Failed()) {
	    new_tracker->tracker_no=trackers.count();
            trackers.append(new_tracker.borrow());
	 }
      }

      BeNode *nodes=metainfo_tree->lookup("nodes",BeNode::BE_LIST);
      if(nodes && dht) {
	 for(int i=0; i<nodes->list.count(); i++) {
	    BeNode *n=nodes->list[i];
	    if(n->type!=BeNode::BE_LIST || n->list.count()<2)
	       continue;
	    BeNode *b_host=n->list[0];
	    BeNode *b_port=n->list[1];
	    if(b_host->type!=BeNode::BE_STR || b_port->type!=BeNode::BE_INT)
	       continue;
	    if(b_port->num<=0 || b_port->num>=65535)
	       continue;
	    ParsedURL u;
	    u.host.set(b_host->str);
	    u.port.set(xstring::format("%u",(unsigned)b_port->num));
	    xstring_ca node(u.Combine());
	    dht->AddBootstrapNode(node);
#if INET6
	    dht_ipv6->AddBootstrapNode(node);
#endif
	 }
      }

      info=Lookup(metainfo_tree,"info",BeNode::BE_DICT);
      if(!info)
         return MOVED;

      if(SetMetadata(info->str))
	 Startup();
      m=MOVED;
      if(Done())
	 return m;
   }
   if(peers_scan_timer.Stopped())
      ScanPeers();
   if(validating) {
      ValidatePiece(validate_index++);
      if(validate_index<total_pieces) {
	 recv_rate.Add(piece_length);
	 return MOVED;
      }
      recv_rate.Add(last_piece_length);
      validating=false;
      recv_rate.Reset();
      if(total_left==0) {
	 complete=true;
	 seed_timer.Reset();
	 if(stop_if_complete) {
	    LogNote(2,"torrent is already complete, stopping");
	    Shutdown();
	    return MOVED;
	 }
      }
      if(building) {
	 if(!complete) {
	    SetError("File validation error");
	    return MOVED;
	 }
	 if(!SetMetadata(building->GetMetadata()))
	    return MOVED;
	 building=0;
	 xstring magnet("magnet:?xt=urn:btih:");
	 magnet.append(info_hash.hexdump());
	 magnet.appendf("&xl=%lld",(long long)total_length);
	 magnet.append("&dn=");
	 magnet.append_url_encoded(name,URL_PATH_UNSAFE);
	 printf("%s\n",magnet.get());
	 Startup();
      }
      RestartPeers();
      dht_announce_timer.Stop();
   }
   if(GetPort())
      StartTrackers();
   if(dht_announce_timer.Stopped())
      AnnounceDHT();

   // count peers
   connected_peers_count=0;
   active_peers_count=0;
   complete_peers_count=0;
   for(int i=0; i<peers.count(); i++) {
      connected_peers_count+=peers[i]->Connected();
      active_peers_count+=peers[i]->Active();
      complete_peers_count+=peers[i]->Complete();
   }

   if(!metadata)
      return m;

   if(optimistic_unchoke_timer.Stopped())
      OptimisticUnchoke();

   // rebuild lists of needed pieces
   if(!complete && (pieces_needed.count()==0 || pieces_timer.Stopped()))
      RebuildPiecesNeeded();

   if(complete) {
      if(pieces_timer.Stopped()) {
	 CalcPerPieceRatio();
	 pieces_timer.Reset();
      }
      if(SeededEnough()) {
	 Shutdown();
	 return MOVED;
      }
   }

   return m;
}

void Torrent::BlackListPeer(const TorrentPeer *peer,const char *timeout)
{
   if(peer->IsPassive() || GetTorrentsCount()==0)
      return;
   if(!black_list)
      black_list=new TorrentBlackList();
   black_list->Add(peer->GetAddress(),timeout);
}
bool Torrent::BlackListed(const TorrentPeer *peer)
{
   return black_list && black_list->Listed(peer->GetAddress());
}

bool Torrent::CanAccept() const
{
   return !validating && decline_timer.Stopped();
}

void Torrent::Accept(int s,const sockaddr_u *addr,IOBuffer *rb)
{
   if(!CanAccept()) {
      LogNote(4,"declining new connection");
      Delete(rb);
      close(s);
      return;
   }
   TorrentPeer *p=new TorrentPeer(this,addr,TorrentPeer::TR_ACCEPTED);
   p->Connect(s,rb);
   AddPeer(p);
}

void Torrent::AddPeer(TorrentPeer *peer)
{
   if(BlackListed(peer)) {
      Delete(peer);
      return;
   }
   for(int i=0; i<peers.count(); i++) {
      if(peers[i]->AddressEq(peer)) {
	 if(peer->Connected() && !peers[i]->Connected()) {
	    peers[i]=peer;
	 } else {
	    Delete(peer);
	 }
	 return;
      }
   }
   peers.append(peer);
}

int Torrent::GetWantedPeersCount() const
{
   int numwant=complete?seed_min_peers:max_peers/2;
   if(numwant>peers.count())
      numwant-=peers.count();
   else
      numwant=0;
   if(shutting_down)
      numwant=-1;
   if(numwant>1) {
      // count almost ready for request trackers
      int trackers_count=0;
      for(int i=0; i<trackers.count(); i++)
	 trackers_count+=(trackers[i]->tracker_timer.TimeLeft()<60);
      // request peers from all trackers evenly (round up).
      if(trackers_count)
	 numwant=(numwant+trackers_count-1)/trackers_count;
   }
   return numwant;
}

const char *Torrent::MakePath(BeNode *p) const
{
   BeNode *path=p->lookup("path.utf-8",BeNode::BE_LIST);
   void (Torrent::*tr)(BeNode*)const=&Torrent::TranslateStringFromUTF8;
   if(!path) {
      path=p->lookup("path",BeNode::BE_LIST);
      tr=&Torrent::TranslateString;
   }
   static xstring buf;
   buf.set(name);
   if(buf.eq("..") || buf[0]=='/') {
      buf.set_substr(0,0,"_",1);
   }
   for(int i=0; i<path->list.count(); i++) {
      BeNode *e=path->list[i];
      if(e->type==BeNode::BE_STR) {
	 (this->*tr)(e);
	 buf.append('/');
	 if(e->str_lc.eq(".."))
	    buf.append('_');
	 buf.append(e->str_lc);
      }
   }
   return buf;
}
const char *Torrent::FindFileByPosition(unsigned piece,unsigned begin,off_t *f_pos,off_t *f_tail) const
{
   off_t target_pos=(off_t)piece*piece_length+begin;
   TorrentFile *file=files->FindByPosition(target_pos);
   if(!file)
      return 0;

   *f_pos=target_pos-file->pos;
   *f_tail=file->length-*f_pos;

   return file->path;
}

TorrentFiles::TorrentFiles(const BeNode *files,const Torrent *t)
{
   if(!files) {
      grow_space(1);
      set_length(1);
      file(0)->set(t->GetName(),0,t->TotalLength());
   } else {
      int count=files->list.length();
      grow_space(count);
      set_length(count);
      off_t scan_pos=0;
      for(int i=0; i<count; i++) {
	 BeNode *node=files->list[i];
	 off_t file_length=node->lookup_int("length");
	 file(i)->set(t->MakePath(node),scan_pos,file_length);
	 scan_pos+=file_length;
      }
   }
   qsort(pos_cmp);
}
TorrentFile *TorrentFiles::FindByPosition(off_t pos)
{
   int i=0;
   int j=length()-1;
   while(i<=j) {
      // invariant: the target element is in the range [i,j]
      int m=(i+j)/2;
      if(file(m)->contains_pos(pos))
	 return file(m);
      if(file(m)->pos>pos)
	 j=m-1;
      else
	 i=m+1;
   }
   return 0;
}

FDCache::FDCache()
   : clean_timer(1)
{
   max_count=16;
   max_time=30;
}
FDCache::~FDCache()
{
   CloseAll();
}
void FDCache::Clean()
{
   for(int i=0; i<3; i++) {
      xmap<FD>& cache=this->cache[i];
      for(const FD *f=&cache.each_begin(); f->last_used; f=&cache.each_next()) {
	 if(f->fd==-1) {
	    if(f->last_used+1<now.UnixTime())
	       cache.remove(cache.each_key());
	    continue;
	 }
	 if(f->last_used+max_time<now.UnixTime()) {
	    ProtoLog::LogNote(9,"closing %s",cache.each_key().get());
	    close(f->fd);
	    cache.remove(cache.each_key());
	 }
      }
   }
   while(Count()>max_count && CloseOne())
      /*empty*/;
   if(Count()>0)
      clean_timer.Reset();
}
int FDCache::Do()
{
   if(clean_timer.Stopped())
      Clean();
   return STALL;
}
void FDCache::Close(const char *name)
{
   const xstring &n=xstring::get_tmp(name);
   for(int i=0; i<3; i++) {
      const FD& f=cache[i].lookup(n);
      if(f.last_used!=0) {
	 if(f.fd!=-1) {
	    ProtoLog::LogNote(9,"closing %s",name);
#ifdef HAVE_POSIX_FADVISE
	    if(i==O_RDONLY) // avoid filling up the cache
	       posix_fadvise(f.fd,0,0,POSIX_FADV_DONTNEED);
#endif
	    close(f.fd);
	 }
	 cache[i].remove(n);
      }
   }
}
void FDCache::CloseAll()
{
   for(int i=0; i<3; i++) {
      xmap<FD>& cache=this->cache[i];
      for(const FD *f=&cache.each_begin(); f->last_used; f=&cache.each_next()) {
	 if(f->fd!=-1) {
	    ProtoLog::LogNote(9,"closing %s",cache.each_key().get());
	    close(f->fd);
	 }
	 cache.remove(cache.each_key());
      }
   }
}
bool FDCache::CloseOne()
{
   int oldest_mode=0;
   int oldest_fd=-1;
   int oldest_time=0;
   const xstring *oldest_key=0;
   for(int i=0; i<3; i++) {
      xmap<FD>& cache=this->cache[i];
      for(const FD *f=&cache.each_begin(); f->last_used; f=&cache.each_next()) {
	 if(f->fd==-1)
	    continue;
	 if(oldest_key==0 || f->last_used<oldest_time) {
	    oldest_key=&cache.each_key();
	    oldest_time=f->last_used;
	    oldest_fd=f->fd;
	    oldest_mode=i;
	 }
      }
   }
   if(!oldest_key)
      return false;
   if(oldest_fd!=-1) {
      ProtoLog::LogNote(9,"closing %s",oldest_key->get());
      close(oldest_fd);
   }
   cache[oldest_mode].remove(*oldest_key);
   return true;
}
int FDCache::Count() const
{
   return cache[0].count()+cache[1].count()+cache[2].count();
}
int FDCache::OpenFile(const char *file,int m,off_t size)
{
   int ci=m&3;
   assert(ci<3);
   FD& f=cache[ci].lookup_Lv(file);
   if(f.last_used!=0) {
      if(f.fd!=-1)
	 f.last_used=now.UnixTime();
      else
	 errno=f.saved_errno;
      return f.fd;
   }
   if(ci==O_RDONLY) {
      // O_RDWR also will do, check if we have it.
      const FD& f_rw=cache[O_RDWR].lookup(file);
      if(f_rw.last_used!=0 && f_rw.fd!=-1) {
	 // don't update last_used to expire it and reopen with proper mode
	 return f_rw.fd;
      }
   }
   Clean();
   clean_timer.Reset();
   ProtoLog::LogNote(9,"opening %s",file);
   int fd;
   do {
      fd=open(file,m,0664);
   } while(fd==-1 && (errno==EMFILE || errno==ENFILE) && CloseOne());
   FD new_entry = {fd,errno,now.UnixTime()};
   cache[ci].add(file,new_entry);
   if(fd!=-1)
      fcntl(fd,F_SETFD,FD_CLOEXEC);
   if(fd==-1 || size==0)
      return fd;
   if(ci==O_RDWR && QueryBool("file:use-fallocate",0)) {
      struct stat st;
      // check if it is newly created file, then allocate space
      if(fstat(fd,&st)!=-1 && st.st_size==0) {
	 if(lftp_fallocate(fd,size)==-1 && errno!=ENOSYS && errno!=EOPNOTSUPP) {
	    ProtoLog::LogError(9,"space allocation for %s (%lld bytes) failed: %s",
	       file,(long long)size,strerror(errno));
	 }
      }
   }
#ifdef HAVE_POSIX_FADVISE
   if(ci==O_RDONLY) {
      // validation mode (when validating, size>0)
      posix_fadvise(fd,0,size,POSIX_FADV_SEQUENTIAL);
      posix_fadvise(fd,0,size,POSIX_FADV_NOREUSE);
   }
#endif//HAVE_POSIX_FADVISE
   return fd;
}

int Torrent::OpenFile(const char *file,int m,off_t size)
{
   bool did_mkdir=false;
   if(!fd_cache)
      fd_cache=new FDCache();
try_again:
   const char *cf=dir_file(output_dir,file);
   int fd=fd_cache->OpenFile(cf,m,size);
   while(fd==-1 && (errno==EMFILE || errno==ENFILE) && peers.count()>0) {
      peers.chop();  // free an fd
      fd=fd_cache->OpenFile(cf,m,size);
   }
   if(validating)
      return fd;
   if(fd==-1)
      fd_cache->Close(cf); // remove negative cache.
   if(fd==-1 && errno==ENOENT && !did_mkdir) {
      LogError(10,"open(%s): %s",cf,strerror(errno));
      const char *sl=strchr(file,'/');
      while(sl)
      {
	 if(sl>file) {
	    if(mkdir(cf=dir_file(output_dir,xstring::get_tmp(file,sl-file)),0775)==-1 && errno!=EEXIST)
	       LogError(9,"mkdir(%s): %s",cf,strerror(errno));
	 }
	 sl=strchr(sl+1,'/');
      }
      did_mkdir=true;
      goto try_again;
   }
   return fd;
}
void Torrent::CloseFile(const char *file) const
{
   if(!fd_cache)
      return;
   fd_cache->Close(dir_file(output_dir,file));
}

void Torrent::SetPieceNotWanted(unsigned piece)
{
   for(int j=0; j<pieces_needed.count(); j++) {
      if(pieces_needed[j]==piece) {
	 pieces_needed.remove(j);
	 break;
      }
   }
}

#define MIN(a,b) ((a)<(b)?(a):(b))

void Torrent::StoreBlock(unsigned piece,unsigned begin,unsigned len,const char *buf,TorrentPeer *src_peer)
{
   for(int i=0; i<peers.count(); i++)
      peers[i]->CancelBlock(piece,begin);

   unsigned b=begin/BLOCK_SIZE;
   int bc=(len+BLOCK_SIZE-1)/BLOCK_SIZE;

   off_t f_pos=0;
   off_t f_rest=len;
   while(len>0) {
      const char *file=FindFileByPosition(piece,begin,&f_pos,&f_rest);
      int fd=OpenFile(file,O_RDWR|O_CREAT,f_pos+f_rest);
      if(fd==-1) {
	 SetError(xstring::format("open(%s): %s",file,strerror(errno)));
	 return;
      }
      int w=pwrite(fd,buf,MIN(f_rest,len),f_pos);
      int saved_errno=errno;
      if(w==-1) {
	 SetError(xstring::format("pwrite(%s): %s",file,strerror(saved_errno)));
	 return;
      }
      if(w==0) {
	 SetError(xstring::format("pwrite(%s): write error - disk full?",file));
	 return;
      }
      buf+=w;
      begin+=w;
      len-=w;
   }

   while(bc-->0) {
      SetBlockPresent(piece,b++);
   }
   if(AllBlocksPresent(piece) && !my_bitfield->get_bit(piece)) {
      ValidatePiece(piece);
      if(!my_bitfield->get_bit(piece)) {
	 LogError(0,"new piece %u digest mismatch",piece);
	 src_peer->MarkPieceInvalid(piece);
	 return;
      }
      LogNote(3,"piece %u complete",piece);
      timeout_timer.Reset();
      SetPieceNotWanted(piece);
      for(int i=0; i<peers.count(); i++)
	 peers[i]->Have(piece);
      if(my_bitfield->has_all_set() && !complete) {
	 complete=true;
	 seed_timer.Reset();
	 end_game=false;
	 ScanPeers();
	 SendTrackersRequest("completed");
	 recv_rate.Reset();
      }
   }
}
void Torrent::SendTrackersRequest(const char *event) const
{
   for(int i=0; i<trackers.count(); i++) {
      if(!trackers[i]->Failed())
	 trackers[i]->SendTrackerRequest(event);
   }
}

const xstring& Torrent::RetrieveBlock(unsigned piece,unsigned begin,unsigned len)
{
   static xstring buf;
   buf.truncate(0);
   buf.get_space(len);

   off_t f_pos=0;
   off_t f_rest=len;
   while(len>0) {
      const char *file=FindFileByPosition(piece,begin,&f_pos,&f_rest);
      int fd=OpenFile(file,O_RDONLY,validating?f_pos+f_rest:0);
      if(fd==-1)
	 return xstring::null;
      int w=pread(fd,buf.add_space(len),MIN(f_rest,len),f_pos);
      if(w==-1) {
	 SetError(xstring::format("pread(%s): %s",file,strerror(errno)));
	 return xstring::null;
      }
      if(w==0) {
// 	 buf.append_padding(len,'\0');
	 break;
      }
      buf.add_commit(w);
      begin+=w;
      len-=w;
      if(validating && w==f_rest)
	 CloseFile(file);
   }
   return buf;
}

TorrentPeer *Torrent::FindPeerById(const xstring& p_id)
{
   // linear search - peers count<100, called rarely
   for(int i=0; i<peers.count(); i++) {
      const TorrentPeer *peer=peers[i];
      if(peer->peer_id.eq(p_id))
	 return const_cast<TorrentPeer*>(peer);
   }
   return 0;
}

void Torrent::ScanPeers() {
   // scan existing peers
   for(int i=0; i<peers.count(); i++) {
      const TorrentPeer *peer=peers[i];
      const char *blacklist_time="2h";
      if(peer->Failed()) {
	 LogError(2,"peer %s failed: %s",peer->GetName(),peer->ErrorText());
      } else if(peer->Disconnected() && peer->ActivityTimedOut()) {
	 LogNote(4,"peer %s disconnected",peer->GetName());
      } else if(peer->myself) {
	 LogNote(4,"removing myself-connected peer %s",peer->GetName());
	 blacklist_time="forever";
      } else if(peer->duplicate) {
	 LogNote(4,"removing duplicate peer %s",peer->GetName());
      } else if(complete && peer->Seed()) {
	 LogNote(4,"removing unneeded peer %s (%s)",peer->GetName(),peers[i]->Status());
	 blacklist_time="1d";
      } else {
	 // keep the peer.
	 continue;
      }
      if(blacklist_time)
	 BlackListPeer(peer,blacklist_time);
      peers.remove(i--);
   }
   ReducePeers();
   peers_scan_timer.Reset();
}

void Torrent::ReducePeers()
{
   if(max_peers>0 && peers.count()>max_peers) {
      // remove least interesting peers.
      peers.qsort(PeersCompareActivity);
      int to_remove=peers.count()-max_peers;
      while(to_remove-->0) {
	 TimeInterval max_idle(peers.last()->activity_timer.TimePassed());
	 LogNote(3,"removing peer %s (too many; idle:%s)",peers.last()->GetName(),
	    max_idle.toString(TimeInterval::TO_STR_TERSE+TimeInterval::TO_STR_TRANSLATE));
	 peers.chop();
	 if(max_idle<60)
	    decline_timer.Set(60-max_idle.Seconds());
      }
   }
   peers.qsort(complete ? PeersCompareSendRate : PeersCompareRecvRate);
   ReduceUploaders();
   ReduceDownloaders();
   UnchokeBestUploaders();
}
void Torrent::ReduceUploaders()
{
   bool rate_low = RateLow(RateLimit::GET);
   if(am_interested_peers_count < (rate_low?max_uploaders:min_uploaders+1))
      return;
   // make the slowest uninterested
   for(int i=0; i<peers.count(); i++) {
      TorrentPeer *peer=peers[i].get_non_const();
      if(peer->am_interested) {
	 if(peer->interest_timer.TimePassed() <= 30)
	    break;
	 peer->SetAmInterested(false);
	 if(am_interested_peers_count < max_uploaders)
	    break;
      }
   }
}
void Torrent::ReduceDownloaders()
{
   bool rate_low = RateLow(RateLimit::PUT);
   if(am_not_choking_peers_count < (rate_low?max_downloaders:min_downloaders+1))
      return;
   // choke the slowest
   for(int i=0; i<peers.count(); i++) {
      TorrentPeer *peer=peers[i].get_non_const();
      if(!peer->am_choking && peer->peer_interested) {
	 if(peer->choke_timer.TimePassed() <= 30)
	    break;
	 peer->SetAmChoking(true);
	 if(am_not_choking_peers_count < max_downloaders)
	    break;
      }
   }
}

bool Torrent::NeedMoreUploaders()
{
   if(!metadata || validating)
      return false;
   return RateLow(RateLimit::GET) && am_interested_peers_count < max_uploaders
      && am_interested_timer.Stopped();
}
bool Torrent::AllowMoreDownloaders()
{
   if(!metadata || validating)
      return false;
   return RateLow(RateLimit::PUT) && am_not_choking_peers_count < max_downloaders;
}

void Torrent::UnchokeBestUploaders()
{
   if(!metadata)
      return;

   // unchoke 4 best uploaders
   int limit = 4;

   int count=0;
   for(int i=peers.count()-1; i>=0 && count<limit; i--) {
      TorrentPeer *peer=peers[i].get_non_const();
      if(!peer->Connected())
	 continue;
      if(!peer->choke_timer.Stopped())
	 continue;   // cannot change choke status yet
      if(!peer->peer_interested)
	 continue;
      peer->SetAmChoking(false);
      count++;
   }
}
void Torrent::OptimisticUnchoke()
{
   xarray<TorrentPeer*> choked_peers;
   for(int i=peers.count()-1; i>=0; i--) {
      TorrentPeer *peer=peers[i].get_non_const();
      if(!peer->Connected())
	 continue;
      if(!peer->choke_timer.Stopped())
	 continue;   // cannot change choke status yet
      if(peer->am_choking) {
	 if(!peer->peer_interested) {
	    peer->SetAmChoking(false);
	    continue;
	 }
	 choked_peers.append(peer);
	 if(peer->retry_timer.TimePassed()<60) {
	    // newly connected is more likely to be unchoked
	    choked_peers.append(peer);
	    choked_peers.append(peer);
	 }
      }
   }
   if(choked_peers.count()==0)
      return;
   choked_peers[rand()/13%choked_peers.count()]->SetAmChoking(false);
   optimistic_unchoke_timer.Reset();
}

int Torrent::PeerBytesAllowed(const TorrentPeer *peer,RateLimit::dir_t dir)
{
   float peer_rate=(dir==RateLimit::GET ? peer->peer_send_rate : peer->peer_recv_rate).Get();
   float total_rate=(dir==RateLimit::GET ? send_rate : recv_rate).Get();
   const int min_rate = 1024;
   // the more is the opposite rate the more rate allowed, with a minimum
   float bytes = rate_limit.BytesAllowed(dir);
   bytes *= (peer_rate  + min_rate)
          / (total_rate + active_peers_count*min_rate);
   return (int)bytes;
}
void Torrent::PeerBytesUsed(int b,RateLimit::dir_t dir)
{
   rate_limit.BytesUsed(b,dir);
}
void Torrent::Reconfig(const char *name)
{
   const char *c=GetName();
   max_peers=ResMgr::Query("torrent:max-peers",c);
   seed_min_peers=ResMgr::Query("torrent:seed-min-peers",c);
   stop_on_ratio=ResMgr::Query("torrent:stop-on-ratio",c);
   stop_min_ppr=ResMgr::Query("torrent:stop-min-ppr",c);
   rate_limit.Reconfig(name,metainfo_url);
   if(listener)
      StartDHT();
}

const xstring& Torrent::Status()
{
   if(metainfo_copy)
      return xstring::format(_("Getting meta-data: %s"),metainfo_copy->GetStatus());
   if(validating) {
      return xstring::format(_("Validation: %u/%u (%u%%) %s%s"),validate_index,total_pieces,
	 validate_index*100/total_pieces,recv_rate.GetStrS(),
	 recv_rate.GetETAStrFromSize((off_t)(total_pieces-validate_index-1)*piece_length+last_piece_length).get());
   }
   if(building)
      return building->Status();
   if(!metadata && !build_md) {
      if(md_download.length()>0)
	 return xstring::format(_("Getting meta-data: %s"),
	    xstring::format("%u/%u",(unsigned)md_download.length(),(unsigned)metadata_size).get());
      else
	 return xstring::get_tmp(_("Waiting for meta-data..."));
   }
   if(shutting_down) {
      for(int i=0; i<trackers.count(); i++) {
	 if(!trackers[i]->IsActive())
	    continue;
	 const char *status=trackers[i]->Status();
	 if(status[0]) {
	    xstring &s=xstring::get_tmp(_("Shutting down: "));
	    if(trackers.count()>1)
	       s.appendf("%d. ",i+1);
	    s.append(status);
	    return s;
	 }
      }
      return xstring::get_tmp("");
   }
   if(total_length==0)
      return xstring::get_tmp("");

   char dn_buf[LONGEST_HUMAN_READABLE + 1];
   char up_buf[LONGEST_HUMAN_READABLE + 1];
   xstring& buf=xstring::format("dn:%s %sup:%s %s",
      human_readable(total_recv, dn_buf, human_autoscale|human_SI, 1, 1),
      recv_rate.GetStrS(),
      human_readable(total_sent, up_buf, human_autoscale|human_SI, 1, 1),
      send_rate.GetStrS());
   if(!complete) {
      buf.appendf("complete:%u/%u (%u%%)",complete_pieces,total_pieces,
	 unsigned((total_length-total_left)*100/total_length));
      buf.append(' ');
      if(min_piece_sources)
	 buf.append(recv_rate.GetETAStrFromSize(total_left));
      if(end_game)
	 buf.append(" end-game");
   } else {
      buf.appendf("complete, ratio:%.2f/%.2f/%.2f",
	 GetMinPerPieceRatio(),GetRatio(),GetMaxPerPieceRatio());
   }
   return buf;
}


TorrentPeer::TorrentPeer(Torrent *p,const sockaddr_u *a,int t_no)
   : timeout_timer(360), retry_timer(30), keepalive_timer(120),
     choke_timer(10), interest_timer(10), activity_timer(300),
     msg_ext_metadata(0), msg_ext_pex(0), metadata_size(0)
{
   parent=p;
   tracker_no=t_no;
   addr=*a;
   sock=-1;
   udp_port=0;
   connected=false;
   passive=false;
   duplicate=0;
   myself=false;
   peer_choking=true;
   am_choking=true;
   peer_interested=false;
   am_interested=false;
   upload_only=false;
   peer_complete_pieces=0;
   retry_timer.Stop();
   retry_timer.AddRandom(2);
   choke_timer.Stop();
   interest_timer.Stop();
   last_piece=NO_PIECE;
   if(addr.is_reserved() || addr.is_multicast() || addr.port()==0)
      SetError("invalid peer address");
   peer_bytes_pool[0]=peer_bytes_pool[1]=0;
   peer_recv=peer_sent=0;
   invalid_piece_count=0;
}
TorrentPeer::~TorrentPeer()
{
}
void TorrentPeer::PrepareToDie()
{
   Disconnect();
}

void TorrentPeer::Connect(int s,IOBuffer *rb)
{
   sock=s;
   recv_buf=rb;
   connected=true;
   passive=true;
}

void TorrentPeer::SetError(const char *s)
{
   error=Error::Fatal(s);
   LogError(11,"fatal error: %s",s);
   Disconnect(s);
}

void TorrentPeer::Restart()
{
   if(!Connected())
      return;
   Disconnect();
   retry_timer.Stop();
   retry_timer.AddRandom(2);
}

void TorrentPeer::Disconnect(const char *dc)
{
   Enter();
   if(Connected() && !recv_buf->Eof())
      LogNote(4,"closing connection");
   recv_queue.empty();
   ClearSentQueue();
   if(peer_bitfield) {
      for(unsigned p=0; p<parent->total_pieces; p++)
	 SetPieceHaving(p,false);
      peer_bitfield=0;
   }
   peer_id.unset();
   fast_set.empty();
   suggested_set.empty();
   recv_buf=0;
   send_buf=0;
   if(sock!=-1) {
      close(sock);
      sock=-1;
      connected=false;
      last_dc.set(dc);
   }
   parent->am_interested_peers_count-=am_interested;
   am_interested=false;
   parent->am_not_choking_peers_count-=!am_choking;
   am_choking=true;
   peer_interested=false;
   peer_choking=true;
   peer_complete_pieces=0;
   retry_timer.Reset();
   choke_timer.Stop();
   interest_timer.Stop();
   // return to main pool
   parent->PeerBytesUsed(-peer_bytes_pool[RateLimit::GET],RateLimit::GET);
   parent->PeerBytesUsed(-peer_bytes_pool[RateLimit::PUT],RateLimit::PUT);
   peer_bytes_pool[0]=peer_bytes_pool[1]=0;
   Leave();
}

void TorrentPeer::SendHandshake()
{
   const char *const protocol="BitTorrent protocol";
   int proto_len=strlen(protocol);
   send_buf->PackUINT8(proto_len);
   send_buf->Put(protocol,proto_len);
   static char extensions[8] = {
      // extensions[7]&0x01 - DHT Protocol (http://www.bittorrent.org/beps/bep_0005.html)
      // extensions[7]&0x04 - Fast Extension (http://www.bittorrent.org/beps/bep_0006.html)
      // extensions[5]&0x10 - Extension Protocol (http://www.bittorrent.org/beps/bep_0010.html)
      0, 0, 0, 0, 0, 0x10, 0, 0x05,
   };
   if(ResMgr::QueryBool("torrent:use-dht",0))
      extensions[7]|=0x01;
   else
      extensions[7]&=~0x01;
   send_buf->Put(extensions,8);
   send_buf->Put(parent->info_hash);
   send_buf->Put(parent->my_peer_id);
   LogSend(9,"handshake");
}
TorrentPeer::unpack_status_t TorrentPeer::RecvHandshake()
{
   unsigned proto_len=0;
   if(recv_buf->Size()>0)
      proto_len=recv_buf->UnpackUINT8();

   if((unsigned)recv_buf->Size()<1+proto_len+8+SHA1_DIGEST_SIZE+Torrent::PEER_ID_LEN)
      return recv_buf->Eof() ? UNPACK_PREMATURE_EOF : UNPACK_NO_DATA_YET;

   int unpacked=1;
   const char *data=recv_buf->Get();

   xstring protocol(data+unpacked,proto_len);
   unpacked+=proto_len;

   memcpy(extensions,data+unpacked,8);
   unpacked+=8; // 8 bytes are reserved (extensions)

   xstring peer_info_hash(data+unpacked,SHA1_DIGEST_SIZE);
   unpacked+=SHA1_DIGEST_SIZE;
   if(peer_info_hash.ne(parent->info_hash)) {
      LogError(0,"got info_hash: %s, wanted: %s",peer_info_hash.hexdump(),parent->info_hash.hexdump());
      SetError("peer info_hash mismatch");
      return UNPACK_WRONG_FORMAT;
   }

   const xstring& tmp_peer_id=xstring::get_tmp(recv_buf->Get()+unpacked,Torrent::PEER_ID_LEN);
   unpacked+=Torrent::PEER_ID_LEN;
   // if we have already such a peer, then this peer or the other one
   // much be marked as duplicate and then removed in ScanPeers.
   duplicate=parent->FindPeerById(tmp_peer_id);
   if(duplicate && !duplicate->Connected()) {
      duplicate->duplicate=this;
      duplicate=0;
   }
   peer_id.set(tmp_peer_id);

   recv_buf->Skip(unpacked);
   LogRecv(4,xstring::format("handshake, %s, peer_id: %s, reserved: %02x%02x%02x%02x%02x%02x%02x%02x",
      protocol.dump(),url::encode(peer_id,"").get(),
      extensions[0],extensions[1],extensions[2],extensions[3],
      extensions[4],extensions[5],extensions[6],extensions[7]));

   return UNPACK_SUCCESS;
}
void TorrentPeer::SendExtensions()
{
  if(!LTEPExtensionEnabled())
      return;
   xmap_p<BeNode> m;
   m.add("ut_metadata",new BeNode(MSG_EXT_METADATA));
   m.add("ut_pex",new BeNode(MSG_EXT_PEX));
   xmap_p<BeNode> ext;
   ext.add("m",new BeNode(&m));
   ext.add("p",new BeNode(parent->GetPort()));
   ext.add("v",new BeNode(PACKAGE "/" VERSION));
   ext.add("reqq",new BeNode(MAX_QUEUE_LEN*16));
   if(parent->Complete())
      ext.add("upload_only",new BeNode(1));
   if(parent->metadata)
      ext.add("metadata_size",new BeNode(parent->metadata.length()));

   const char *ip=ResMgr::Query("torrent:ip",0);
   sockaddr_u sa;
   socklen_t sa_len=sizeof(sa);
   if((ip && ip[0] && inet_aton(ip,&sa.in.sin_addr))
   || (getsockname(sock,&sa.sa,&sa_len)!=-1 && sa.sa.sa_family==AF_INET))
      ext.add("ipv4",new BeNode((const char*)&sa.in.sin_addr,4));

#if INET6
   const char *ipv6=ResMgr::Query("torrent:ipv6",0);
   sa_len=sizeof(sa);
   if((ipv6 && ipv6[0] && inet_pton(AF_INET6,ipv6,&sa.in6.sin6_addr)>0)
   || (getsockname(sock,&sa.sa,&sa_len)!=-1 && sa.sa.sa_family==AF_INET6))
      ext.add("ipv6",new BeNode((const char*)&sa.in6.sin6_addr,16));
#endif

   sa_len=sizeof(sa);
   if(getpeername(sock,&sa.sa,&sa_len)!=-1) {
      if(sa.sa.sa_family==AF_INET)
	 ext.add("yourip",new BeNode((const char*)&sa.in.sin_addr,4));
#if INET6
      else if(sa.sa.sa_family==AF_INET6)
         ext.add("yourip",new BeNode((const char*)&sa.in6.sin6_addr,16));
#endif
   }

   PacketExtended pkt(MSG_EXT_HANDSHAKE,new BeNode(&ext));
   pkt.Pack(send_buf);
   LogSend(9,xstring::format("extended(%u,%s)",pkt.code,pkt.data->Format1()));
}

void TorrentPeer::SendDataReply()
{
   const PacketRequest *p=recv_queue.next();
   Enter(parent);
   const xstring& data=parent->RetrieveBlock(p->index,p->begin,p->req_length);
   Leave(parent);
   if(!Connected()) // we can be disconnected by parent
      return;
   if(data.length()!=p->req_length) {
      if(parent->my_bitfield->get_bit(p->index))
	 parent->SetError(xstring::format("failed to read piece %u",p->index));
      return;
   }
   LogSend(8,xstring::format("piece:%u begin:%u size:%u",p->index,p->begin,p->req_length));
   PacketPiece(p->index,p->begin,data).Pack(send_buf);
   peer_sent+=data.length();
   peer_send_rate.Add(data.length());
   parent->AccountSend(p->index,data.length());
   BytesPut(data.length());
   activity_timer.Reset();
}

int TorrentPeer::SendDataRequests(unsigned p)
{
   if(p==NO_PIECE)
      return 0;
   if(parent->my_bitfield->get_bit(p)
   || !peer_bitfield || !peer_bitfield->get_bit(p))
      return 0;

   int sent=0;
   unsigned blocks=parent->BlocksInPiece(p);
   unsigned bytes_allowed=BytesAllowed(RateLimit::GET);
   for(unsigned b=0; b<blocks; b++) {
      if(parent->BlockPresent(p,b))
	 continue;
      if(parent->piece_info[p].downloader_for(b)) {
	 if(!parent->end_game)
	    continue;
	 if(parent->piece_info[p].downloader_for(b)==this)
	    continue;
	 if(FindRequest(p,b*Torrent::BLOCK_SIZE)>=0)
	    continue;
      }

      unsigned begin=b*Torrent::BLOCK_SIZE;
      unsigned len=Torrent::BLOCK_SIZE;

      if(b==blocks-1) {
	 assert(begin<parent->PieceLength(p));
	 unsigned max_len=parent->PieceLength(p)-begin;
	 if(len>max_len)
	    len=max_len;
      }

      if(bytes_allowed<len)
	 break;

      parent->SetDownloader(p,b,0,this);
      PacketRequest *req=new PacketRequest(p,b*Torrent::BLOCK_SIZE,len);
      LogSend(6,xstring::format("request piece:%u begin:%u size:%u",p,b*Torrent::BLOCK_SIZE,len));
      req->Pack(send_buf);
      sent_queue.push(req);
      SetLastPiece(p);
      sent++;
      activity_timer.Reset();
      bytes_allowed-=len;
      BytesGot(len);

      if(sent_queue.count()>=MAX_QUEUE_LEN)
	 break;
   }
   return sent;
}

bool TorrentPeer::InFastSet(unsigned p) const
{
   for(int i=0; i<fast_set.count(); i++)
      if(fast_set[i]==p)
	 return true;
   return false;
}

void TorrentPeer::SendDataRequests()
{
   assert(am_interested);

   if(peer_choking && !FastExtensionEnabled())
      return;
   if(sent_queue.count()>=MAX_QUEUE_LEN)
      return;
   if(!BytesAllowedToGet(Torrent::BLOCK_SIZE))
      return;

   if(peer_choking) {
      // try to continue getting last piece if it is in the fast set
      unsigned last_piece=GetLastPiece();
      if(last_piece!=NO_PIECE && InFastSet(last_piece) && SendDataRequests(last_piece)>0)
	 return;
      // try fast set when choking
      while(fast_set.count()>0) {
	 unsigned p=fast_set[0];
	 if(SendDataRequests(p)>0)
	    return;
	 fast_set.next();
      }
      return;
   }

   // try to continue getting last piece
   if(SendDataRequests(GetLastPiece())>0)
      return;

   // try suggested pieces
   while(suggested_set.count()>0) {
      unsigned p=suggested_set.next();
      if(SendDataRequests(p)>0)
	 return;
   }

   // pick a new piece
   unsigned p=NO_PIECE;
   for(int i=0; i<parent->pieces_needed.count(); i++) {
      if(peer_bitfield->get_bit(parent->pieces_needed[i])) {
	 p=parent->pieces_needed[i];
	 if(parent->my_bitfield->get_bit(p))
	    continue;
	 // add some randomness, so that different instances don't synchronize
	 if(parent->AllBlocksAbsent(p) && random()/13%16==0)
	    continue;
	 if(SendDataRequests(p)>0)
	    return;
      }
   }
   if(p==NO_PIECE && interest_timer.Stopped())
      SetAmInterested(false);
}

void TorrentPeer::Have(unsigned p)
{
   if(!send_buf)
      return;
   Enter();
   LogSend(9,xstring::format("have(%u)",p));
   PacketHave(p).Pack(send_buf);
   Leave();
}
int TorrentPeer::FindRequest(unsigned piece,unsigned begin) const
{
   for(int i=0; i<sent_queue.count(); i++) {
      const PacketRequest *req=sent_queue[i];
      if(req->index==piece && req->begin==begin)
	 return i;
   }
   return -1;
}
void TorrentPeer::CancelBlock(unsigned p,unsigned b)
{
   if(!send_buf)
      return;
   Enter();
   int i=FindRequest(p,b);
   if(i>=0) {
      const PacketRequest *req=sent_queue[i];
      LogSend(9,xstring::format("cancel(%u,%u)",p,b));
      PacketCancel(p,b,req->req_length).Pack(send_buf);
      parent->SetDownloader(p,b/Torrent::BLOCK_SIZE,this,0);
      sent_queue.remove(i);
   }
   Leave();
}

// mark that peer as having an invalid piece
void TorrentPeer::MarkPieceInvalid(unsigned p)
{
   invalid_piece_count++;
   SetPieceHaving(p,false);
   SetAmInterested(am_interested);
   if(invalid_piece_count>5)
      parent->BlackListPeer(this,"1d");
}

void TorrentPeer::ClearSentQueue(int i)
{
   if(i<0)
      return;
   if(!FastExtensionEnabled()) {
      // without Fast Extension we assume sequential packet processing,
      // thus clear also all sent requests before this one.
      while(i-->=0) {
	 const PacketRequest *req=sent_queue.next();
	 parent->PeerBytesGot(-req->req_length);
	 parent->SetDownloader(req->index,req->begin/Torrent::BLOCK_SIZE,this,0);
      }
   } else {
      const PacketRequest *req=sent_queue[i];
      parent->PeerBytesGot(-req->req_length);
      parent->SetDownloader(req->index,req->begin/Torrent::BLOCK_SIZE,this,0);
      sent_queue.remove(i);
   }
}

int TorrentPeer::BytesAllowed(RateLimit::dir_t dir)
{
   int a=parent->PeerBytesAllowed(this,dir);
   int pool_target=Torrent::BLOCK_SIZE*2;
   if(peer_bytes_pool[dir]<pool_target) {
      int to_pool=pool_target-peer_bytes_pool[dir];
      if(to_pool>a)
	 to_pool=a;
      peer_bytes_pool[dir]+=to_pool;
      a-=to_pool;
      parent->PeerBytesUsed(to_pool,dir);
   }
   return peer_bytes_pool[dir]+a;
}
bool TorrentPeer::BytesAllowed(RateLimit::dir_t dir,unsigned bytes)
{
   int a=BytesAllowed(dir);
   if(bytes<=(unsigned)a)
      return true;
   TimeoutS(1);
   return false;
}
void TorrentPeer::BytesUsed(int b,RateLimit::dir_t dir)
{
   if(peer_bytes_pool[dir]>=b)
      peer_bytes_pool[dir]-=b;
   else {
      b-=peer_bytes_pool[dir];
      peer_bytes_pool[dir]=0;
      parent->PeerBytesUsed(b,dir);
   }
}

unsigned TorrentPeer::GetLastPiece() const
{
   if(!peer_bitfield)
      return NO_PIECE;
   unsigned p=last_piece;
   // continue if have any blocks already
   if(p!=NO_PIECE && !parent->my_bitfield->get_bit(p)
   && parent->AnyBlocksPresent(p)
   && peer_bitfield->get_bit(p))
      return p;
   p=parent->last_piece;
   if(p!=NO_PIECE && !parent->my_bitfield->get_bit(p)
   && peer_bitfield->get_bit(p))
      return p;
   p=last_piece;
   if(p!=NO_PIECE && !parent->my_bitfield->get_bit(p)
   && peer_bitfield->get_bit(p))
      return p;
   return NO_PIECE;
}

void TorrentPeer::SetLastPiece(unsigned p)
{
   if(last_piece==NO_PIECE || parent->my_bitfield->get_bit(last_piece))
      last_piece=p;
   if(parent->last_piece==NO_PIECE || parent->my_bitfield->get_bit(parent->last_piece))
      parent->last_piece=p;
}

void TorrentPeer::SetAmInterested(bool interest)
{
   if(invalid_piece_count>5)
      interest=false;
   if(am_interested==interest)
      return;
   Enter();
   LogSend(6,interest?"interested":"uninterested");
   Packet(interest?MSG_INTERESTED:MSG_UNINTERESTED).Pack(send_buf);
   parent->am_interested_peers_count+=(interest-am_interested);
   am_interested=interest;
   interest_timer.Reset();
   if(am_interested)
      parent->am_interested_timer.Reset();
   (void)BytesAllowed(RateLimit::GET); // draw some bytes from the common pool
   Leave();
}
void TorrentPeer::SetAmChoking(bool c)
{
   if(am_choking==c)
      return;
   Enter();
   LogSend(6,c?"choke":"unchoke");
   Packet(c?MSG_CHOKE:MSG_UNCHOKE).Pack(send_buf);
   parent->am_not_choking_peers_count-=(c-am_choking);
   am_choking=c;
   choke_timer.Reset();
   if(am_choking) {
      if(!FastExtensionEnabled()) {
	 recv_queue.empty();
      } else {
	 // send rejects
	 while(recv_queue.count()>0) {
	    const PacketRequest *p=recv_queue.next();
	    LogSend(6,xstring::format("reject-request piece:%u begin:%u size:%u",p->index,p->begin,p->req_length));
	    PacketRejectRequest(p->index,p->begin,p->req_length).Pack(send_buf);
	 }
      }
   }
   Leave();
}

void TorrentPeer::SetPieceHaving(unsigned p,bool have)
{
   int diff = (have - peer_bitfield->get_bit(p));
   if(!diff)
      return;
   parent->piece_info[p].add_sources_count(diff);
   peer_complete_pieces+=diff;
   peer_bitfield->set_bit(p,have);

   if(parent->piece_info[p].get_sources_count()==0)
      parent->SetPieceNotWanted(p);
   if(have && send_buf && !am_interested && !parent->my_bitfield->get_bit(p)
   && parent->NeedMoreUploaders()) {
      SetAmInterested(true);
      SetLastPiece(p);
   }
}

void TorrentPeer::HandlePacket(Packet *p)
{
   switch(p->GetPacketType())
   {
   case MSG_KEEPALIVE: {
	 LogRecv(5,"keep-alive");
	 break;
      }
   case MSG_CHOKE: {
	 LogRecv(5,"choke");
	 peer_choking=true;
	 ClearSentQueue(); // discard pending requests
	 break;
      }
   case MSG_UNCHOKE: {
	 LogRecv(5,"unchoke");
	 peer_choking=false;
	 if(am_interested)
	    SendDataRequests();
	 break;
      }
   case MSG_INTERESTED: {
	 LogRecv(5,"interested");
	 peer_interested=true;
	 break;
      }
   case MSG_UNINTERESTED: {
	 LogRecv(5,"uninterested");
	 recv_queue.empty();
	 peer_interested=false;
	 break;
      }
   case MSG_HAVE: {
	 if(!parent->HasMetadata())
	    break;
	 PacketHave *pp=static_cast<PacketHave*>(p);
	 LogRecv(5,xstring::format("have(%u)",pp->piece));
	 if(!parent->HasMetadata())
	    break;
	 if(pp->piece>=parent->total_pieces) {
	    SetError("invalid piece index");
	    break;
	 }
	 SetPieceHaving(pp->piece,true);
	 break;
      }
   case MSG_BITFIELD: {
	 if(!parent->HasMetadata())
	    break;
	 PacketBitField *pp=static_cast<PacketBitField*>(p);
	 if(pp->bitfield->count()<(int)parent->total_pieces/8) {
	    LogError(9,"bitfield length %d, expected %u",pp->bitfield->count(),parent->total_pieces/8);
	    SetError("invalid bitfield length");
	    break;
	 }
	 if(pp->bitfield->has_any_set(parent->total_pieces,pp->bitfield->get_bit_length())) {
	    SetError("bitfield has spare bits set");
	    break;
	 }
	 for(unsigned p=0; p<parent->total_pieces; p++)
	    SetPieceHaving(p,pp->bitfield->get_bit(p));
	 LogRecv(5,xstring::format("bitfield(%u/%u)",peer_complete_pieces,parent->total_pieces));
	 break;
      }
   case MSG_PORT: {
	 PacketPort *pp=static_cast<PacketPort*>(p);
	 LogRecv(5,xstring::format("port(%u)",pp->port));
	 udp_port=pp->port;
	 if(DHT_Enabled() && Torrent::dht) {
	    sockaddr_u a(addr);
	    a.set_port(udp_port);
	    Torrent::GetDHT(a)->SendPing(a);
	 }
	 break;
      }
   case MSG_HAVE_ALL: {
	 LogRecv(5,"have-all");
	 if(!FastExtensionEnabled()) {
	    SetError("fast extension is disabled");
	    break;
	 }
	 if(!parent->HasMetadata())
	    break;
	 for(unsigned p=0; p<parent->total_pieces; p++)
	    SetPieceHaving(p,1);
	 break;
      }
   case MSG_HAVE_NONE: {
	 LogRecv(5,"have-none");
	 if(!FastExtensionEnabled()) {
	    SetError("fast extension is disabled");
	    break;
	 }
	 if(!parent->HasMetadata())
	    break;
	 for(unsigned p=0; p<parent->total_pieces; p++)
	    SetPieceHaving(p,0);
	 break;
      }
   case MSG_SUGGEST_PIECE: {
	 PacketSuggestPiece *pp=static_cast<PacketSuggestPiece*>(p);
	 LogRecv(5,xstring::format("suggest-piece:%u",pp->piece));
	 if(!FastExtensionEnabled()) {
	    SetError("fast extension is disabled");
	    break;
	 }
	 if(!parent->HasMetadata())
	    break;
	 if(pp->piece>=parent->total_pieces) {
	    SetError("invalid piece index");
	    break;
	 }
	 suggested_set.push(pp->piece);
	 break;
      }
   case MSG_ALLOWED_FAST: {
	 PacketAllowedFast *pp=static_cast<PacketAllowedFast*>(p);
	 LogRecv(5,xstring::format("allowed-fast:%u",pp->piece));
	 if(!FastExtensionEnabled()) {
	    SetError("fast extension is disabled");
	    break;
	 }
	 if(!parent->HasMetadata())
	    break;
	 if(pp->piece>=parent->total_pieces) {
	    SetError("invalid piece index");
	    break;
	 }
	 fast_set.push(pp->piece);
	 break;
      }
   case MSG_REJECT_REQUEST: {
	 PacketRejectRequest *pp=static_cast<PacketRejectRequest*>(p);
	 LogRecv(5,xstring::format("reject-request(%u,%u)",pp->index,pp->begin));
	 if(!FastExtensionEnabled()) {
	    SetError("fast extension is disabled");
	    break;
	 }
	 int i=FindRequest(pp->index,pp->begin);
	 if(i>=0)
	    ClearSentQueue(i);
	 break;
      }
   case MSG_EXTENDED: {
	 PacketExtended *pp=static_cast<PacketExtended*>(p);
	 LogRecv(9,xstring::format("extended(%u,%s)",pp->code,pp->data->Format1()));
	 HandleExtendedMessage(pp);
	 break;
      }
   case MSG_PIECE: {
	 PacketPiece *pp=static_cast<PacketPiece*>(p);
	 LogRecv(7,xstring::format("piece:%u begin:%u size:%u",pp->index,pp->begin,(unsigned)pp->data.length()));
	 if(!parent->HasMetadata())
	    break;
	 if(pp->index>=parent->total_pieces) {
	    SetError("invalid piece index");
	    break;
	 }
	 if(pp->begin>=parent->PieceLength(pp->index)) {
	    SetError("invalid data offset");
	    break;
	 }
	 if(pp->begin+pp->data.length() > parent->PieceLength(pp->index)) {
	    SetError("invalid data length");
	    break;
	 }
	 int i=FindRequest(pp->index,pp->begin);
	 if(i<0) {
// 	    SetError("got a piece that was not requested");
	    break;
	 }
	 ClearSentQueue(i);
	 parent->PeerBytesGot(pp->data.length()); // re-take the bytes returned by ClearSentQueue
	 Enter(parent);
	 parent->StoreBlock(pp->index,pp->begin,pp->data.length(),pp->data.get(),this);
	 Leave(parent);

	 int len=pp->data.length();
	 peer_recv+=len;
	 peer_recv_rate.Add(len);
	 parent->AccountRecv(pp->index,len);

	 // request another block from the same piece
	 if(am_interested && (!peer_choking || InFastSet(pp->index)))
	    SendDataRequests(pp->index);
	 break;
      }
   case MSG_REQUEST: {
	 PacketRequest *pp=static_cast<PacketRequest*>(p);
	 LogRecv(5,xstring::format("request for piece:%u begin:%u size:%u",pp->index,pp->begin,pp->req_length));
	 if(pp->req_length>Torrent::BLOCK_SIZE*2) {
	    SetError("too large request");
	    break;
	 }
	 if(!parent->HasMetadata())
	    break;
	 if(am_choking)
	    break;
	 if(pp->index>=parent->total_pieces) {
	    SetError("invalid piece index");
	    break;
	 }
	 if(pp->begin>=parent->PieceLength(pp->index)) {
	    SetError("invalid data offset");
	    break;
	 }
	 if(pp->begin+pp->req_length > parent->PieceLength(pp->index)) {
	    SetError("invalid data length");
	    break;
	 }
	 if(recv_queue.count()>=MAX_QUEUE_LEN*16) {
	    SetError("too many requests");
	    break;
	 }
	 recv_queue.push(pp);
	 activity_timer.Reset();
	 p=0;
	 break;
      }
   case MSG_CANCEL: {
	 PacketCancel *pp=static_cast<PacketCancel*>(p);
	 LogRecv(5,xstring::format("cancel(%u,%u)",pp->index,pp->begin));
	 for(int i=0; i<recv_queue.count(); i++) {
	    const PacketRequest *req=recv_queue[i];
	    if(req->index==pp->index && req->begin==pp->begin) {
	       recv_queue.remove(i);
	       break;
	    }
	 }
	 break;
      }
   }
   delete p;
}

void Torrent::MetadataDownloaded()
{
   xstring new_info_hash;
   SHA1(md_download,new_info_hash);
   if(info_hash && info_hash.ne(new_info_hash)) {
      LogError(1,"downloaded metadata does not match info_hash, retrying");
      StartMetadataDownload();
      return;
   }
   if(SetMetadata(md_download))
      Startup();
   md_download.unset();
}

void TorrentPeer::SendMetadataRequest()
{
   if(!msg_ext_metadata || !parent->md_download || parent->md_download.length()>=metadata_size
   || parent->md_download.length()%Torrent::BLOCK_SIZE)
      return;
   xmap_p<BeNode> req;
   req.add("msg_type",new BeNode(UT_METADATA_REQUEST));
   req.add("piece",new BeNode(parent->md_download.length()/Torrent::BLOCK_SIZE));
   PacketExtended pkt(msg_ext_metadata,new BeNode(&req));
   LogSend(4,xstring::format("ut_metadata request %s",pkt.data->Format1()));
   pkt.Pack(send_buf);
}

void TorrentPeer::HandleExtendedMessage(PacketExtended *pp)
{
   if(pp->data->type!=BeNode::BE_DICT) {
      SetError("extended type must be DICT");
      return;
   }
   if(pp->code==MSG_EXT_HANDSHAKE) {
      BeNode *m=pp->data->lookup("m",BeNode::BE_DICT);
      if(m) {
	 msg_ext_metadata=m->lookup_int("ut_metadata");
	 msg_ext_pex=m->lookup_int("ut_pex");
      }
      metadata_size=parent->metadata_size=pp->data->lookup_int("metadata_size");
      upload_only=pp->data->lookup_int("upload_only");

      if(!parent->HasMetadata() && !msg_ext_metadata) {
	 Disconnect("peer cannot provide metadata");
	 return;
      }

      const xstring& v=pp->data->lookup_str("v");
      if(v)
	 LogNote(3,"peer version is %s",v.get());

      const xstring& myip=pp->data->lookup_str("yourip");
      if(myip && myip.length()==4) {
	 char ip[16];
	 inet_ntop(AF_INET,myip.get(),ip,sizeof(ip));
	 LogNote(5,"my external IPv4 is %s",ip);
      }
      if(passive) {
	 // use specified port number to connect back
	 int p=pp->data->lookup_int("p");
	 if(p && p>=1024 && p<=65535) {
	    LogNote(9,"using port %d to connect back",p);
	    addr.set_port(p);
	    passive=false;
	    // check the black list
	    if(Torrent::BlackListed(this)) {
	       SetError("blacklisted");
	       return;
	    }
	    // check for duplicates
	    TaskRefArray<TorrentPeer>& peers=parent->peers;
	    for(int i=0; i<peers.count(); i++) {
	       if(peers[i]!=this && peers[i]->AddressEq(this)) {
		  if(!peers[i]->Connected())
		     peers[i]->duplicate=this;
		  else
		     duplicate=peers[i].get_non_const();
		  return;
	       }
	    }
	 }
      }
      if(msg_ext_metadata && parent->md_download)
	 SendMetadataRequest();
   } else if(pp->code==MSG_EXT_METADATA) {
      BeNode *msg_type=pp->data->lookup("msg_type",BeNode::BE_INT);
      if(!msg_type) {
	 SetError("ut_metadata msg_type bad or missing");
	 return;
      }
      BeNode *piece=pp->data->lookup("piece",BeNode::BE_INT);
      if(!piece || piece->num<0 || piece->num>=INT_MAX/Torrent::BLOCK_SIZE) {
	 SetError("ut_metadata piece bad or missing");
	 return;
      }
      size_t offset=piece->num*Torrent::BLOCK_SIZE;
      xmap_p<BeNode> reply;
      switch(msg_type->num) {
	 case UT_METADATA_REQUEST: {
	    if(offset>parent->metadata.length()) {
	       reply.add("msg_type",new BeNode(UT_METADATA_REJECT));
	       reply.add("piece",new BeNode(piece->num));
	       PacketExtended pkt(msg_ext_metadata,new BeNode(&reply));
	       LogSend(4,xstring::format("ut_metadata reject %s",pkt.data->Format1()));
	       pkt.Pack(send_buf);
	       break;
	    }
	    const char *d=parent->metadata+offset;
	    unsigned len=parent->metadata.length()-offset;
	    if(len>Torrent::BLOCK_SIZE)
	       len=Torrent::BLOCK_SIZE;
	    reply.add("msg_type",new BeNode(UT_METADATA_DATA));
	    reply.add("piece",new BeNode(piece->num));
	    reply.add("total_size",new BeNode(parent->metadata.length()));
	    PacketExtended pkt(msg_ext_metadata,new BeNode(&reply));
	    LogSend(4,xstring::format("ut_metadata data %s",pkt.data->Format1()));
	    pkt.SetAppendix(d,len);
	    pkt.Pack(send_buf);
	    break;
	 }
	 case UT_METADATA_DATA: {
	    if(parent->md_download) {
	       if(offset==parent->md_download.length()) {
		  BeNode *b_size=pp->data->lookup("total_size",BeNode::BE_INT);
		  if(b_size) {
		     if(metadata_size && metadata_size!=(size_t)b_size->num) {
			SetError("metadata_size mismatch with total_size");
			return;
		     }
		     metadata_size=b_size->num;
		     parent->metadata_size=metadata_size;
		  }
		  parent->md_download.append(pp->appendix);
		  if(pp->appendix.length()<Torrent::BLOCK_SIZE)
		     parent->MetadataDownloaded();
	       }
	       SendMetadataRequest();
	    }
	    break;
	 }
	 case UT_METADATA_REJECT:
	    break;
	 default:
	    SetError("ut_metadata msg_type invalid value");
	    return;
      }
   } else if(pp->code==MSG_EXT_PEX) {
      if(!pex.recv_timer.Stopped())
	 return;
      pex.recv_timer.Reset();
      BeNode *added=pp->data->lookup("added",BeNode::BE_STR);
      BeNode *added6=pp->data->lookup("added6",BeNode::BE_STR);
      BeNode *added_f=pp->data->lookup("added.f",BeNode::BE_STR);
      BeNode *added6_f=pp->data->lookup("added6.f",BeNode::BE_STR);
      AddPEXPeers(added,added_f,6);
      AddPEXPeers(added6,added6_f,18);
   }
}
void TorrentPeer::AddPEXPeers(BeNode *added,BeNode *added_f,int addr_size)
{
   if(!added)
      return;

   const char *data=added->str;
   unsigned n=added->str.length()/addr_size;
   if(n>50)
      n=50;

   const char *flags=0;
   if(added_f && added_f->str.length()==n)
      flags=added_f->str;

   int peers_count=0;
   for(unsigned i=0; i<n; data+=addr_size, i++) {
      unsigned char f=(flags?flags[i]:pex.CONNECTABLE);
      if(!(f&pex.CONNECTABLE))
	 continue;
      if(parent->Complete() && (f&pex.SEED))
	 continue;
      sockaddr_u a;
      a.set_compact(data,addr_size);
      if(!a.is_compatible(this->addr))
	 continue;
      parent->AddPeer(new TorrentPeer(parent,&a,TR_PEX));
      peers_count++;
   }
   if(peers_count>0)
      LogNote(4,"%d %s peers added from PEX message",peers_count,addr_size==6?"ipv4":"ipv6");
}
void TorrentPeer::SendPEXPeers()
{
   pex.send_timer.Reset();
   if(!msg_ext_pex || parent->Private())
      return;
   xmap<char> old_sent;
   old_sent.move_here(pex.sent);
   int peer_count=0;
   xstring added;
   xstring added6;
   xstring added_f;
   xstring added6_f;
   xstring dropped;
   xstring dropped6;
   int a=0,a6=0,d=0,d6=0;
   for(int i=parent->peers.count(); i>0; i--) {
      const TorrentPeer *peer=parent->peers[i-1];
      if(!peer->Connected() || peer->IsPassive() || peer->Failed()
      || !peer->addr.is_compatible(this->addr) || peer==this || peer->myself)
	 continue;
      const xstring& ca=peer->addr.compact();
      if(old_sent.exists(ca)) {
	 old_sent.remove(ca);
	 continue;
      }
      unsigned char f=pex.CONNECTABLE;
      if(peer->Seed())
	 f|=pex.SEED;
      peer_count++;
      if(peer_count>50)
	 continue;
      if(ca.length()==6) {
	 added.append(ca);
	 added_f.append(f);
	 a++;
      } else {
	 added6.append(ca);
	 added6_f.append(f);
	 a6++;
      }
      pex.sent.add(ca,f);
   }
   peer_count=0;
   for(old_sent.each_begin(); !old_sent.each_finished(); old_sent.each_next())
   {
      const xstring& ca=old_sent.each_key();
      peer_count++;
      if(peer_count>50) {
	 // drop it later
	 pex.sent.add(ca,0);
	 continue;
      }
      if(ca.length()==6) {
	 dropped.append(ca);
	 d++;
      } else {
	 dropped6.append(ca);
	 d6++;
      }
   }
   if(a+a6+d+d6==0)
      return;
   xmap_p<BeNode> req;
   if(a) {
      req.add("added",new BeNode(added));
      req.add("added.f",new BeNode(added_f));
   }
   if(a6) {
      req.add("added6",new BeNode(added6));
      req.add("added6.f",new BeNode(added6_f));
   }
   if(d)
      req.add("dropped",new BeNode(dropped));
   if(d6)
      req.add("dropped6",new BeNode(dropped6));
   PacketExtended pkt(msg_ext_pex,new BeNode(&req));
   LogSend(4,xstring::format("ut_pex message: added=[%d,%d], dropped=[%d,%d]",a,a6,d,d6));
   pkt.Pack(send_buf);
}

bool TorrentPeer::HasNeededPieces()
{
   if(!peer_bitfield)
      return false;
   if(GetLastPiece()!=NO_PIECE)
      return true;
   for(int i=0; i<parent->pieces_needed.count(); i++)
      if(peer_bitfield->get_bit(parent->pieces_needed[i]))
	 return true;
   return false;
}

int TorrentPeer::Do()
{
   int m=STALL;
   if(error || myself)
      return m;
   if(sock==-1) {
      if(passive)
	 return m;
      if(!retry_timer.Stopped())
	 return m;
      if(parent->IsValidating())
	 return m;
      sock=SocketCreateTCP(addr.sa.sa_family,0);
      if(sock==-1)
      {
	 if(NonFatalError(errno))
	    return m;
	 SetError(xstring::format(_("cannot create socket of address family %d"),addr.sa.sa_family));
	 return MOVED;
      }
      LogNote(4,_("Connecting to peer %s port %u"),SocketNumericAddress(&addr),SocketPort(&addr));
      connected=false;
   }
   if(!connected) {
      int res=SocketConnect(sock,&addr);
      if(res==-1 && errno!=EINPROGRESS && errno!=EALREADY && errno!=EISCONN)
      {
	 int e=errno;
	 const char *error=strerror(e);
	 LogError(4,"connect(%s): %s\n",GetName(),error);
	 Disconnect(error);
	 if(FA::NotSerious(e) && !ActivityTimedOut())
	    return MOVED;
	 SetError(error);
	 return MOVED;
      }
      if(res==-1 && errno!=EISCONN) {
	 Block(sock,POLLOUT);
	 return m;
      }
      connected=true;
      timeout_timer.Reset();
      m=MOVED;
   }
   if(!recv_buf) {
      recv_buf=new IOBufferFDStream(new FDStream(sock,"<input-socket>"),IOBuffer::GET);
   }
   if(!send_buf) {
      send_buf=new IOBufferFDStream(new FDStream(sock,"<output-socket>"),IOBuffer::PUT);
      SendHandshake();
   }
   if(send_buf->Error())
   {
      LogError(2,"send: %s",send_buf->ErrorText());
      Disconnect(send_buf->ErrorText());
      return MOVED;
   }
   if(recv_buf->Error())
   {
      LogError(2,"receive: %s",recv_buf->ErrorText());
      Disconnect(recv_buf->ErrorText());
      return MOVED;
   }
   if(!peer_id) {
      // expect handshake
      unpack_status_t s=RecvHandshake();
      if(s==UNPACK_NO_DATA_YET)
	 return m;
      if(s!=UNPACK_SUCCESS) {
	 if(s==UNPACK_PREMATURE_EOF) {
	    if(recv_buf->Size()>0) {
	       LogError(2,_("peer unexpectedly closed connection after %s"),recv_buf->Dump());
	       Disconnect(_("peer unexpectedly closed connection"));
	    } else {
	       LogError(4,_("peer closed connection (before handshake)"));
	       Disconnect(_("peer closed connection (before handshake)"));
	    }
	 } else {
	    Disconnect(_("invalid peer response format"));
	 }
	 return MOVED;
      }
      if(!parent->HasMetadata() && !LTEPExtensionEnabled()) {
	 Disconnect("peer cannot provide metadata");
	 return MOVED;
      }
      timeout_timer.Reset();
      myself=peer_id.eq(Torrent::my_peer_id);
      if(myself)
	 return MOVED;
      SendExtensions();
      if(parent->HasMetadata())
	 peer_bitfield=new BitField(parent->total_pieces);
      if(FastExtensionEnabled()) {
	 if(parent->complete_pieces==0) {
	    LogSend(5,"have-none");
	    Packet(MSG_HAVE_NONE).Pack(send_buf);
	 } else if(parent->complete_pieces==parent->total_pieces) {
	    LogSend(5,"have-all");
	    Packet(MSG_HAVE_ALL).Pack(send_buf);
	 } else {
	    LogSend(5,"bitfield");
	    PacketBitField(parent->my_bitfield).Pack(send_buf);
	 }
      } else if(parent->my_bitfield && parent->my_bitfield->has_any_set()) {
	 LogSend(5,"bitfield");
	 PacketBitField(parent->my_bitfield).Pack(send_buf);
      }
      if(Torrent::listener_udp && DHT_Enabled()) {
	 int udp_port=Torrent::listener_udp->GetPort();
#if INET6
	 if(Torrent::listener_ipv6_udp && addr.sa.sa_family==AF_INET6)
	    udp_port=Torrent::listener_ipv6_udp->GetPort();
#endif
	 if(udp_port) {
	    LogSend(5,xstring::format("port(%d)",udp_port));
	    PacketPort(udp_port).Pack(send_buf);
	 }
      }
      keepalive_timer.Reset();
   }

   if(keepalive_timer.Stopped()) {
      LogSend(5,"keep-alive");
      Packet(MSG_KEEPALIVE).Pack(send_buf);
      keepalive_timer.Reset();
   }

   if(send_buf->Size()>(int)Torrent::BLOCK_SIZE*4)
      recv_buf->Suspend();
   else
      recv_buf->Resume();

   if(recv_buf->IsSuspended())
      return m;

   timeout_timer.Reset(send_buf->EventTime());
   timeout_timer.Reset(recv_buf->EventTime());
   if(timeout_timer.Stopped()) {
      LogError(0,_("Timeout - reconnecting"));
      Disconnect("timed out");
      return MOVED;
   }

   if(!am_interested && interest_timer.Stopped()
   && HasNeededPieces() && parent->NeedMoreUploaders())
      SetAmInterested(true);

   if(am_interested && sent_queue.count()<MAX_QUEUE_LEN)
      SendDataRequests();

   if(peer_interested && am_choking && choke_timer.Stopped()
   && parent->AllowMoreDownloaders())
      SetAmChoking(false);

   if(recv_queue.count()>0 && send_buf->Size()<(int)Torrent::BLOCK_SIZE*2) {
      unsigned bytes_allowed=BytesAllowed(RateLimit::PUT);
      while(bytes_allowed>=recv_queue[0]->req_length) {
	 bytes_allowed-=recv_queue[0]->req_length;
	 SendDataReply();
	 m=MOVED;
	 if(!Connected())
	    return m;
	 if(recv_queue.count()==0)
	    break;
	 if(send_buf->Size()>=(int)Torrent::BLOCK_SIZE)
	    m|=send_buf->Do();
	 if(send_buf->Size()>=(int)Torrent::BLOCK_SIZE*2)
	    break;
      }
   }

   if(recv_buf->Eof() && recv_buf->Size()==0) {
      LogError(4,_("peer closed connection"));
      Disconnect(_("peer closed connection"));
      return MOVED;
   }

   if(pex.send_timer.Stopped())
      SendPEXPeers();

   Packet *reply=0;
   unpack_status_t st=UnpackPacket(recv_buf,&reply);
   if(st==UNPACK_NO_DATA_YET)
      return m;
   if(st!=UNPACK_SUCCESS)
   {
      if(st==UNPACK_PREMATURE_EOF) {
	 LogError(2,_("peer unexpectedly closed connection after %s"),recv_buf->Dump());
	 Disconnect(_("peer unexpectedly closed connection"));
      } else {
	 LogError(2,_("invalid peer response format"));
	 Disconnect(_("invalid peer response format"));
      }
      return MOVED;
   }
   reply->DropData(recv_buf);
   HandlePacket(reply);
   return MOVED;
}

TorrentPeer::unpack_status_t TorrentPeer::UnpackPacket(SMTaskRef<IOBuffer>& b,TorrentPeer::Packet **p)
{
   Packet *&pp=*p;
   pp=0;

   Ref<Packet> probe(new Packet);
   unpack_status_t res=probe->Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;

   LogRecvF(11,"got a packet, length=%d, type=%d(%s)\n",
      probe->GetLength(),probe->GetPacketType(),probe->GetPacketTypeText());

   switch(probe->GetPacketType())
   {
   case MSG_KEEPALIVE:
   case MSG_CHOKE:
   case MSG_UNCHOKE:
   case MSG_INTERESTED:
   case MSG_UNINTERESTED:
   case MSG_HAVE_ALL:
   case MSG_HAVE_NONE:
      pp=probe.borrow();
      break;
   case MSG_HAVE:
      pp=new PacketHave();
      break;
   case MSG_BITFIELD:
      pp=new PacketBitField();
      break;
   case MSG_REQUEST:
      pp=new PacketRequest();
      break;
   case MSG_PIECE:
      pp=new PacketPiece();
      break;
   case MSG_CANCEL:
      pp=new PacketCancel();
      break;
   case MSG_PORT:
      pp=new PacketPort();
      break;
   case MSG_SUGGEST_PIECE:
      pp=new PacketSuggestPiece();
      break;
   case MSG_ALLOWED_FAST:
      pp=new PacketAllowedFast();
      break;
   case MSG_REJECT_REQUEST:
      pp=new PacketRejectRequest();
      break;
   case MSG_EXTENDED:
      pp=new PacketExtended();
      break;
   }
   if(probe)
      res=pp->Unpack(b);
   if(res!=UNPACK_SUCCESS)
   {
      switch(res)
      {
      case UNPACK_PREMATURE_EOF:
	 LogError(0,"premature eof");
	 break;
      case UNPACK_WRONG_FORMAT:
	 LogError(0,"wrong packet format");
	 break;
      case UNPACK_NO_DATA_YET:
      case UNPACK_SUCCESS:
	 ;
      }
      if(probe)
	 probe->DropData(b);
      else
	 pp->DropData(b);
      delete pp;
      pp=0;
   }
   return res;
}

const char *TorrentPeer::Packet::GetPacketTypeText() const
{
   const char *const text_table[]={
      "keep-alive", "choke", "unchoke", "interested", "uninterested",
      "have", "bitfield", "request", "piece", "cancel", "port",
      "10", "11", "12",
      "suggest-piece", "have-all", "have-none", "reject-request", "allowed-fast",
      "18", "19",
      "extended",
   };
   return text_table[type+1];
}

TorrentPeer::unpack_status_t TorrentPeer::Packet::Unpack(const Buffer *b)
{
   unpacked=0;
   if(b->Size()<4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   length=b->UnpackUINT32BE(0);
   unpacked+=4;
   if(length==0) {
      type=MSG_KEEPALIVE;
      return UNPACK_SUCCESS;
   }
   if(length<0 || length>1024*1024) {
      LogError(4,"invalid length %d",length);
      return UNPACK_WRONG_FORMAT;
   }
   if(b->Size()<length+4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   int t=b->UnpackUINT8(4);
   unpacked++;
   if(!is_valid_reply(t)) {
      LogError(4,"unknown packet type %d, length %d",t,length);
      return UNPACK_WRONG_FORMAT;
   }
   type=(packet_type)t;
   return UNPACK_SUCCESS;
}

bool TorrentPeer::AddressEq(const TorrentPeer *o) const
{
   return !memcmp(&addr,&o->addr,sizeof(addr));
}

const char *TorrentPeer::GetName() const
{
   xstring& name=xstring::format("[%s]:%d",addr.address(),addr.port());
   if(tracker_no==TR_ACCEPTED)
      name.append("/A");
   else if(tracker_no==TR_DHT)
      name.append("/D");
   else if(tracker_no==TR_PEX)
      name.append("/X");
   else if(parent->trackers.count()>1)
      name.appendf("/%d",tracker_no+1);
   return name;
}

const char *TorrentPeer::Status()
{
   if(sock==-1) {
      if(last_dc)
	 return xstring::format("Disconnected (%s)",last_dc.get());
      return _("Not connected");
   }
   if(!connected)
      return _("Connecting...");
   if(!peer_id)
      return _("Handshaking...");
   xstring &buf=xstring::format("dn:%s %sup:%s %s",
      xhuman(peer_recv),peer_recv_rate.GetStrS(),
      xhuman(peer_sent),peer_send_rate.GetStrS());
   if(peer_interested)
      buf.append("peer-interested ");
   if(peer_choking)
      buf.append("peer-choking ");
   if(am_interested)
      buf.append("am-interested ");
   if(am_choking)
      buf.append("am-choking ");
   if(parent->HasMetadata()) {
      if(peer_complete_pieces<parent->total_pieces)
	 buf.appendf("complete:%u/%u (%u%%)",peer_complete_pieces,parent->total_pieces,
	    peer_complete_pieces*100/parent->total_pieces);
      else
	 buf.append("complete");
   }
   return buf;
}

TorrentPeer::Packet::Packet(packet_type t)
{
   type=t;
   length=0;
   if(type>=0)
      length+=1;
}
void TorrentPeer::Packet::Pack(SMTaskRef<IOBuffer>& b)
{
   b->PackUINT32BE(length);
   if(type>=0)
      b->PackUINT8(type);
}

TorrentPeer::PacketBitField::PacketBitField(const BitField *bf)
   : Packet(MSG_BITFIELD)
{
   bitfield=new BitField();
   bitfield->set(*bf);
   length+=bitfield->count();
}
TorrentPeer::PacketBitField::~PacketBitField()
{
}
TorrentPeer::unpack_status_t TorrentPeer::PacketBitField::Unpack(const Buffer *b)
{
   unpack_status_t res;
   res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   int bytes=length+4-unpacked;
   bitfield=new BitField(bytes*8);
   memcpy(bitfield->get_non_const(),b->Get()+unpacked,bytes);
   unpacked+=bytes;
   return UNPACK_SUCCESS;
}
void TorrentPeer::PacketBitField::ComputeLength()
{
   Packet::ComputeLength();
   length+=bitfield->count();
}
void TorrentPeer::PacketBitField::Pack(SMTaskRef<IOBuffer>& b)
{
   Packet::Pack(b);
   b->Put((const char*)(bitfield->get()),bitfield->count());
}

TorrentPeer::_PacketIBL::_PacketIBL(packet_type t,unsigned i,unsigned b,unsigned l)
   : Packet(t), index(i), begin(b), req_length(l)
{
   length+=12;
}
TorrentPeer::unpack_status_t TorrentPeer::_PacketIBL::Unpack(const Buffer *b)
{
   unpack_status_t res;
   res=Packet::Unpack(b);
   if(res!=UNPACK_SUCCESS)
      return res;
   index=b->UnpackUINT32BE(unpacked);unpacked+=4;
   begin=b->UnpackUINT32BE(unpacked);unpacked+=4;
   req_length=b->UnpackUINT32BE(unpacked);unpacked+=4;
   return UNPACK_SUCCESS;
}
void TorrentPeer::_PacketIBL::ComputeLength()
{
   Packet::ComputeLength();
   length+=12;
}
void TorrentPeer::_PacketIBL::Pack(SMTaskRef<IOBuffer>& b)
{
   Packet::Pack(b);
   b->PackUINT32BE(index);
   b->PackUINT32BE(begin);
   b->PackUINT32BE(req_length);
}
TorrentPeer::unpack_status_t TorrentPeer::Packet::UnpackBencoded(const Buffer *b,int *offset,int limit,Ref<BeNode> *out)
{
   assert(limit<=b->Size());
   int rest=limit-*offset;
   int rest0=rest;
   *out=BeNode::Parse(b->Get()+*offset,rest,&rest);
   if(!*out) {
      if(rest>0)
	 return UNPACK_WRONG_FORMAT;
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   }
   *offset+=(rest0-rest);
   return UNPACK_SUCCESS;
}


BitField::BitField(int bits) {
   bit_length=bits;
   int bytes=(bits+7)/8;
   get_space(bytes);
   memset(buf,0,bytes);
   set_length(bytes);
}
bool BitField::get_bit(int i) const {
   return (*this)[i/8]&(0x80>>(i%8));
}
void BitField::set_bit(int i,bool value) {
   unsigned char &b=(*this)[i/8];
   int v=(0x80>>(i%8));
   if(value)
      b|=v;
   else
      b&=~v;
}
bool BitField::has_any_set(int from,int to) const {
   for(int i=from; i<to; i++)
      if(get_bit(i))
	 return true;
   return false;
}
bool BitField::has_all_set(int from,int to) const {
   for(int i=from; i<to; i++)
      if(!get_bit(i))
	 return false;
   return true;
}
void BitField::set_range(int from,int to,bool value) {
   for(int i=from; i<to; i++)
      set_bit(i,value);
}

void TorrentBlackList::check_expire()
{
   for(Timer *e=bl.each_begin(); e; e=bl.each_next()) {
      if(e->Stopped()) {
	 LogNote(4,"black-delisting peer %s\n",bl.each_key().get());
	 bl.remove(bl.each_key());
      }
   }
}
void TorrentBlackList::Add(const sockaddr_u &a,const char *t)
{
   check_expire();
   if(Listed(a))
      return;
   LogNote(4,"black-listing peer %s (%s)\n",(const char*)a,t);
   bl.add(a.to_xstring(),new Timer(TimeIntervalR(t)));
}
bool TorrentBlackList::Listed(const sockaddr_u &a)
{
   return bl.lookup(a.to_xstring())!=0;
}


TorrentListener::TorrentListener(int a,int t)
   : af(a), type(t), sock(-1), last_sent_udp_count(0)
{
}
TorrentListener::~TorrentListener()
{
   if(sock!=-1)
      close(sock);
}
void TorrentListener::FillAddress(int port)
{
   addr.set_defaults(af,"torrent",port);
}

bool Torrent::NoTorrentCanAccept()
{
   for(const Torrent *t=torrents.each_begin(); t; t=torrents.each_next()) {
      if(t->CanAccept())
	 return false;
   }
   return true;
}

int TorrentListener::Do()
{
   int m=STALL;
   if(error)
      return m;
   if(sock==-1) {
      int proto=(type==SOCK_STREAM?IPPROTO_TCP:IPPROTO_UDP);
      sock=SocketCreateUnbound(af,type,proto,0);
      if(sock==-1) {
	 if(NonFatalError(errno))
	    return m;
	 error=Error::Fatal(_("cannot create socket of address family %d"),addr.sa.sa_family);
	 return MOVED;
      }
      SocketSinglePF(sock,af);

      // Try to assign a port from given range
      Range range(ResMgr::Query("torrent:port-range",0));

      // but first try already allocated port
      int prefer_port=Torrent::GetPort();
      if(prefer_port) {
	 ReuseAddress(sock);   // try to reuse address.
	 FillAddress(prefer_port);
	 if(addr.bind_to(sock)==0)
	    goto bound;
	 LogError(1,"bind(%s): %s",addr.to_string(),strerror(errno));
      }

      for(int t=0; ; t++)
      {
	 if(t>=10)
	 {
	    close(sock);
	    sock=-1;
	    TimeoutS(1);	 // retry later.
	    return m;
	 }
	 if(t==9)
	    ReuseAddress(sock);   // try to reuse address.

	 int port=0;
	 if(!range.IsFull())
	    port=range.Random();
	 if(!port && type==SOCK_DGRAM)
	    port=Range("1024-65535").Random();

	 if(!port)
	     break;	// nothing to bind

	 FillAddress(port);
	 if(addr.bind_to(sock)==0)
	    break;
	 int saved_errno=errno;

	 // Fail unless socket was already taken
	 if(errno!=EINVAL && errno!=EADDRINUSE)
	 {
	    LogError(0,"bind(%s): %s",addr.to_string(),strerror(saved_errno));
	    close(sock);
	    sock=-1;
	    if(NonFatalError(errno))
	    {
	       TimeoutS(1);
	       return m;
	    }
	    error=Error::Fatal(_("Cannot bind a socket for torrent:port-range"));
	    return MOVED;
	 }
	 LogError(10,"bind(%s): %s",addr.to_string(),strerror(saved_errno));
      }
   bound:
      if(type==SOCK_STREAM)
	 if(listen(sock,5) < 0)
             LogError(0,"listen failed: %s", strerror(errno));

      // get the allocated port
      socklen_t addr_len=sizeof(addr);
      getsockname(sock,&addr.sa,&addr_len);
      LogNote(4,"listening on %s %s",type==SOCK_STREAM?"tcp":"udp",addr.to_string());
      m=MOVED;

      if(type==SOCK_DGRAM && Torrent::dht)
	 Torrent::GetDHT(af)->Load();
   }

   if(type==SOCK_DGRAM) {
      if(!Ready(sock,POLLIN)) {
	 Block(sock,POLLIN);
	 return m;
      }
      char buf[0x4000];
      sockaddr_u src;
      socklen_t src_len=sizeof(src);
      int res=recvfrom(sock,buf,sizeof(buf),0,&src.sa,&src_len);
      if(res==-1) {
	 if(!E_RETRY(errno))
	    LogError(9,"recvfrom: %s",strerror(errno));
	 Block(sock,POLLIN);
	 return m;
      }
      if(res==0)
	 return MOVED;
      rate.Add(1);
      Torrent::DispatchUDP(buf,res,src);
      return MOVED;
   }

   if(rate.Get()>5 || Torrent::NoTorrentCanAccept())
   {
      TimeoutS(1);
      return m;
   }

   if(!Ready(sock,POLLIN)) {
      Block(sock,POLLIN);
      return m;
   }

   sockaddr_u remote_addr;
   int a=SocketAccept(sock,&remote_addr);
   if(a==-1) {
      Block(sock,POLLIN);
      return m;
   }
   rate.Add(1);
   LogNote(3,_("Accepted connection from [%s]:%d"),remote_addr.address(),remote_addr.port());
   (void)new TorrentDispatcher(a,&remote_addr);
   m=MOVED;

   return m;
}
bool TorrentListener::MaySendUDP()
{
   // limit udp rate
   if(last_sent_udp_count>=10 && now==last_sent_udp)
      UpdateNow();
   TimeDiff time_passed(now,last_sent_udp);
   if(time_passed.MilliSeconds()<1) {
      if(last_sent_udp_count>=10) {
	 Timeout(1);
	 return false;
      }
      last_sent_udp_count++;
   } else {
      last_sent_udp_count=0;
      last_sent_udp=now;
   }
   if (sock==-1)
      return false;
   // check if output buffer is available
   struct pollfd pfd;
   pfd.fd=sock;
   pfd.events=POLLOUT;
   pfd.revents=0;
   int res=poll(&pfd,1,0);
   if(res>0)
      return true;
   Block(sock,POLLOUT);
   return false;
}
int TorrentListener::SendUDP(const sockaddr_u& a,const xstring& buf)
{
   int res=sendto(sock,buf,buf.length(),0,&a.sa,a.addr_len());
   if(res==-1)
      LogError(0,"sendto(%s): %s",a.to_string(),strerror(errno));
   return res;
}

void Torrent::DispatchUDP(const char *buf,int len,const sockaddr_u& src)
{
   int rest;
   if(buf[0]=='d' && buf[len-1]=='e' && dht) {
      Ref<BeNode> msg(BeNode::Parse(buf,len,&rest));
      if(!msg)
	 goto unknown;
      const SMTaskRef<DHT> &d=Torrent::GetDHT(src);
      d->Enter();
      d->HandlePacket(msg.get_non_const(),src);
      d->Leave();
   } else if(buf[0]==0x41) {
      LogRecv(9,xstring::format("uTP SYN v1 from %s {%s}",src.to_string(),xstring::get_tmp(buf,len).hexdump()));
   } else {
   unknown:
      LogRecv(4,xstring::format("udp from %s {%s}",src.to_string(),xstring::get_tmp(buf,len).hexdump()));
   }
}

void Torrent::Dispatch(const xstring& info_hash,int sock,const sockaddr_u *remote_addr,IOBuffer *recv_buf)
{
   Torrent *t=FindTorrent(info_hash);
   if(!t) {
      LogError(3,_("peer sent unknown info_hash=%s in handshake"),info_hash.hexdump());
      close(sock);
      Delete(recv_buf);
      return;
   }
   t->Accept(sock,remote_addr,recv_buf);
}

TorrentDispatcher::TorrentDispatcher(int s,const sockaddr_u *a)
   : sock(s), addr(*a),
     recv_buf(new IOBufferFDStream(new FDStream(sock,"<input-socket>"),IOBuffer::GET)),
     timeout_timer(60),
     peer_name(addr.to_xstring())
{
}
TorrentDispatcher::~TorrentDispatcher()
{
   if(sock!=-1)
      close(sock);
}
int TorrentDispatcher::Do()
{
   if(timeout_timer.Stopped())
   {
      LogError(1,_("peer handshake timeout"));
      Delete(this);
      return MOVED;
   }

   unsigned proto_len=0;
   if(recv_buf->Size()>0)
      proto_len=recv_buf->UnpackUINT8();

   if((unsigned)recv_buf->Size()<1+proto_len+8+SHA1_DIGEST_SIZE) {
      if(recv_buf->Eof()) {
	 if(recv_buf->Size()>0)
	    LogError(1,_("peer short handshake"));
	 else
	    LogError(4,_("peer closed just accepted connection"));
	 Delete(this);
	 return MOVED;
      }
      return STALL;
   }

   int unpacked=1;
   const char *data=recv_buf->Get();

   unpacked+=proto_len;
   unpacked+=8; // 8 bytes are reserved

   xstring peer_info_hash(data+unpacked,SHA1_DIGEST_SIZE);
   unpacked+=SHA1_DIGEST_SIZE;

   Torrent::Dispatch(peer_info_hash,sock,&addr,recv_buf.borrow());
   sock=-1;
   Delete(this);
   return MOVED;
}

///
TorrentJob::TorrentJob(Torrent *t)
   : torrent(t), completed(false), done(false)
{
}
TorrentJob::~TorrentJob()
{
}
void TorrentJob::PrepareToDie()
{
   done=true;
   torrent=0;
   Job::PrepareToDie();
}

int TorrentJob::Do()
{
   if(done)
      return STALL;
   if(torrent->Done()) {
      done=true;
      const Error *e=torrent->GetInvalidCause();
      if(e)
	 eprintf("%s\n",e->Text());
      return MOVED;
   }
   if(!completed && torrent->Complete()) {
      if(parent->WaitsFor(this) && !torrent->IsSharing()) {
	 PrintStatus(1,"");
	 printf(_("Seeding in background...\n"));
	 parent->RemoveWaiting(this);
      }
      completed=true;
      return MOVED;
   }
   return STALL;
}

xstring& TorrentJob::FormatStatus(xstring& s,int v,const char *tab)
{
   if(torrent->IsDownloading())
      torrent->CalcPiecesStats();
   const char *name=torrent->GetName();
   if(name)
      s.appendf("%sName: %s\n",tab,name);
   const char *status=torrent->Status();
   if(*status)
      s.appendf("%s%s\n",tab,status);
   if(torrent->IsDownloading()) {
      s.appendf("%spiece availability: min %u, avg %.2f, %d%% available\n",tab,
	 torrent->MinPieceSources(),torrent->AvgPieceSources(),torrent->PiecesAvailablePct());
      if(torrent->GetRatio()>0) {
	 s.appendf("%sratio: %.2f/%.2f/%.2f\n",tab,
	    torrent->GetMinPerPieceRatio(),torrent->GetRatio(),
	    torrent->GetMaxPerPieceRatio());
      }
   }

   if(v>2) {
      s.appendf("%sinfo hash: %s\n",tab,torrent->GetInfoHash().hexdump());
      if(torrent->HasMetadata()) {
	 s.appendf("%stotal length: %llu\n",tab,torrent->TotalLength());
	 s.appendf("%spiece length: %u\n",tab,torrent->PieceLength());
      }
   }

   if(v>1) {
      if(torrent->Trackers().count()==1) {
	 s.appendf("%stracker: %s - %s\n",tab,torrent->Trackers()[0]->GetURL(),
	       torrent->Trackers()[0]->Status());
      } else if(torrent->Trackers().count()>1) {
	 s.appendf("%strackers:\n",tab);
	 for(int i=0; i<torrent->Trackers().count(); i++) {
	    s.appendf("%s%2d. %s - %s\n",tab,i+1,torrent->Trackers()[i]->GetURL(),
		  torrent->Trackers()[i]->Status());
	 }
      }
      const char *dht_status=torrent->DHT_Status();
      if(*dht_status)
	 s.appendf("%sDHT: %s\n",tab,dht_status);
   }

   if(torrent->ShuttingDown())
      return s;

   if(torrent->GetPeersCount()<=5 || v>1) {
      const TaskRefArray<TorrentPeer>& peers=torrent->GetPeers();
      int not_connected_peers=torrent->GetPeersCount()-torrent->GetConnectedPeersCount();
      if(v<=2 && not_connected_peers>0)
	 s.appendf("%s  not connected peers: %d\n",tab,not_connected_peers);
      for(int i=0; i<peers.count(); i++) {
	 if(peers[i]->Connected() || v>2)
	    s.appendf("%s  %s: %s\n",tab,peers[i]->GetName(),peers[i]->Status());
      }
   } else {
      s.appendf("%s  peers:%d connected:%d active:%d complete:%d\n",tab,
	 torrent->GetPeersCount(),torrent->GetConnectedPeersCount(),
	 torrent->GetActivePeersCount(),torrent->GetCompletePeersCount());
   }
   return s;
}

void TorrentJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   const xstring& status=torrent->Status();
   const char *name=torrent->GetName();
   int w=s->GetWidthDelayed()-status.length()-3;
   if(w<8)  w=8;
   if(w>40) w=40;
   s->Show("%s: %s",squeeze_file_name(name,w),status.get());
}

int TorrentJob::AcceptSig(int sig)
{
   if(!torrent || torrent->ShuttingDown())
      return WANTDIE;
   torrent->Shutdown();
   return MOVED;
}


#include "CmdExec.h"
CDECL_BEGIN
#include <glob.h>
CDECL_END

CMD(torrent)
{
   Torrent::ClassInit();

#define args (parent->args)
#define eprintf parent->eprintf
   enum {
      OPT_OUTPUT_DIRECTORY,
      OPT_FORCE_VALID,
      OPT_DHT_BOOTSTRAP,
      OPT_SHARE,
      OPT_ONLY_NEW,
      OPT_ONLY_INCOMPLETE,
   };
   static const struct option torrent_opts[]=
   {
      {"output-directory",required_argument,0,OPT_OUTPUT_DIRECTORY},
      {"force-valid",no_argument,0,OPT_FORCE_VALID},
      {"dht-bootstrap",required_argument,0,OPT_DHT_BOOTSTRAP},
      {"share",no_argument,0,OPT_SHARE},
      {"only-new",no_argument,0,OPT_ONLY_NEW},
      {"only-incomplete",no_argument,0,OPT_ONLY_INCOMPLETE},
      {0}
   };
   const char *output_dir=0;
   const char *dht_bootstrap=0;
   bool force_valid=false;
   bool share=false;
   bool only_new=false;
   bool only_incomplete=false;

   args->rewind();
   int opt;
   while((opt=args->getopt_long("O:",torrent_opts,0))!=EOF)
   {
      switch(opt)
      {
      case(OPT_OUTPUT_DIRECTORY):
      case('O'):
	 output_dir=optarg;
	 break;
      case(OPT_FORCE_VALID):
	 force_valid=true;
	 break;
      case(OPT_DHT_BOOTSTRAP):
	 dht_bootstrap=optarg;
	 Torrent::BootstrapDHT(dht_bootstrap);
	 break;
      case(OPT_SHARE):
	 share=true;
	 break;
      case(OPT_ONLY_NEW):
	 only_new=true;
	 // fallthrough
      case(OPT_ONLY_INCOMPLETE):
	 only_incomplete=true;
	 break;
      case('?'):
      try_help:
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }
   args->back();

   if(share && output_dir) {
      eprintf(_("%s: --share conflicts with --output-directory.\n"),args->a0());
      return 0;
   }
   if(share && only_new) {
      eprintf(_("%s: --share conflicts with --only-new.\n"),args->a0());
      return 0;
   }
   if(share && only_incomplete) {
      eprintf(_("%s: --share conflicts with --only-incomplete.\n"),args->a0());
      return 0;
   }

   xstring_ca torrent_opt(args->Combine(0,args->getindex()+1));

   xstring_ca cwd(xgetcwd());
   if(output_dir) {
      output_dir=dir_file(cwd,expand_home_relative(output_dir));
      output_dir=alloca_strdup(output_dir);
   } else
      output_dir=cwd;

   Ref<ArgV> args_g(new ArgV(args->a0()));
   const char *torrent;
   while((torrent=args->getnext())!=0) {
      int globbed=0;
      if(share || !url::is_url(torrent)) {
	 glob_t pglob;
	 glob(expand_home_relative(torrent),0,0,&pglob);
	 if(pglob.gl_pathc>0) {
	    for(unsigned i=0; i<pglob.gl_pathc; i++) {
	       const char *f=pglob.gl_pathv[i];
	       struct stat st;
	       if(share || (stat(f,&st)!=-1 && S_ISREG(st.st_mode))) {
		  args_g->Add(dir_file(cwd,f));
		  globbed++;
	       }
	    }
	 }
	 globfree(&pglob);
      }
      if(!globbed)
	 args_g->Add(torrent);
   }

   torrent=args_g->getnext();
   if(!torrent)
   {
      if(dht_bootstrap)
	 return 0;
      if(share)
	 eprintf(_("%s: Please specify a file or directory to share.\n"),args->a0());
      else
	 eprintf(_("%s: Please specify meta-info file or URL.\n"),args->a0());
      goto try_help;
   }
   while(torrent) {
      Torrent *t=new Torrent(torrent,cwd,output_dir);
      if(force_valid)
	 t->ForceValid();
      if(share)
	 t->Share();
      if(only_new)
	 t->StopIfKnown();
      if(only_incomplete)
	 t->StopIfComplete();
      TorrentJob *tj=new TorrentJob(t);
      tj->cmdline.set(xstring::cat(torrent_opt," ",torrent,NULL));
      parent->AddNewJob(tj);
      torrent=args_g->getnext();
   }
   return 0;
#undef args
}

#include "modconfig.h"
#ifndef MODULE_CMD_TORRENT
# define module_init cmd_torrent_module_init
#endif
CDECL void module_init()
{
   Torrent::ClassInit();
   CmdExec::RegisterCommand("torrent",cmd_torrent,0,
	 N_("Start BitTorrent job for the given torrent-files, which can be a local file,\n"
	 "URL, magnet link or plain info_hash written in hex or base32. Local wildcards\n"
	 "are expanded. Options:\n"
	 " -O <base>      specifies base directory where files should be placed\n"
	 " --force-valid  skip file validation\n"
	 " --dht-bootstrap=<node>  bootstrap DHT by sending a query to the node\n"
	 " --share        share specified file or directory\n"));
}
