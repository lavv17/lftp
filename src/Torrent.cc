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

/* $Id: Torrent.cc,v 1.54 2011/08/01 09:51:51 lav Exp $ */

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

#include "Torrent.h"
#include "log.h"
#include "url.h"
#include "misc.h"
#include "plural.h"

static ResType torrent_vars[] = {
   {"torrent:port-range", "6881-6889", ResMgr::RangeValidate, ResMgr::NoClosure},
   {"torrent:max-peers", "60", ResMgr::UNumberValidate},
   {"torrent:stop-on-ratio", "2.0", ResMgr::FloatValidate},
   {"torrent:seed-max-time", "30d", ResMgr::TimeIntervalValidate},
   {"torrent:seed-min-peers", "3", ResMgr::UNumberValidate},
   {"torrent:ip", "", ResMgr::IPv4AddrValidate, ResMgr::NoClosure},
   {"torrent:retracker", ""},
   {"torrent:use-dht", "yes", ResMgr::BoolValidate, ResMgr::NoClosure},
#if INET6
   {"torrent:ipv6", "", ResMgr::IPv6AddrValidate, ResMgr::NoClosure},
#endif
   {0}
};
static ResDecls torrent_vars_register(torrent_vars);

#if INET6
#ifdef HAVE_IFADDRS_H
#include <ifaddrs.h>
#endif
static const char *FindGlobalIPv6Address()
{
#ifdef HAVE_IFADDRS_H
   struct ifaddrs *ifaddrs=0;
   getifaddrs(&ifaddrs);
   for(struct ifaddrs *ifa=ifaddrs; ifa; ifa=ifa->ifa_next) {
      if(ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET6) {
	 struct in6_addr *addr=&((struct sockaddr_in6*)ifa->ifa_addr)->sin6_addr;
	 if(!IN6_IS_ADDR_UNSPECIFIED(addr) && !IN6_IS_ADDR_LOOPBACK(addr)
	 && !IN6_IS_ADDR_LINKLOCAL(addr) && !IN6_IS_ADDR_SITELOCAL(addr)
	 && !IN6_IS_ADDR_MULTICAST(addr)) {
	    char *buf=xstring::tmp_buf(INET6_ADDRSTRLEN);
	    inet_ntop(AF_INET6, addr, buf, INET6_ADDRSTRLEN);
	    freeifaddrs(ifaddrs);
	    return buf;
	 }
      }
   }
   freeifaddrs(ifaddrs);
#endif
   return 0;
}
#endif

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
xmap<Torrent*> Torrent::torrents;
SMTaskRef<TorrentListener> Torrent::listener;
SMTaskRef<TorrentListener> Torrent::listener_udp;
SMTaskRef<DHT> Torrent::dht;
#if INET6
SMTaskRef<TorrentListener> Torrent::listener_ipv6;
SMTaskRef<TorrentListener> Torrent::listener_ipv6_udp;
SMTaskRef<DHT> Torrent::dht_ipv6;
#endif
Ref<FDCache> Torrent::fd_cache;
Ref<TorrentBlackList> Torrent::black_list;

void TorrentTracker::AddURL(const char *url)
{
   LogNote(4,"Tracker URL is `%s'",url);
   ParsedURL u(url,true);
   if(u.proto.ne("http") && u.proto.ne("https")) {
      LogError(1,"unsupported tracker protocol `%s', must be http or https",u.proto.get());
      return;
   }
   xstring& tracker_url=*new xstring(url);
   // fix the URL: append either ? or & if missing.
   if(tracker_url.last_char()!='?' && tracker_url.last_char()!='&')
      tracker_url.append(tracker_url.instr('?')>=0?'&':'?');
   tracker_urls.append(&tracker_url);
}

TorrentTracker::TorrentTracker(Torrent *p,const char *url)
   : parent(p), current_tracker(0),
     tracker_timer(600), tracker_timeout_timer(120),
     started(false), tracker_no(0)
{
   AddURL(url);
}

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

   const char *home=get_lftp_home();
   const char *nodename=get_nodename();

   const char *ip=ResMgr::Query("torrent:ip",0);
   if(!ip || !ip[0])
      ip="127.0.0.1";
   xstring ip_packed;
   ip_packed.get_space(4);
   inet_pton(AF_INET,ip,ip_packed.get_non_const());
   ip_packed.set_length(4);
   xstring node_id;
   DHT::MakeNodeId(node_id,ip_packed);
   dht=new DHT(AF_INET,node_id);
   dht->state_file.setf("%s/DHT-%s",home,nodename);
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
   dht_ipv6->state_file.setf("%s/DHT6-%s",home,nodename);
   if(listener_ipv6_udp->GetPort())
      dht_ipv6->Load();
#endif
}
void Torrent::StopDHT()
{
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
   listener->Do(); // try to allocate ipv4 port first
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
     pieces_needed_rebuild_timer(10),
     cwd(c), output_dir(od), rate_limit(mf),
     seed_timer("torrent:seed-max-time",0),
     optimistic_unchoke_timer(30), peers_scan_timer(1),
     am_interested_timer(1), dht_announce_timer(10*60)
{
   shutting_down=false;
   complete=false;
   end_game=false;
   is_private=false;
   validating=false;
   force_valid=false;
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
   last_piece=TorrentPeer::NO_PIECE;
   Reconfig(0);

   if(!fd_cache)
      fd_cache=new FDCache();
   if(!black_list)
      black_list=new TorrentBlackList();

   StartListener();
   StartDHT();

   if(!my_peer_id) {
      my_peer_id.set("-lftp44-");
      my_peer_id.appendf("%04x",(unsigned)getpid() & 0xffff);
      my_peer_id.appendf("%08x",(unsigned)now.UnixTime());
      assert(my_peer_id.length()==PEER_ID_LEN);
   }
   if(!my_key) {
      for(int i=0; i<10; i++)
	 my_key.appendf("%02x",unsigned(random()/13%256));
   }
}

bool Torrent::TrackersDone() const
{
   for(int i=0; i<trackers.count(); i++) {
      if(trackers[i]->tracker_reply)
	 return false;
   }
   return true;
}
int Torrent::Done() const
{
   return (shutting_down && TrackersDone());
}

void TorrentTracker::Shutdown()
{
   if(Failed()) // don't stop a failed tracker
      return;
   // stop if have started or at least processing a start request
   if(started || tracker_reply)
      SendTrackerRequest("stopped");
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
   LogNote(3,"Shutting down...");
   shutting_down=true;
   ShutdownTrackers();
   PrepareToDie();
}

void Torrent::PrepareToDie()
{
   peers.unset();
   RemoveTorrent(this);
   if(GetTorrentsCount()==0) {
      StopListener();
      fd_cache=0;
      black_list=0;
   }
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
   const TorrentPeer*& downloader=piece_info[piece]->downloader[block];
   if(downloader==o)
      downloader=n;
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
   BeNode *b_charset=metainfo_tree->lookup("encoding",BeNode::BE_STR);
   if(b_charset)
      charset=b_charset->str;
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
      valid=!memcmp(pieces->get()+p*SHA1_DIGEST_SIZE,sha1,SHA1_DIGEST_SIZE);
   }
   if(!valid) {
      if(buf.length()==PieceLength(p))
	 LogError(11,"piece %u digest mismatch",p);
      if(my_bitfield->get_bit(p)) {
	 total_left+=PieceLength(p);
	 complete_pieces--;
	 my_bitfield->set_bit(p,0);
      }
      piece_info[p]->block_map.clear();
   } else {
      LogNote(11,"piece %u ok",p);
      if(!my_bitfield->get_bit(p)) {
	 total_left-=PieceLength(p);
	 complete_pieces++;
	 my_bitfield->set_bit(p,1);
      }
   }
}

bool TorrentPiece::has_a_downloader() const
{
   for(int i=0; i<downloader.count(); i++)
      if(downloader[i])
	 return true;
   return false;
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
   int ra=cmp_torrent->piece_info[*a]->sources_count;
   int rb=cmp_torrent->piece_info[*b]->sources_count;
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
   return (stop_on_ratio>0 && GetRatio()>=stop_on_ratio)
      || seed_timer.Stopped();
}

void TorrentTracker::SetError(const char *e)
{
   if(tracker_urls.count()<=1)
      error=new Error(-1,e,true);
   else {
      LogError(3,"Tracker error: %s, using next tracker URL",e);
      tracker_urls.remove(current_tracker--);
      NextTracker();
   }
}
int TorrentTracker::HandleTrackerReply()
{
   if(tracker_reply->Error()) {
      SetError(tracker_reply->ErrorText());
      t_session->Close();
      tracker_reply=0;
      tracker_timer.Reset();
      return MOVED;
   }
   if(!tracker_reply->Eof()) {
      if(tracker_timeout_timer.Stopped()) {
	 t_session->Close();
	 tracker_reply=0;
	 tracker_timer.Reset();
	 LogError(3,"Tracker timeout");
	 NextTracker();
	 return MOVED;
      }
      return STALL;
   }
   t_session->Close();
   int rest;
   Ref<BeNode> reply(BeNode::Parse(tracker_reply->Get(),tracker_reply->Size(),&rest));
   if(!reply) {
      LogError(3,"Tracker reply parse error (data: %s)",tracker_reply->Dump());
      tracker_reply=0;
      tracker_timer.Reset();
      NextTracker();
      return MOVED;
   }
   LogNote(10,"Received tracker reply:");
   Log::global->Write(10,reply->Format());

   if(parent->ShuttingDown()) {
      tracker_reply=0;
      t_session=0;
      return MOVED;
   }
   started=true;

   if(reply->type!=BeNode::BE_DICT) {
      SetError("Reply: wrong reply type, must be DICT");
      tracker_reply=0;
      return MOVED;
   }

   BeNode *b_failure_reason=reply->lookup("failure reason");
   if(b_failure_reason) {
      if(b_failure_reason->type==BeNode::BE_STR)
	 SetError(b_failure_reason->str);
      else
	 SetError("Reply: wrong `failure reason' type, must be STR");
      tracker_reply=0;
      return MOVED;
   }

   BeNode *b_interval=reply->lookup("interval",BeNode::BE_INT);
   if(b_interval) {
      LogNote(4,"Tracker interval is %llu",b_interval->num);
      tracker_timer.Set(b_interval->num);
   }

   if(!tracker_id)
      tracker_id.set(reply->lookup_str("tracker id"));

   int peers_count=0;
   BeNode *b_peers=reply->lookup("peers");
   if(b_peers) {
      if(b_peers->type==BeNode::BE_STR) { // binary model
	 const char *data=b_peers->str;
	 int len=b_peers->str.length();
	 LogNote(9,"peers have binary model, length=%d",len);
	 while(len>=6) {
	    sockaddr_u a;
	    a.set_compact(data,6);
	    data+=6;
	    len-=6;
	    parent->AddPeer(new TorrentPeer(parent,&a,tracker_no));
	    peers_count++;
	 }
      } else if(b_peers->type==BeNode::BE_LIST) { // dictionary model
	 int count=b_peers->list.count();
	 LogNote(9,"peers have dictionary model, count=%d",count);
	 for(int p=0; p<count; p++) {
	    BeNode *b_peer=b_peers->list[p];
	    if(b_peer->type!=BeNode::BE_DICT)
	       continue;
	    BeNode *b_ip=b_peer->lookup("ip",BeNode::BE_STR);
	    if(!b_ip)
	       continue;
	    BeNode *b_port=b_peer->lookup("port",BeNode::BE_INT);
	    if(!b_port)
	       continue;
	    sockaddr_u a;
#if INET6
	    if(b_ip->str.instr(':')>=0) {
	       a.sa.sa_family=AF_INET6;
	       if(inet_pton(AF_INET6,b_ip->str,&a.in6.sin6_addr)<=0)
		  continue;
	       a.set_port(b_port->num);
	    } else
#endif
	    {
	       a.sa.sa_family=AF_INET;
	       if(!inet_aton(b_ip->str,&a.in.sin_addr))
		  continue;
	       a.set_port(b_port->num);
	    }
	    parent->AddPeer(new TorrentPeer(parent,&a,tracker_no));
	    peers_count++;
	 }
      }
      LogNote(4,plural("Received valid info about %d peer$|s$",peers_count),peers_count);
   }
#if INET6
   peers_count=0;
   b_peers=reply->lookup("peers6",BeNode::BE_STR);
   if(b_peers) { // binary model
      const char *data=b_peers->str;
      int len=b_peers->str.length();
      while(len>=18) {
	 sockaddr_u a;
	 a.set_compact(data,18);
	 data+=18;
	 len-=18;
	 parent->AddPeer(new TorrentPeer(parent,&a,tracker_no));
	 peers_count++;
      }
      LogNote(4,plural("Received valid info about %d IPv6 peer$|s$",peers_count),peers_count);
   }
#endif//INET6
   tracker_timer.Reset();
   tracker_reply=0;
   return MOVED;
}

void Torrent::CleanPeers()
{
   // remove uninteresting peers and request more
   for(int i=0; i<peers.count(); i++) {
      const TorrentPeer *peer=peers[i];
      if(peer->ActivityTimedOut()) {
	 LogNote(4,"removing uninteresting peer %s (%s)",peer->GetName(),peers[i]->Status());
	 BlackListPeer(peer,"2h");
	 peers.remove(i--);
      }
   }
}

int TorrentTracker::Do()
{
   int m=STALL;
   if(Failed())
      return m;
   if(tracker_reply) {
      m|=HandleTrackerReply();
   } else {
      if(tracker_timer.Stopped()) {
	 parent->CleanPeers();
	 SendTrackerRequest(0);
      }
   }
   return m;
}
void TorrentTracker::NextTracker()
{
   current_tracker++;
   if(current_tracker>=tracker_urls.count())
      current_tracker=0;
   ParsedURL u(GetURL(),true);
   t_session=FileAccess::New(&u);
}
void TorrentTracker::Start()
{
   if(t_session || Failed())
      return;
   ParsedURL u(GetURL(),true);
   t_session=FileAccess::New(&u);
   SendTrackerRequest("started");
}

void Torrent::StartTrackers()
{
   for(int i=0; i<trackers.count(); i++) {
      trackers[i]->Start();
   }
   if(!is_private)
      AnnounceDHT();
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
   return port;
}

void Torrent::AnnounceDHT()
{
   if(dht)
      dht->AnnouncePeer(this);
#if INET6
   if(dht_ipv6)
      dht_ipv6->AnnouncePeer(this);
#endif
   dht_announce_timer.Reset();
}

void Torrent::SetMetadata(const xstring& md)
{
   metadata.set(md);

   xstring new_info_hash;
   SHA1(metadata,new_info_hash);
   if(info_hash && info_hash.ne(new_info_hash)) {
      metadata.unset();
      SetError("metadata does not match info_hash");
      return;
   }
   info_hash.set(new_info_hash);

   if(!info) {
      int rest;
      info=BeNode::Parse(metadata,metadata.length(),&rest);
      if(!info) {
	 SetError("cannot parse metadata");
	 return;
      }
      xmap_p<BeNode> d;
      d.add("info",info);
      metainfo_tree=new BeNode(&d);
      InitTranslation();
   }

   BeNode *b_piece_length=Lookup(info,"piece length",BeNode::BE_INT);
   if(!b_piece_length)
      return;
   piece_length=b_piece_length->num;
   LogNote(4,"Piece length is %u",piece_length);

   BeNode *b_name=Lookup(info,"name",BeNode::BE_STR);
   if(!b_name)
      return;
   TranslateString(b_name);
   name.set(b_name->str_lc);
   Reconfig(0);

   BeNode *files=info->lookup("files");
   if(!files) {
      single_file=true;
      BeNode *length=Lookup(info,"length",BeNode::BE_INT);
      if(!length)
	 return;
      total_length=length->num;
   } else {
      single_file=false;
      if(files->type!=BeNode::BE_LIST) {
	 SetError("Meta-data: wrong `info/files' type, must be LIST");
	 return;
      }
      total_length=0;
      for(int i=0; i<files->list.length(); i++) {
	 if(files->list[i]->type!=BeNode::BE_DICT) {
	    SetError(xstring::format("Meta-data: wrong `info/files[%d]' type, must be LIST",i));
	    return;
	 }
	 BeNode *f=Lookup(files->list[i]->dict,"length",BeNode::BE_INT);
	 if(!f)
	    return;
	 if(!Lookup(files->list[i]->dict,"path",BeNode::BE_LIST))
	    return;
	 total_length+=f->num;
      }
   }
   LogNote(4,"Total length is %llu",total_length);
   total_left=total_length;

   last_piece_length=total_length%piece_length;
   if(last_piece_length==0)
      last_piece_length=piece_length;

   total_pieces=(total_length+piece_length-1)/piece_length;

   BeNode *b_pieces=Lookup(info,"pieces",BeNode::BE_STR);
   if(!b_pieces)
      return;
   pieces=&b_pieces->str;
   if(pieces->length()!=SHA1_DIGEST_SIZE*total_pieces) {
      SetError("Meta-data: invalid `pieces' length");
      return;
   }

   is_private=info->lookup_int("private");

   Torrent *other=FindTorrent(info_hash);
   if(other) {
      if(other!=this) {
	 SetError("This torrent is already running");
	 return;
      }
   } else {
      AddTorrent(this);
   }

   my_bitfield=new BitField(total_pieces);
   for(unsigned p=0; p<total_pieces; p++)
      piece_info.append(new TorrentPiece(BlocksInPiece(p)));

   if(!force_valid) {
      validate_index=0;
      validating=true;
      recv_rate.Reset();
   } else {
      for(unsigned i=0; i<total_pieces; i++)
	 my_bitfield->set_bit(i,1);
      complete_pieces=total_pieces;
      total_left=0;
      complete=true;
      seed_timer.Reset();
      dht_announce_timer.Stop();
   }
   DisconnectPeers(); // restart all connected peers
}

void Torrent::DisconnectPeers()
{
   for(int i=0; i<peers.count(); i++) {
      peers[i]->Disconnect(); // restart all connected peers
   }
}

void Torrent::ParseMagnet(const char *m0)
{
   md_download.nset("",0); // start the download
   char *m=alloca_strdup(m0);
   for(char *p=strtok(m,"&"); p; p=strtok(NULL,"&")) {
      char *v=strchr(p,'=');
      if(!v)
	 continue;
      *v++=0;
      if(!strcmp(p,"xt")) {
	 if(strncmp(v,"urn:btih:",9)) {
	    SetError("Only BitTorrent magnet links are supported");
	    return;
	 }
	 v+=9;
	 if(strlen(v)!=40) {
	    SetError("Invalid length of urn:btih in magnet link");
	    return;
	 }
	 xstring& btih=xstring::get_tmp(v);
	 btih.hex_decode();
	 if(btih.length()!=20) {
	    SetError("Invalid value of urn:btih in magnet link");
	    return;
	 }
	 info_hash.move_here(btih);

	 if(FindTorrent(info_hash)) {
	    SetError("This torrent is already running");
	    return;
	 }
	 AddTorrent(this);
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
}

int Torrent::Do()
{
   int m=STALL;
   if(Done() || shutting_down)
      return m;
   if(!metainfo_tree && metainfo_url && !md_download) {
      // retrieve metainfo if don't have already.
      if(!metainfo_fa) {
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
	    md_download.nset("",0); // start the download
	    if(FindTorrent(info_hash)) {
	       SetError("This torrent is already running");
	       return MOVED;
	    }
	    AddTorrent(this);
	    return MOVED;
	 }
	 ParsedURL u(metainfo_url,true);
	 if(!u.proto)
	    u.proto.set("file");
	 LogNote(9,"Retrieving meta-data from `%s'...\n",metainfo_url.get());
	 metainfo_fa=FileAccess::New(&u);
	 metainfo_fa->Open(u.path,FA::RETRIEVE);
	 metainfo_fa->SetFileURL(metainfo_url);
	 metainfo_data=new IOBufferFileAccess(metainfo_fa);
	 m=MOVED;
      }
      if(metainfo_data->Error()) {
	 SetError(new Error(-1,metainfo_data->ErrorText(),metainfo_data->ErrorFatal()));
	 metainfo_fa->Close();
	 metainfo_data=0;
	 return MOVED;
      }
      if(!metainfo_data->Eof())
      	 return m;
      metainfo_fa->Close();
      metainfo_fa=0;
      LogNote(9,"meta-data EOF\n");
      int rest;
      metainfo_tree=BeNode::Parse(metainfo_data->Get(),metainfo_data->Size(),&rest);
      metainfo_data=0;
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
	    BeNode *b_ip=n->list[0];
	    BeNode *b_port=n->list[1];
	    if(b_ip->type!=BeNode::BE_STR || b_port->type!=BeNode::BE_INT)
	       continue;
	    sockaddr_u a;
#if INET6
	    if(b_ip->str.instr(':')>=0) {
	       a.sa.sa_family=AF_INET6;
	       if(inet_pton(AF_INET6,b_ip->str,&a.in6.sin6_addr)<=0)
		  continue;
	       a.set_port(b_port->num);
	       dht_ipv6->SendPing(a);
	    } else
#endif
	    {
	       a.sa.sa_family=AF_INET;
	       if(!inet_aton(b_ip->str,&a.in.sin_addr))
		  continue;
	       a.set_port(b_port->num);
	       dht->SendPing(a);
	    }
	 }
      }

      info=Lookup(metainfo_tree,"info",BeNode::BE_DICT);
      if(!info)
         return MOVED;

      SetMetadata(info->str);
      m=MOVED;
      if(Done())
	 return m;
   }
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
      }
      DisconnectPeers();   // restart peer connections
      dht_announce_timer.Stop();
   }
   if(GetPort())
      StartTrackers();

   if(peers_scan_timer.Stopped())
      ScanPeers();
   if(optimistic_unchoke_timer.Stopped())
      OptimisticUnchoke();

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

   // rebuild lists of needed pieces
   if(!complete && pieces_needed_rebuild_timer.Stopped()) {
      pieces_needed.truncate();
      bool enter_end_game=true;
      for(unsigned i=0; i<total_pieces; i++) {
	 if(!my_bitfield->get_bit(i)) {
	    if(!piece_info[i]->has_a_downloader())
	       enter_end_game=false;
	    if(piece_info[i]->sources_count==0)
	       continue;
	    pieces_needed.append(i);
	 }
      }
      if(!end_game && enter_end_game) {
	 LogNote(1,"entering End Game mode");
	 end_game=true;
      }
      cmp_torrent=this;
      pieces_needed.qsort(PiecesNeededCmp);
      pieces_needed_rebuild_timer.Reset();
   }

   if(complete && SeededEnough()) {
      Shutdown();
      return MOVED;
   }

   return m;
}

void Torrent::BlackListPeer(const TorrentPeer *peer,const char *timeout)
{
   if(!peer->IsPassive())
      black_list->Add(peer->GetAddress(),timeout);
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
   if(black_list->Listed(peer->GetAddress())) {
      delete peer;
      return;
   }
   for(int i=0; i<peers.count(); i++) {
      if(peers[i]->AddressEq(peer)) {
	 if(peer->Connected() && !peers[i]->Connected()) {
	    peers[i]=peer;
	 } else {
	    delete peer;
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

void TorrentTracker::SendTrackerRequest(const char *event)
{
   if(!t_session)
      return;

   xstring request(GetURL());
   request.appendf("info_hash=%s",url::encode(parent->GetInfoHash(),URL_PATH_UNSAFE).get());
   request.appendf("&peer_id=%s",url::encode(parent->GetMyPeerId(),URL_PATH_UNSAFE).get());
   request.appendf("&port=%d",parent->GetPort());
   request.appendf("&uploaded=%llu",parent->GetTotalSent());
   request.appendf("&downloaded=%llu",parent->GetTotalRecv());
   if(parent->HasMetadata())
      request.appendf("&left=%llu",parent->GetTotalLeft());
   request.append("&compact=1&no_peer_id=1");
   if(event)
      request.appendf("&event=%s",event);
   const char *ip=ResMgr::Query("torrent:ip",0);
   if(ip && ip[0])
      request.appendf("&ip=%s",ip);

#if INET6
   int port=Torrent::GetPortIPv4();
   int port_ipv6=Torrent::GetPortIPv6();
   const char *ipv6=ResMgr::Query("torrent:ipv6",0);
   if(port && ip && ip[0])
      request.appendf("&ipv4=%s:%d",ip,port);
   if(port_ipv6)
      request.appendf("&ipv6=[%s]:%d",ipv6&&ipv6[0]?ipv6:Torrent::GetAddressIPv6(),port_ipv6);
#endif

   int numwant=parent->GetWantedPeersCount();
   if(numwant>=0)
      request.appendf("&numwant=%d",numwant);
   const xstring& my_key=parent->GetMyKey();
   if(my_key)
      request.appendf("&key=%s",my_key.get());
   if(tracker_id)
      request.appendf("&trackerid=%s",url::encode(tracker_id,URL_PATH_UNSAFE).get());
   LogSend(4,request);
   t_session->Open(url::path_ptr(request),FA::RETRIEVE);
   t_session->SetFileURL(request);
   tracker_reply=new IOBufferFileAccess(t_session);
   tracker_timeout_timer.Reset();
}

const char *Torrent::MakePath(BeNode *p) const
{
   BeNode *path=p->lookup("path",BeNode::BE_LIST);
   static xstring buf;
   buf.set(name);
   if(buf.eq("..") || buf[0]=='/') {
      buf.set_substr(0,0,"_",1);
   }
   for(int i=0; i<path->list.count(); i++) {
      BeNode *e=path->list[i];
      if(e->type==BeNode::BE_STR) {
	 TranslateString(e);
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
   const BeNode *files=info->lookup("files",BeNode::BE_LIST);
   off_t target_pos=(off_t)piece*piece_length+begin;
   if(!files) {
      *f_pos=target_pos;
      *f_tail=total_length-target_pos;
      return name;
   } else {
      off_t scan_pos=0;
      for(int i=0; i<files->list.length(); i++) {
	 off_t file_length=files->list[i]->lookup_int("length");
	 if(scan_pos<=target_pos && scan_pos+file_length>target_pos) {
	    *f_pos=target_pos-scan_pos;
	    *f_tail=file_length-*f_pos;
	    return MakePath(files->list[i]);
	 }
	 scan_pos+=file_length;
      }
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
      for(const FD *f=&cache[i].each_begin(); f->last_used; f=&cache[i].each_next()) {
	 if(f->fd==-1 && f->last_used+1<now.UnixTime()) {
	    cache[i].remove(cache[i].each_key());
	    continue;
	 }
	 if(f->last_used+max_time<now.UnixTime()) {
	    close(f->fd);
	    cache[i].remove(cache[i].each_key());
	 }
      }
   }
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
	    close(f.fd);
	 }
	 cache[i].remove(n);
      }
   }
}
void FDCache::CloseAll()
{
   for(int i=0; i<3; i++) {
      for(const FD *f=&cache[i].each_begin(); f->last_used; f=&cache[i].each_next()) {
	 if(f->fd!=-1)
	    close(f->fd);
	 cache[i].remove(cache[i].each_key());
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
      for(const FD *f=&cache[i].each_begin(); f; f=&cache[i].each_next()) {
	 if(oldest_key==0 || f->last_used<oldest_time) {
	    oldest_key=&cache[i].each_key();
	    oldest_time=f->last_used;
	    oldest_fd=f->fd;
	    oldest_mode=i;
	 }
      }
   }
   if(!oldest_key)
      return false;
   if(oldest_fd!=-1)
      close(oldest_fd);
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
#ifdef HAVE_POSIX_FALLOCATE
   if(ci==O_RDWR) {
      struct stat st;
      // check if it is newly created file, then allocate space
      if(fstat(fd,&st)!=-1 && st.st_size==0)
	 posix_fallocate(fd,0,size);
   }
#endif//HAVE_POSIX_FALLOCATE
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
      piece_info[piece]->block_map.set_bit(b++,1);
   }
   if(piece_info[piece]->block_map.has_all_set() && !my_bitfield->get_bit(piece)) {
      ValidatePiece(piece);
      if(!my_bitfield->get_bit(piece)) {
	 LogError(0,"new piece %u digest mismatch",piece);
	 src_peer->MarkPieceInvalid(piece);
	 return;
      }
      LogNote(3,"piece %u complete",piece);
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
      if(peer->Failed())
	 LogError(2,"peer %s failed: %s",peer->GetName(),peer->ErrorText());
      else if(peer->Disconnected())
	 LogNote(4,"peer %s disconnected",peer->GetName());
      else if(peer->myself) {
	 LogNote(4,"removing myself-connected peer %s",peer->GetName());
	 BlackListPeer(peer,"forever");
      } else if(peer->duplicate) {
	 LogNote(4,"removing duplicate peer %s",peer->GetName());
      } else if(complete && peer->Seed())
	 LogNote(4,"removing unneeded peer %s (%s)",peer->GetName(),peers[i]->Status());
      else
	 continue;
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
   rate_limit.Reconfig(name,metainfo_url);
   if(listener)
      StartDHT();
}

const xstring& Torrent::Status()
{
   if(!metadata) {
      if(md_download.length()>0)
	 return xstring::format(_("Getting meta-data: %s"),
	    xstring::format("%u/%u",(unsigned)md_download.length(),(unsigned)metadata_size).get());
      else
	 return xstring::get_tmp(_("Waiting for meta-data"));
   }
   if(metainfo_data)
      return xstring::format(_("Getting meta-data: %s"),metainfo_data->Status());
   if(validating) {
      return xstring::format(_("Validation: %u/%u (%u%%) %s%s"),validate_index,total_pieces,
	 validate_index*100/total_pieces,recv_rate.GetStrS(),
	 recv_rate.GetETAStrFromSize((off_t)(total_pieces-validate_index-1)*piece_length+last_piece_length).get());
   }
   if(shutting_down) {
      if(trackers.count()==0)
	 return xstring::get_tmp("");
      for(int i=0; i<trackers.count(); i++) {
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
   xstring& buf=xstring::format("dn:%llu %sup:%llu %scomplete:%u/%u (%u%%)",
      total_recv,recv_rate.GetStrS(),total_sent,send_rate.GetStrS(),
      complete_pieces,total_pieces,
      unsigned((total_length-total_left)*100/total_length));
   if(end_game)
      buf.append(" end-game");
   buf.append(' ');
   buf.append(recv_rate.GetETAStrFromSize(total_left));
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
   Disconnect();
}

void TorrentPeer::Disconnect()
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
   if(sock!=-1)
      close(sock);
   sock=-1;
   connected=false;
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
   ext.add("v",new BeNode(PACKAGE"/"VERSION));
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
   if(data.length()!=p->req_length) {
      if(parent->my_bitfield->get_bit(p->index))
	 parent->SetError(xstring::format("failed to read piece %u",p->index));
      return;
   }
   LogSend(8,xstring::format("piece:%u begin:%u size:%u",p->index,p->begin,p->req_length));
   PacketPiece(p->index,p->begin,data).Pack(send_buf);
   peer_sent+=data.length();
   parent->total_sent+=data.length();
   parent->send_rate.Add(data.length());
   peer_send_rate.Add(data.length());
   BytesPut(data.length());
   activity_timer.Reset();
}

int TorrentPeer::SendDataRequests(unsigned p)
{
   if(p==NO_PIECE)
      return 0;

   int sent=0;
   unsigned blocks=parent->BlocksInPiece(p);
   unsigned bytes_allowed=BytesAllowed(RateLimit::GET);
   for(unsigned b=0; b<blocks; b++) {
      if(parent->piece_info[p]->block_map.get_bit(b))
	 continue;
      if(parent->piece_info[p]->downloader[b]) {
	 if(!parent->end_game)
	    continue;
	 if(parent->piece_info[p]->downloader[b]==this)
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
	 if(peer_bitfield->get_bit(p) && !parent->my_bitfield->get_bit(p)) {
	    if(SendDataRequests(p)>0)
	       return;
	 }
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
      if(peer_bitfield->get_bit(p) && !parent->my_bitfield->get_bit(p)
      && SendDataRequests(p)>0)
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
	 if(!parent->piece_info[p]->block_map.has_any_set()
	 && random()/13%16==0)
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
   && parent->piece_info[p]->block_map.has_any_set()
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
   parent->piece_info[p]->sources_count+=diff;
   peer_complete_pieces+=diff;
   peer_bitfield->set_bit(p,have);

   if(parent->piece_info[p]->sources_count==0)
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
	 if(DHT_Enabled()) {
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
	 parent->total_recv+=len;
	 parent->recv_rate.Add(len);
	 peer_recv_rate.Add(len);

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
      md_download.nset("",0);
      return;
   }
   SetMetadata(md_download);
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
      if(!piece) {
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
	 LogError(4,"connect(%s): %s\n",GetName(),strerror(e));
	 Disconnect();
	 if(FA::NotSerious(e))
	    return MOVED;
	 SetError(strerror(e));
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
      Disconnect();
      return MOVED;
   }
   if(recv_buf->Error())
   {
      LogError(2,"recieve: %s",recv_buf->ErrorText());
      Disconnect();
      return MOVED;
   }
   if(!peer_id) {
      // expect handshake
      unpack_status_t s=RecvHandshake();
      if(s==UNPACK_NO_DATA_YET)
	 return m;
      if(s!=UNPACK_SUCCESS) {
	 if(s==UNPACK_PREMATURE_EOF) {
	    if(recv_buf->Size()>0)
	       LogError(2,_("peer unexpectedly closed connection after %s"),recv_buf->Dump());
	    else
	       LogError(4,_("peer closed connection (before handshake)"));
	 }
	 Disconnect();
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
      Disconnect();
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
      Disconnect();
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
      if(st==UNPACK_PREMATURE_EOF)
	 LogError(2,_("peer unexpectedly closed connection after %s"),recv_buf->Dump());
      else
	 LogError(2,_("invalid peer response format"));
      Disconnect();
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

   Log::global->Format(11,"<--- got a packet, length=%d, type=%d(%s)\n",
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
   if(sock==-1)
      return _("Not connected");
   if(!connected)
      return _("Connecting...");
   if(!peer_id)
      return _("Handshaking...");
   xstring &buf=xstring::format("dn:%llu %sup:%llu %s",
      peer_recv,peer_recv_rate.GetStrS(),peer_sent,peer_send_rate.GetStrS());
   if(peer_interested)
      buf.append("peer-interested ");
   if(peer_choking)
      buf.append("peer-choking ");
   if(am_interested)
      buf.append("am-interested ");
   if(am_choking)
      buf.append("am-choking ");
   if(parent->HasMetadata())
      buf.appendf("complete:%u/%u (%u%%)",peer_complete_pieces,parent->total_pieces,
	 peer_complete_pieces*100/parent->total_pieces);
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


void TorrentBlackList::check_expire()
{
   for(Timer *e=bl.each_begin(); e; e=bl.each_next()) {
      if(e->Stopped()) {
	 Log::global->Format(4,"---- black-delisting peer %s\n",bl.each_key().get());
	 delete e;
	 bl.remove(bl.each_key());
      }
   }
}
void TorrentBlackList::Add(const sockaddr_u &a,const char *t)
{
   check_expire();
   if(Listed(a))
      return;
   Log::global->Format(4,"---- black-listing peer %s (%s)\n",(const char*)a,t);
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
	 listen(sock,5);

      // get the allocated port
      socklen_t addr_len=sizeof(addr);
      getsockname(sock,&addr.sa,&addr_len);
      LogNote(4,"listening on %s %s",type==SOCK_STREAM?"tcp":"udp",addr.to_string());
      m=MOVED;

      if(type==SOCK_DGRAM && Torrent::dht)
	 Torrent::GetDHT(af)->Load();
   }

   if(type==SOCK_DGRAM) {
      char buf[0x1000];
      sockaddr_u src;
      socklen_t src_len=sizeof(src);
      int res=recvfrom(sock,buf,sizeof(buf),0,&src.sa,&src_len);
      if(res==-1) {
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
      LogError(3,"peer %s sent unknown info_hash=%s in handshake",
	 remote_addr->to_string(),info_hash.hexdump());
      close(sock);
      delete recv_buf;
      return;
   }
   t->Accept(sock,remote_addr,recv_buf);
}

TorrentDispatcher::TorrentDispatcher(int s,const sockaddr_u *a)
   : sock(s), addr(*a),
     recv_buf(new IOBufferFDStream(new FDStream(sock,"<input-socket>"),IOBuffer::GET)),
     timeout_timer(60)
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
      deleting=true;
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
	 deleting=true;
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
   deleting=true;
   return MOVED;
}

DHT::DHT(int af,const xstring& id)
   : af(af), sent_req_expire_scan(5), search_cleanup_timer(5),
     refresh_timer(60), nodes_cleanup_timer(30), save_timer(60),
     node_id(id.copy()), t(random())
{
   LogNote(10,"creating DHT with id=%s",node_id.hexdump());
}
DHT::~DHT()
{
}
int DHT::Do()
{
   int m=STALL;
   if(state_io) {
      if(state_io->GetDirection()==IOBuffer::GET) {
	 if(state_io->Error()) {
	    LogError(1,"loading state: %s",state_io->ErrorText());
	    state_io=0;
	 } else if(state_io->Eof()) {
	    Load(state_io);
	    state_io=0;
	 }
      } else {
	 if(state_io->Error())
	    LogError(1,"saving state: %s",state_io->ErrorText());
	 if(state_io->Done())
	    state_io=0;
      }
   }
   if(sent_req_expire_scan.Stopped()) {
      for(const Request *r=sent_req.each_begin(); r; r=sent_req.each_next()) {
	 if(r->Expired()) {
	    LogError(4,"DHT request %s to %s timed out",r->data->lookup_str("q").get(),r->addr.to_string());
	    Node *n=nodes.lookup(r->GetNodeId());
	    if(n) {
	       n->LostPing();
	       LogNote(4,"DHT node %s has lost %d packets",n->GetName(),n->ping_lost_count);
	    }
	    sent_req.remove(sent_req.each_key());
	 }
      }
      sent_req_expire_scan.Reset();
   }
   if(search_cleanup_timer.Stopped()) {
      for(int i=0; i<search.count(); i++) {
	 if(search[i]->search_timer.Stopped())
	    search.remove(i--);
      }
      search_cleanup_timer.Reset();
   }
   if(nodes_cleanup_timer.Stopped()) {
      for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
	 if(n->IsBad()) {
	    LogNote(9,"removing bad node %s",n->GetName());
	    RemoveNode(n);
	 } else if(n->good_timer.TimeLeft().Seconds() <= 60) {
	    SendPing(n);
	 }
      }
      nodes_cleanup_timer.Reset();
      if(save_timer.Stopped()) {
	 Save();
	 save_timer.Reset();
      }
      if(nodes.count()>0 && routes[0]->nodes.count()<2 && search.count()==0) {
	 // run bootstrap search
	 LogNote(9,"bootstrapping");
	 Search *s=new Search(node_id);
	 s->Bootstrap();
	 StartSearch(s);
      }
   }
   if(refresh_timer.Stopped()) {
      for(int i=0; i<routes.count(); i++) {
	 if(!routes[i]->IsFresh()) {
	    LogNote(9,"refreshing route bucket %d",i);
	    // make random id in the range
	    int bytes=routes[i]->prefix_bits/8;
	    int bits=routes[i]->prefix_bits%8;
	    xstring random_id(routes[i]->prefix.get(),bytes+(bits>0));
	    unsigned mask=(1<<(8-bits))-1;
	    if(bits>0)
	       random_id.get_non_const()[bytes]|=(random()/13)&mask;
	    while(random_id.length()<20)
	       random_id.append(char(random()/13));
	    StartSearch(new Search(random_id));
	    routes[i]->fresh_timer.Reset();
	 }
      }
      refresh_timer.Reset();
   }
   if(nodes.count()<K*K && load_nodes.count()>0 && send_queue.count()<K) {
      xstring &n=*load_nodes.next();
      sockaddr_u a;
      a.set_compact(n);
      SendPing(a);
      m=MOVED;
   }

   // manual bootstrapping
   if(resolver) {
      if(resolver->Error()) {
	 LogError(1,"%s",resolver->ErrorMsg());
	 resolver=0;
	 m=MOVED;
      } else if(resolver->Done()) {
	 const xarray<sockaddr_u>& r=resolver->Result();
	 for(int i=0; i<r.count(); i++)
	    Torrent::GetDHT(r[i])->SendPing(r[i]);
	 resolver=0;
	 m=MOVED;
      }
   }
   if(!state_io && !resolver && bootstrap_nodes.count()>0) {
      xstring &b=*bootstrap_nodes.next();
      ParsedURL u(b);
      if(u.proto==0 && u.host)
	 resolver=new Resolver(u.host,u.port,"6881");
      m=MOVED;
   }

   if(send_queue.count()>0 && MaySendMessage()) {
      SendMessage(send_queue.next().borrow());
      m=MOVED;
   }

   return m;
}
BeNode *DHT::NewQuery(const char *q,xmap_p<BeNode>& a)
{
   xmap_p<BeNode> m;
   BeNode *n=new BeNode((const char*)&t,sizeof(t));
   m.add("t",n);
   t++;
   m.add("y",new BeNode("q",1));
   m.add("q",new BeNode(q));
   a.add("id",new BeNode(node_id));
   m.add("a",new BeNode(&a));
   return new BeNode(&m);
}
BeNode *DHT::NewReply(const xstring& t0,xmap_p<BeNode>& r)
{
   xmap_p<BeNode> m;
   m.add("t",new BeNode(t0));
   m.add("y",new BeNode("r",1));
   r.add("id",new BeNode(node_id));
   m.add("r",new BeNode(&r));
   return new BeNode(&m);
}
BeNode *DHT::NewError(const xstring& t0,int code,const char *msg)
{
   xmap_p<BeNode> m;
   m.add("t",new BeNode(t0));
   m.add("y",new BeNode("e",1));
   xarray_p<BeNode> e;
   e.append(new BeNode(code));
   e.append(new BeNode(msg));
   m.add("e",new BeNode(&e));
   return new BeNode(&m);
}
const char *DHT::MessageType(BeNode *q)
{
   const xstring& y=q->lookup_str("y");
   const char *msg_type="message";
   if(y.eq("q"))
      msg_type=q->lookup_str("q");
   else if(y.eq("r"))
      msg_type="response";
   else if(y.eq("e"))
      msg_type="error";
   return msg_type;
}
void DHT::SendMessage(BeNode *q,const sockaddr_u& a,const xstring& id)
{
   send_queue.push(new Request(q,a,id));
}
void DHT::SendMessage(Request *req)
{
   req->expire_timer.Reset();
   BeNode *q=req->data.get_non_const();
   const sockaddr_u& a=req->addr;
   LogSend(4,xstring::format("sending DHT %s to %s %s",MessageType(q),
      a.to_string(),q->Format1()));
   int res=-1;
   res=Torrent::GetUDPSocket(af)->SendUDP(a,q->Pack());
   if(res!=-1 && q->lookup_str("y").eq("q"))
      sent_req.add(q->lookup_str("t"),req);
   else
      delete req;
}
bool DHT::MaySendMessage()
{
   return Torrent::GetUDPSocket(af)->MaySendUDP();
}
void DHT::SendPing(const sockaddr_u& a,const xstring& id)
{
   if(a.port()==0 || a.is_private() || a.is_reserved() || a.is_multicast())
      return;
   Enter();
   xmap_p<BeNode> arg;
   SendMessage(NewQuery("ping",arg),a,id);
   Leave();
}
void DHT::AnnouncePeer(const Torrent *t)
{
   const xstring& info_hash=t->GetInfoHash();
   // check for duplicated announce
   for(int i=0; i<search.count(); i++) {
      if(search[i]->target_id.eq(info_hash))
	 return;
   }
   Enter();
   Search *s=new Search(info_hash);
   s->WantPeers(t->Complete());
#if INET6
   // try to find nodes in the other AF if needed
   if(Torrent::GetDHT(af==AF_INET?AF_INET6:AF_INET)->nodes.count()<1)
      s->Bootstrap();
#endif
   StartSearch(s);
   Leave();
}
int DHT::AddNodesToReply(xmap_p<BeNode> &r,const xstring& target,int max_count)
{
   xarray<const Node*> n;
   FindNodes(target,n,max_count);
   xstring compact_nodes;
   for(int i=0; i<n.count(); i++) {
      compact_nodes.append(n[i]->id);
      compact_nodes.append(n[i]->addr.compact());
   }
   r.add(af==AF_INET?"nodes":"nodes6",new BeNode(compact_nodes));
   return n.count();
}
int DHT::AddNodesToReply(xmap_p<BeNode> &r,const xstring& target,bool want_n4,bool want_n6)
{
   int nodes_count=0;
   if(want_n4)
      nodes_count+=Torrent::GetDHT(AF_INET)->AddNodesToReply(r,target,K);
   if(want_n6)
      nodes_count+=Torrent::GetDHT(AF_INET6)->AddNodesToReply(r,target,K);
   return nodes_count;
}
void DHT::HandlePacket(BeNode *p,const sockaddr_u& src)
{
   LogRecv(4,xstring::format("received DHT %s from %s %s",MessageType(p),
      src.to_string(),p->Format1()));
   const xstring& t=p->lookup_str("t");
   if(!t)
      return;
   const xstring& y=p->lookup_str("y");
   if(!y)
      return;
   if(y.eq("q")) {
      const xstring& q=p->lookup_str("q");
      if(!q)
	 return;
      BeNode *a=p->lookup("a",BeNode::BE_DICT);
      if(!a)
	 return;
      const xstring& id=a->lookup_str("id");
      if(id.length()!=20)
	 return;
      xmap_p<BeNode> r;
      const xstring &src_compact=src.compact_addr();
      r.add("ip",new BeNode(src_compact));
      Node *node=FoundNode(id,src,false);

      bool want_n4=false;
      bool want_n6=false;
      BeNode *want=a->lookup("want",BeNode::BE_LIST);
      if(want) {
	 for(int i=0; i<want->list.count(); i++) {
	    BeNode *w=want->list[i];
	    if(w->type!=BeNode::BE_STR)
	       continue;
	    if(w->str.eq("n4"))
	       want_n4=true;
	    if(w->str.eq("n6"))
	       want_n6=true;
	 }
      }
      if(!want_n4 && !want_n6) {
	 want_n4=(src.family()==AF_INET);
	 want_n6=(src.family()==AF_INET6);
      }

      if(q.eq("ping")) {
	 LogSend(5,xstring::format("DHT ping reply to %s",src.to_string()));
	 SendMessage(NewReply(t,r),src);
      } else if(q.eq("find_node")) {
	 const xstring& target=a->lookup_str("target");
	 if(!target)
	    return;
	 int nodes_count=AddNodesToReply(r,target,want_n4,want_n6);
	 LogSend(5,xstring::format("DHT find_node reply with %d nodes to %s",nodes_count,src.to_string()));
	 SendMessage(NewReply(t,r),src);
      } else if(q.eq("get_peers")) {
	 const xstring& info_hash=a->lookup_str("info_hash");
	 if(info_hash.length()!=20)
	    return;
	 bool noseed=a->lookup_int("noseed");
	 xarray_p<Peer> *p=peers.lookup(info_hash);
	 int nodes_count=0;
	 if(!p) {
	    nodes_count=AddNodesToReply(r,info_hash,want_n4,want_n6);
	 } else {
	    xarray_p<BeNode> values;
	    for(int i=0; i<p->count(); i++) {
	       Peer *peer=(*p)[i];
	       if(noseed && peer->seed)
		  continue;
	       if(!peer->IsGood())
		  continue;
	       if(peer->compact_addr.length()==6 && !want_n4)
		  continue;
	       if(peer->compact_addr.length()==18 && !want_n6)
		  continue;
	       values.append(new BeNode(peer->compact_addr));
	    }
	    if(values.count()>0)
	       r.add("values",new BeNode(&values));
	    else
	       nodes_count=AddNodesToReply(r,info_hash,want_n4,want_n6);
	 }
	 if(!node->my_token || node->token_timer.Stopped()) {
	    // make new token
	    node->my_last_token.set(node->my_token);
	    node->my_token.truncate();
	    for(int i=0; i<16; i++)
	       node->my_token.append(char(random()/13));
	    node->token_timer.Reset();
	 }
	 r.add("token",new BeNode(node->my_token));
	 LogSend(5,xstring::format("DHT get_peers reply with %d values and %d nodes to %s",
	    p?p->count():0,nodes_count,src.to_string()));
	 SendMessage(NewReply(t,r),src);
      } else if(q.eq("announce_peer")) {
	 // need a valid token
	 if(!node->my_token || node->token_timer.Stopped())
	    return;
	 const xstring& token=a->lookup_str("token");
	 if(!token)
	    return;
	 if(token.ne(node->my_token) && token.ne(node->my_last_token)) {
	    SendMessage(NewError(t,ERR_PROTOCOL,"invalid token"),src);
	    return;
	 }
	 // ok, token is valid. Now add the peer.
	 const xstring& info_hash=a->lookup_str("info_hash");
	 if(info_hash.length()!=20)
	    return;
	 int port=a->lookup_int("port");
	 if(!port)
	    return;
	 bool seed=a->lookup_int("seed");
	 sockaddr_u peer_addr(src);
	 peer_addr.set_port(port);
	 AddPeer(info_hash,peer_addr.compact(),seed);
	 SendMessage(NewReply(t,r),src);
      } else {
	 SendMessage(NewError(t,ERR_UNKNOWN_METHOD,"method unknown"),src);
      }
      return;
   }
   Ref<Request> req(sent_req.borrow(t));
   if(!req) {
      LogError(2,"got DHT reply with unknown `t' from %s",src.to_string());
      return;
   }

   const xstring& q=req->data->lookup_str("q");
   if(y.eq("r")) {
      BeNode *r=p->lookup("r",BeNode::BE_DICT);
      if(!r)
	 return;
      const xstring& id=r->lookup_str("id");
      if(id.length()!=20)
	 return;
      const xstring& ip=r->lookup_str("ip");
      if(ip && !ValidNodeId(node_id,ip)) {
	 if(!ip_voted.lookup(src.compact_addr())) {
	    sockaddr_u reported_ip;
	    reported_ip.set_compact(ip);
	    LogNote(2,"%s reported our IP as %s",src.to_string(),reported_ip.address());
	    unsigned& votes=ip_votes.lookup_Lv(ip);
	    votes++;
	    if(votes>=4) {
	       // we have incorrect node_id, restart with correct one.
	       MakeNodeId(node_id,ip);
	       LogNote(0,"restarting DHT with new id %s",node_id.hexdump());
	       Restart();
	    }
	 }
      }
      FoundNode(id,src,true);
      if(q.eq("get_peers")) {
	 const xstring& info_hash=req->data->lookup("a")->lookup_str("info_hash");
	 Torrent *torrent=Torrent::FindTorrent(info_hash);
	 BeNode *values=r->lookup("values",BeNode::BE_LIST);
	 if(values) {
	    // some peers found.
	    for(int i=0; i<values->list.count(); i++) {
	       if(values->list[i]->type!=BeNode::BE_STR)
		  continue;
	       const xstring &c=values->list[i]->str;
	       sockaddr_u a;
	       if(!a.set_compact(c) || !a.port())
		  continue;
	       LogNote(9,"found peer %s for info_hash=%s",a.to_string(),info_hash.hexdump());
	       if(torrent)
		  torrent->AddPeer(new TorrentPeer(torrent,&a,TorrentPeer::TR_DHT));
	    }
	 }
	 const xstring& token=r->lookup_str("token");
	 if(token && torrent) {
	    if(!ValidNodeId(id,src.compact_addr()))
	       LogError(2,"warning: node id %s is invalid for %s",id.hexdump(),src.address());
	    // announce the torrent
	    int port=torrent->GetPortIPv4();
#if INET6
	    if(src.family()==AF_INET6)
	       port=torrent->GetPortIPv6();
#endif
	    xmap_p<BeNode> a;
	    a.add("info_hash",new BeNode(info_hash));
	    a.add("port",new BeNode(port));
	    a.add("token",new BeNode(token));
	    if(torrent->Complete())
	       a.add("seed",new BeNode(1));
	    SendMessage(NewQuery("announce_peer",a),src,id);
	 }
      }
      if(q.eq("find_node") || q.eq("get_peers")) {
	 const xstring& nodes=r->lookup_str("nodes");
	 if(nodes) {
	    LogNote(9,"adding %d nodes",(int)nodes.length()/26);
	    const char *data=nodes;
	    int len=nodes.length();
	    while(len>=26) {
	       xstring id(data,20);
	       sockaddr_u a;
	       a.set_compact(data+20,6);
	       data+=26;
	       len-=26;
	       FoundNode(id,a,false);
	    }
	 }
#if INET6
	 const xstring& nodes6=r->lookup_str("nodes6");
	 if(nodes6) {
	    LogNote(9,"adding %d nodes6",(int)nodes6.length()/38);
	    const char *data=nodes6;
	    int len=nodes6.length();
	    while(len>=38) {
	       xstring id(data,20);
	       sockaddr_u a;
	       a.set_compact(data+20,18);
	       data+=38;
	       len-=38;
	       FoundNode(id,a,false);
	    }
	 }
#endif //INET6
      }
   } else if(y.eq("e")) {
      int code=0;
      const char *msg="unknown";
      BeNode *e=p->lookup("e",BeNode::BE_LIST);
      if(e) {
	 if(e->list.count()>=1 && e->list[0]->type==BeNode::BE_INT)
	    code=e->list[0]->num;
	 if(e->list.count()>=2 && e->list[1]->type==BeNode::BE_STR)
	    msg=e->list[1]->str;
      }
      LogError(2,"got DHT error for %s (%d: %s) from %s",q.get(),code,msg,src.to_string());
   }
}
bool DHT::Node::IsBetterThan(const Node *node,const xstring& target) const
{
   for(int i=0; i<20; i++) {
      unsigned char a=this->id[i]^target[i];
      unsigned char b=node->id[i]^target[i];
      if(a<b)
	 return true;
      if(a>b)
	 return false;
   }
   return false;
}
bool DHT::Search::IsFeasible(const Node *n) const
{
   return best_node==0 || n->IsBetterThan(best_node,target_id);
}
void DHT::Search::ContinueOn(DHT *d,const Node *n)
{
   if(IsFeasible(n)) {
      best_node=n;
      depth++;
   }
   xmap_p<BeNode> a;
#if INET6
   if(bootstrap) {
	 xarray_p<BeNode> want;
	 want.append(new BeNode("n4"));
	 want.append(new BeNode("n6"));
	 a.add("want",new BeNode(&want));
   }
#endif
   if(!want_peers) {
      a.add("target",new BeNode(target_id));
      d->SendMessage(d->NewQuery("find_node",a),n->addr,n->id);
   } else {
      a.add("info_hash",new BeNode(target_id));
      if(noseed)
	 a.add("noseed",new BeNode(1));
      d->SendMessage(d->NewQuery("get_peers",a),n->addr,n->id);
   }
   search_timer.Reset();
}
void DHT::StartSearch(Search *s)
{
   xarray<const Node*> n;
   FindNodes(s->target_id,n,K);
   if(n.count()==0) {
      LogError(2,"no good nodes found in the routing table");
      // try to send everywhere
      for(const Node *node=nodes.each_begin(); node; node=nodes.each_next()) {
	 if(!node->IsBad())
	    s->ContinueOn(this,node);
      }
   } else {
      for(int i=0; i<n.count(); i++)
	 s->ContinueOn(this,n[i]);
   }
   s->depth=0;
   search.append(s);
}
DHT::Node *DHT::FoundNode(const xstring& id,const sockaddr_u& a,bool responded)
{
   if(a.port()==0 || a.is_private() || a.is_reserved() || a.is_multicast()) {
      LogError(9,"node address %s is not valid",a.to_string());
      return 0;
   }

   if(a.family()!=af) {
      assert(!responded); // we should not get replies from different AF
      const SMTaskRef<DHT>& d=Torrent::GetDHT(a);
      d->Enter();
      d->FoundNode(id,a,false);
      d->Leave();
      return 0;
   }
   Node *n=nodes.lookup(id);
   if(!n) {
      n=new Node(id,a,responded);
      AddNode(n);
   } else {
      if(responded) {
	 n->responded=true;
	 n->ResetLostPing();
      }
      if(n->responded)
	 n->SetGood();
      AddRoute(n);
   }

   // continue search
   for(int i=0; i<search.count(); i++) {
      const Ref<Search>& s=search[i];
      if(s->IsFeasible(n)) {
	 s->ContinueOn(this,n);
	 LogNote(3,"search for %s continues on %s (%s) depth=%d",
	    s->target_id.hexdump(),n->id.hexdump(),
	    n->GetName(),s->depth);
      }
   }
   return n;
}
void DHT::RemoveNode(Node *n)
{
   RemoveRoute(n);
   for(int i=0; i<search.count(); i++) {
      if(search[i]->best_node==n)
	 search.remove(i--);
   }
   nodes.remove(n->id);
}
void DHT::AddNode(Node *n)
{
   assert(n->id.length()==20);
   nodes.add(n->id,n);
   AddRoute(n);
   if(nodes.count()>MAX_NODES) {
      // remove some nodes.
      for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
	 if(!n->IsGood()) {
	    LogNote(9,"removing node %s",n->GetName());
	    RemoveNode(n);
	 }
      }
      LogNote(9,"good node count=%d",nodes.count());
   }
}
int DHT::FindRoute(const xstring& id)
{
   // routes are ordered by prefix length decreasing
   // the first route bucket always matches our node_id
   for(int i=0; i<routes.count(); i++) {
      if(routes[i]->PrefixMatch(id)) {
	 return i;
      }
   }
   return -1;
}
void DHT::RemoveRoute(const Node *n)
{
   int i=FindRoute(n->id);
   if(i==-1)
      return;
   routes[i]->RemoveNode(n);
}
void DHT::AddRoute(const Node *n)
{
   int i=FindRoute(n->id);
   if(i==-1) {
      assert(routes.count()==0);
      routes.append(new RouteBucket(0,xstring::null));
      i=0;
   }
try_again:
   const Ref<RouteBucket>& r=routes[i];
   // remove a non-good node to free space
   if(r->nodes.count()>=K) {
      for(int j=0; j<r->nodes.count(); j++) {
	 if(!r->nodes[j]->IsGood()) {
	    r->nodes.remove(j);
	    break;
	 }
      }
   }
   // prefer responded nodes
   if(r->nodes.count()>=K && n->responded) {
      for(int j=0; j<r->nodes.count(); j++) {
	 if(!r->nodes[j]->responded) {
	    r->nodes.remove(j);
	    break;
	 }
      }
   }
   if(r->nodes.count()>=K && i==0 && r->prefix_bits<160) {
      // split the bucket
      int bits=r->prefix_bits;
      size_t byte=bits/8;
      unsigned mask = 1<<(7-bits%8);
      if(r->prefix.length()<=byte)
	 r->prefix.append('\0');
      xstring p1(r->prefix.copy());
      xstring p2(r->prefix.copy());
      p1.get_non_const()[byte]&=~mask;
      p2.get_non_const()[byte]|=mask;
      RouteBucket *b1=new RouteBucket(bits+1,p1);
      RouteBucket *b2=new RouteBucket(bits+1,p2);
      for(int j=0; j<r->nodes.count(); j++) {
	 if(r->nodes[j]->id[byte]&mask)
	    b2->nodes.append(r->nodes[j]);
	 else
	    b1->nodes.append(r->nodes[j]);
      }
      if(node_id[byte]&mask) {
	 routes[0]=b2;
	 routes.insert(b1,1);
	 i=(n->id[byte]&mask)?0:1;
      } else {
	 routes[0]=b1;
	 routes.insert(b2,1);
	 i=(n->id[byte]&mask)?1:0;
      }
      LogNote(9,"splitted route bucket 0");
      LogNote(9,"new route[0] prefix=%s",routes[0]->to_string());
      LogNote(9,"new route[1] prefix=%s",routes[1]->to_string());
      assert(routes[0]->PrefixMatch(node_id));
      goto try_again;
   }
   r->SetFresh();
   r->RemoveNode(n); // move the node to end of the list
   if(r->nodes.count()>=K)
      return;
   LogNote(3,"adding node %s to route bucket %d (prefix=%s)",n->GetName(),i,r->to_string());
   r->nodes.append(n);
}
void DHT::RouteBucket::RemoveNode(const Node *n)
{
   for(int j=0; j<nodes.count(); j++) {
      if(nodes[j]==n) {
	 nodes.remove(j);
	 break;
      }
   }
}
bool DHT::RouteBucket::PrefixMatch(const xstring& id) const
{
   int bytes=prefix_bits/8;
   int bits=prefix_bits%8;
   unsigned mask=~((1<<(8-bits))-1);
   if(bytes>0 && memcmp(prefix.get(),id.get(),bytes))
      return false;
   if(bits>0 && (prefix[bytes]&mask)!=(id[bytes]&mask))
      return false;
   return true;
}
void DHT::AddGoodNodes(xarray<const Node*> &a,const xarray<const Node*> &nodes)
{
   for(int i=0; i<nodes.count(); i++)
      if(nodes[i]->IsGood())
	 a.append(nodes[i]);
}
void DHT::FindNodes(const xstring& target_id,xarray<const Node*> &a,int max_count)
{
   a.truncate();
   int i=FindRoute(target_id);
   if(i==-1)
      return;
   AddGoodNodes(a,routes[i]->nodes);
   while(a.count()<max_count && ++i<routes.count())
      AddGoodNodes(a,routes[i]->nodes);
   if(a.count()>max_count)
      a.set_length(K);
}
void DHT::AddPeer(const xstring& info_hash,const xstring& a,bool seed)
{
   xarray_p<Peer> *ps=peers.lookup(info_hash);
   if(!ps) {
      if(peers.count()>=MAX_TORRENTS) {
	 // remove random torrent
	 int r=random()/13%peers.count();
	 int i=0;
	 for(peers.each_begin(); ; peers.each_next(), i++) {
	    if(i==r) {
	       peers.remove(peers.each_key());
	       break;
	    }
	 }
      }
      ps=new xarray_p<Peer>();
      peers.add(info_hash,ps);
   }
   for(int i=0; i<ps->count(); i++) {
      Peer *p=(*ps)[i];
      if(p->compact_addr.eq(a)) {
	 ps->remove(i);
	 break;
      }
   }
   if(ps->count()>=MAX_PEERS)
      ps->remove(0);
   ps->append(new Peer(a,seed));
   LogNote(9,"DHT knows %d torrents and %d peers of info_hash=%s",peers.count(),ps->count(),info_hash.hexdump());
}

void DHT::MakeNodeId(xstring &id,const xstring& ip,int r)
{
   // http://www.rasterbar.com/products/libtorrent/dht_sec.html
   static char v4mask[] = { 0x01, 0x07, 0x1f, 0x7f };
   static char v6mask[] = { 0x00, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f };

   char *mask=(ip.length()==4?v4mask:v6mask);
   int len=(ip.length()==4?sizeof(v4mask):sizeof(v6mask));
   int i;

   xstring seed;
   for(i=0; i<len; i++)
      seed.append(char(ip[i] & mask[i]));

   seed.append(char(r&7));

   Torrent::SHA1(seed,id);

   for(i=4; i<19; i++)
      id.get_non_const()[i] = random()/13;
   id.get_non_const()[19] = r;
}
bool DHT::ValidNodeId(const xstring& id,const xstring& ip)
{
   if(id.length()!=20)
      return false;

   sockaddr_u addr;
   if(!addr.set_compact(ip))
      return false;

   if(addr.is_loopback() || addr.is_private())
      return true;

   xstring id1;
   MakeNodeId(id1,ip,id[19]);
   return !memcmp(id,id1,4);
}
void DHT::Restart()
{
   ip_voted.empty();
   ip_votes.empty();
   routes.truncate();
   // re-add known nodes to route buckets
   for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
      if(n->IsGood())
	 AddRoute(n);
   }
}
void DHT::Save(const SMTaskRef<IOBuffer>& buf)
{
   xmap_p<BeNode> state;
   state.add("id",new BeNode(node_id));
   xarray_p<BeNode> b_nodes;
   int responded_count=0;
   for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
      if(n->IsGood()) {
	 b_nodes.append(new BeNode(n->addr.compact()));
	 if(n->responded)
	    responded_count++;
      }
   }
   LogNote(9,"saving state, %d good nodes (%d responded)",b_nodes.count(),responded_count);
   state.add("nodes",new BeNode(&b_nodes));
   BeNode(&state).Pack(buf);
   for(int i=0; i<routes.count(); i++) {
      const RouteBucket *r=routes[i];
      LogNote(9,"route bucket %d: nodes count=%d prefix=%s",i,
	 (int)r->nodes.count(),r->to_string());
   }
}
void DHT::Load(const SMTaskRef<IOBuffer>& buf)
{
   int rest;
   Ref<BeNode> state(BeNode::Parse(buf->Get(),buf->Size(),&rest));
   if(!state || state->type!=BeNode::BE_DICT)
      return;
   const xstring& b_id=state->lookup_str("id");
   if(b_id.length()==20) {
      node_id.set(b_id);
      Restart();
   }
   BeNode *b_nodes=state->lookup("nodes",BeNode::BE_LIST);
   if(!b_nodes)
      return;
   for(int i=0; i<b_nodes->list.count(); i++) {
      BeNode *b_node=b_nodes->list[i];
      if(b_node->type!=BeNode::BE_STR)
	 continue;
      load_nodes.push(new xstring(b_node->str.copy()));
   }
}
void DHT::Save()
{
   if(!state_file)
      return;
   FileStream *f=new FileStream(state_file,O_WRONLY|O_TRUNC|O_CREAT);
   f->set_lock();
   f->set_create_mode(0600);
   state_io=new IOBufferFDStream(f,IOBuffer::PUT);
   Save(state_io);
   state_io->PutEOF();
}
void DHT::Load()
{
   if(!state_file)
      return;
   FileStream *f=new FileStream(state_file,O_RDONLY);
   f->set_lock();
   state_io=new IOBufferFDStream(f,IOBuffer::GET);
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
      if(parent->WaitsFor(this)) {
	 PrintStatus(1,"");
	 printf(_("Seeding in background...\n"));
	 parent->RemoveWaiting(this);
      }
      completed=true;
      return MOVED;
   }
   return STALL;
}

const char *TorrentTracker::Status() const
{
   if(!t_session)
      return "";
   if(t_session->IsOpen())
      return t_session->CurrentStatus();
   return xstring::format(_("next request in %s"),NextRequestIn());
}

xstring& TorrentJob::FormatStatus(xstring& s,int v,const char *tab)
{
   const char *name=torrent->GetName();
   if(name)
      s.appendf("%sName: %s\n",tab,name);
   s.appendf("%s%s\n",tab,torrent->Status().get());
   if(torrent->GetRatio()>0)
      s.appendf("%sratio: %f\n",tab,torrent->GetRatio());

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
   };
   static const struct option torrent_opts[]=
   {
      {"output-directory",required_argument,0,OPT_OUTPUT_DIRECTORY},
      {"force-valid",no_argument,0,OPT_FORCE_VALID},
      {"dht-bootstrap",required_argument,0,OPT_DHT_BOOTSTRAP},
      {0}
   };
   const char *output_dir=0;
   const char *dht_bootstrap=0;
   bool force_valid=false;

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
      case('?'):
      try_help:
	 eprintf(_("Try `help %s' for more information.\n"),args->a0());
	 return 0;
      }
   }
   args->back();

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
      if(!url::is_url(torrent)) {
	 glob_t pglob;
	 glob(expand_home_relative(torrent),0,0,&pglob);
	 if(pglob.gl_pathc>0) {
	    for(unsigned i=0; i<pglob.gl_pathc; i++) {
	       const char *f=pglob.gl_pathv[i];
	       struct stat st;
	       if(stat(f,&st)!=-1 && S_ISREG(st.st_mode)) {
		  args_g->Add(f);
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
      eprintf(_("%s: Please specify meta-info file or URL.\n"),args->a0());
      goto try_help;
   }
   while(torrent) {
      Torrent *t=new Torrent(torrent,cwd,output_dir);
      if(force_valid)
	 t->ForceValid();
      TorrentJob *tj=new TorrentJob(t);
      tj->cmdline.set(xstring::cat(torrent_opt," ",torrent,NULL));
      parent->AddNewJob(tj);
      torrent=args_g->getnext();
   }
   return 0;
#undef args
}

#include "modconfig.h"
#ifdef MODULE_CMD_TORRENT
void module_init()
{
   Torrent::ClassInit();
   CmdExec::RegisterCommand("torrent",cmd_torrent);
}
#endif
