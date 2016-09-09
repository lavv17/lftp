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

#include "Torrent.h"
#include "TorrentTracker.h"
#include "log.h"
#include "url.h"
#include "misc.h"
#include "plural.h"

void TorrentTracker::AddURL(const char *url)
{
   LogNote(4,"Tracker URL is `%s'",url);
   ParsedURL u(url,true);
   if(u.proto.ne("http") && u.proto.ne("https") && u.proto.ne("udp")) {
      LogError(1,"unsupported tracker protocol `%s', must be http, https or udp",u.proto.get());
      return;
   }
   xstring& tracker_url=*new xstring(url);
   if(u.proto.ne("udp")) {
      if(!u.path || !u.path[0])
	 tracker_url.append('/');
      // fix the URL: append either ? or & if missing.
      if(tracker_url.last_char()!='?' && tracker_url.last_char()!='&')
	 tracker_url.append(tracker_url.instr('?')>=0?'&':'?');
   }
   tracker_urls.append(&tracker_url);
}

TorrentTracker::TorrentTracker(Torrent *p,const char *url)
   : parent(p), current_tracker(0),
     tracker_timer(600), tracker_timeout_timer(120),
     started(false), tracker_no(0)
{
   AddURL(url);
}

bool TorrentTracker::IsActive() const
{
   return backend && backend->IsActive();
}
void TorrentTracker::Shutdown()
{
   if(Failed()) // don't stop a failed tracker
      return;
   // stop if have started or at least processing a start request
   if(started || IsActive())
      SendTrackerRequest("stopped");
}
void TorrentTracker::SetError(const char *e)
{
   if(tracker_urls.count()<=1)
      error=new Error(-1,e,true);
   else {
      LogError(3,"Tracker error: %s, using next tracker URL",e);
      tracker_urls.remove(current_tracker--);
      NextTracker();
      // retry immediately
      tracker_timer.Stop();
   }
}
bool TorrentTracker::AddPeerCompact(const char *compact_addr,int len) const
{
   sockaddr_u a;
   if(!a.set_compact(compact_addr,len))
      return false;
   Enter(parent);
   parent->AddPeer(new TorrentPeer(parent,&a,tracker_no));
   Leave(parent);
   return true;
}
bool TorrentTracker::AddPeer(const xstring& addr,int port) const
{
   sockaddr_u a;
#if INET6
   if(addr.instr(':')>=0) {
      a.sa.sa_family=AF_INET6;
      if(inet_pton(AF_INET6,addr,&a.in6.sin6_addr)<=0)
	 return false;
   } else
#endif
   {
      a.sa.sa_family=AF_INET;
      if(!inet_aton(addr,&a.in.sin_addr))
	 return false;
   }
   a.set_port(port);
   Enter(parent);
   parent->AddPeer(new TorrentPeer(parent,&a,tracker_no));
   Leave(parent);
   return true;
}
int TorrentTracker::Do()
{
   int m=STALL;
   if(Failed())
      return m;
   if(backend && backend->IsActive()) {
      if(tracker_timeout_timer.Stopped()) {
	 LogError(3,"Tracker timeout");
	 NextTracker();
	 return MOVED;
      }
   } else {
      if(tracker_timer.Stopped()) {
	 parent->CleanPeers();
	 SendTrackerRequest(0);
      }
   }
   return m;
}
void TorrentTracker::CreateTrackerBackend()
{
   backend=0;
   ParsedURL u(GetURL(),true);
   if(u.proto.eq("udp")) {
      backend=new UdpTracker(this,&u);
   } else if(u.proto.eq("http") || u.proto.eq("https")) {
      backend=new HttpTracker(this,&u);
   }
}
void TorrentTracker::NextTracker()
{
   current_tracker++;
   if(current_tracker>=tracker_urls.count())
      current_tracker=0;
   tracker_timer.Reset();

   CreateTrackerBackend();
}
void TorrentTracker::Start()
{
   if(backend || Failed())
      return;
   CreateTrackerBackend();
   SendTrackerRequest("started");
}
void TorrentTracker::SendTrackerRequest(const char *event)
{
   backend->SendTrackerRequest(event);
   tracker_timeout_timer.Reset();
}
const char *TorrentTracker::Status() const
{
   if(error)
      return error->Text();
   if(!backend)
      return _("not started");
   if(backend->IsActive())
      return backend->Status();
   return xstring::format(_("next request in %s"),NextRequestIn());
}


// TrackerBackend
const xstring& TrackerBackend::GetInfoHash() const { return master->parent->GetInfoHash(); }
const xstring& TrackerBackend::GetMyPeerId() const { return master->parent->GetMyPeerId(); }
int TrackerBackend::GetPort() const { return master->parent->GetPort(); }
unsigned long long TrackerBackend::GetTotalSent() const { return master->parent->GetTotalSent(); }
unsigned long long TrackerBackend::GetTotalRecv() const { return master->parent->GetTotalRecv(); }
unsigned long long TrackerBackend::GetTotalLeft() const { return master->parent->GetTotalLeft(); }
bool TrackerBackend::HasMetadata() const { return master->parent->HasMetadata(); }
bool TrackerBackend::Complete() const { return master->parent->Complete(); }
int TrackerBackend::GetWantedPeersCount() const { return master->parent->GetWantedPeersCount(); }
const xstring& TrackerBackend::GetMyKey() const { return master->parent->GetMyKey(); }
unsigned TrackerBackend::GetMyKeyNum() const { return master->parent->GetMyKeyNum(); }
const char *TrackerBackend::GetTrackerId() const { return master->tracker_id; }
bool TrackerBackend::ShuttingDown() const { return master->parent->ShuttingDown(); }
void TrackerBackend::Started() const { master->started=true; }
void TrackerBackend::TrackerRequestFinished() const { master->tracker_timer.Reset(); }

// HttpTracker
#define super TrackerBackend
int HttpTracker::HandleTrackerReply()
{
   if(tracker_reply->Error()) {
      SetError(tracker_reply->ErrorText());
      t_session->Close();
      tracker_reply=0;
      return MOVED;
   }
   if(!tracker_reply->Eof())
      return STALL;
   t_session->Close();
   int rest;
   Ref<BeNode> reply(BeNode::Parse(tracker_reply->Get(),tracker_reply->Size(),&rest));
   if(!reply) {
      LogError(3,"Tracker reply parse error (data: %s)",tracker_reply->Dump());
      tracker_reply=0;
      NextTracker();
      return MOVED;
   }
   LogNote(10,"Received tracker reply:");
   Log::global->Write(10,reply->Format());

   if(ShuttingDown()) {
      tracker_reply=0;
      t_session=0;
      return MOVED;
   }
   Started();

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
   if(b_interval)
      SetInterval(b_interval->num);
   SetTrackerID(reply->lookup_str("tracker id"));

   int peers_count=0;
   BeNode *b_peers=reply->lookup("peers");
   if(b_peers) {
      if(b_peers->type==BeNode::BE_STR) { // binary model
	 const char *data=b_peers->str;
	 int len=b_peers->str.length();
	 LogNote(9,"peers have binary model, length=%d",len);
	 while(len>=6) {
	    if(AddPeerCompact(data,6))
	       peers_count++;
	    data+=6;
	    len-=6;
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
	    if(AddPeer(b_ip->str,b_port->num))
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
	 if(AddPeerCompact(data,18))
	    peers_count++;
	 data+=18;
	 len-=18;
      }
      LogNote(4,plural("Received valid info about %d IPv6 peer$|s$",peers_count),peers_count);
   }
#endif//INET6
   tracker_reply=0;
   TrackerRequestFinished();
   return MOVED;
}

int HttpTracker::Do()
{
   int m=STALL;
   if(!IsActive())
      return m;
   if(tracker_reply)
      m|=HandleTrackerReply();
   return m;
}

void HttpTracker::SendTrackerRequest(const char *event)
{
   if(!t_session)
      return;

   xstring request(GetURL());
   request.appendf("info_hash=%s",url::encode(GetInfoHash(),URL_PATH_UNSAFE).get());
   request.appendf("&peer_id=%s",url::encode(GetMyPeerId(),URL_PATH_UNSAFE).get());
   request.appendf("&port=%d",GetPort());
   request.appendf("&uploaded=%llu",GetTotalSent());
   request.appendf("&downloaded=%llu",GetTotalRecv());
   request.appendf("&left=%llu",HasMetadata()?GetTotalLeft():123456789ULL);
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

   int numwant=GetWantedPeersCount();
   if(numwant>=0)
      request.appendf("&numwant=%d",numwant);
   const xstring& my_key=GetMyKey();
   if(my_key)
      request.appendf("&key=%s",my_key.get());
   const char *tracker_id=GetTrackerId();
   if(tracker_id)
      request.appendf("&trackerid=%s",url::encode(tracker_id,URL_PATH_UNSAFE).get());
   LogSend(4,request);
   t_session->Open(url::path_ptr(request),FA::RETRIEVE);
   t_session->SetFileURL(request);
   tracker_reply=new IOBufferFileAccess(t_session);
}


// UdpTracker
int UdpTracker::Do()
{
   int m=STALL;
   if(!peer) {
      // need to resolve addresses
      if(!resolver) {
	 resolver=new Resolver(hostname,portname,"80");
	 resolver->Roll();
	 m=MOVED;
      }
      if(!resolver->Done())
	 return m;
      if(resolver->Error())
      {
	 SetError(resolver->ErrorMsg());
	 return(MOVED);
      }
      peer.set(resolver->Result());
      peer_curr=0;
      resolver=0;
      try_number=0;
      m=MOVED;
   }
   if(!IsActive())
      return m;
   if(sock==-1) {
      // need to create the socket
      sock=SocketCreate(peer[peer_curr].family(),SOCK_DGRAM,IPPROTO_UDP,hostname);
      if(sock==-1) {
	 int saved_errno=errno;
	 LogError(9,"socket: %s",strerror(saved_errno));
	 if(NonFatalError(saved_errno))
	    return m;
	 xstring& str=xstring::format(_("cannot create socket of address family %d"),
		     peer[peer_curr].family());
	 str.appendf(" (%s)",strerror(saved_errno));
	 SetError(str);
	 return MOVED;
      }
   }
   if(current_action!=a_none) {
      if(!RecvReply()) {
	 if(timeout_timer.Stopped()) {
	    LogError(3,"request timeout");
	    NextPeer();
	    return MOVED;
	 }
	 return m;
      }
      return MOVED;
   }
   if(!has_connection_id) {
      // need to get connection id
      SendConnectRequest();
      return MOVED;
   }
   SendEventRequest();
   return MOVED;
}

void UdpTracker::NextPeer() {
   current_action=a_none;
   has_connection_id=false;
   connection_id=0;
   int old_peer=peer_curr;
   peer_curr++;
   if(peer_curr>=peer.count()) {
      peer_curr=0;
      try_number++;
   }
   // check if we need to create a socket of different address family
   if(old_peer!=peer_curr && peer[old_peer].family()!=peer[peer_curr].family()) {
      close(sock);
      sock=-1;
   }
}

bool UdpTracker::RecvReply() {
   if(!Ready(sock,POLLIN)) {
      Block(sock,POLLIN);
      return false;
   }
   Buffer buf;
   const int max_len=0x1000;
   sockaddr_u addr;
   socklen_t addr_len=sizeof(addr);
   int len=recvfrom(sock,buf.GetSpace(max_len),max_len,0,&addr.sa,&addr_len);
   if(len<0) {
      int saved_errno=errno;
      if(NonFatalError(saved_errno)) {
	 Block(sock,POLLIN);
	 return false;
      }
      SetError(xstring::format("recvfrom: %s",strerror(saved_errno)));
      return false;
   }
   if(len==0) {
      SetError("recvfrom: EOF?");
      return false;
   }
   buf.SpaceAdd(len);
   LogRecv(10,xstring::format("got a packet from %s of length %d {%s}",addr.to_string(),len,buf.Dump()));
   if(len<16) {
      LogError(9,"ignoring too short packet");
      return false;
   }
   unsigned tid=buf.UnpackUINT32BE(4);
   if(tid!=transaction_id) {
      LogError(9,"ignoring mismatching transaction packet (0x%08X!=0x%08X)",tid,transaction_id);
      return false;
   }
   int action=buf.UnpackUINT32BE(0);
   if(action!=current_action && action!=a_error) {
      LogError(9,"ignoring mismatching action packet (%d!=%d)",action,current_action);
      return false;
   }
   switch(action) {
   case a_none:
      abort();
   case a_connect:
      connection_id=buf.UnpackUINT64BE(8);
      has_connection_id=true;
      LogNote(9,"connected");
      break;
   case a_announce:
   case a_announce6:
   {
      SetInterval(buf.UnpackUINT32BE(8));
      if(buf.Size()<20)
	 break;
      unsigned leachers=buf.UnpackUINT32BE(12);
      unsigned seeders=buf.UnpackUINT32BE(16);
      LogNote(9,"leechers=%u seeders=%u",leachers,seeders);
      int peers_count=0;
      int compact_addr_size=6;
      if(current_action==a_announce6)
	 compact_addr_size=18;
      for(int i=20; i+compact_addr_size<=buf.Size(); i+=compact_addr_size) {
	 if(AddPeerCompact(buf.Get()+i,compact_addr_size))
	    peers_count++;
      }
      LogNote(4,plural("Received valid info about %d peer$|s$",peers_count),peers_count);
      current_event=ev_idle;
      TrackerRequestFinished();
      break;
   }
   case a_scrape:
      // not implemented
      break;
   case a_error:
      SetError(buf.Get()+8);
      break;
   }
   current_action=a_none;
   try_number=0;
   return true;
}

bool UdpTracker::SendPacket(Buffer& req)
{
   LogSend(10,xstring::format("sending a packet to %s of length %d {%s}",peer[peer_curr].to_string(),req.Size(),req.Dump()));
   int len=sendto(sock,req.Get(),req.Size(),0,&peer[peer_curr].sa,peer[peer_curr].addr_len());
   if(len<0) {
      int saved_errno=errno;
      if(NonFatalError(saved_errno)) {
	 Block(sock,POLLOUT);
	 return false;
      }
      SetError(xstring::format("sendto: %s",strerror(saved_errno)));
      return false;
   }
   if(len<req.Size()) {
      LogError(9,"could not send complete datagram of size %d",req.Size());
      Block(sock,POLLOUT);
      return false;
   }
   timeout_timer.Set(60*(1<<try_number));
   return true;
}

bool UdpTracker::SendConnectRequest()
{
   LogNote(9,"connecting...");
   Buffer req;
   req.PackUINT64BE(connect_magic);
   req.PackUINT32BE(a_connect);
   req.PackUINT32BE(NewTransactionId());
   if(!SendPacket(req))
      return false;
   current_action=a_connect;
   return true;
}

const char *UdpTracker::EventToString(event_t e)
{
   const char *map[]={
      "",
      "completed",
      "started",
      "stopped",
   };
   if(e>=0 && e<=3)
      return map[e];
   return "???";
}

bool UdpTracker::SendEventRequest()
{
   action_t action=a_announce;
   const char *a_name="announce";
#if INET6
   if(peer[peer_curr].family()==AF_INET6) {
      action=a_announce6;
      a_name="announce6";
   }
#endif
   LogNote(9,"%s %s",a_name,EventToString(current_event));
   assert(has_connection_id);
   assert(current_event!=ev_idle);
   Buffer req;
   req.PackUINT64BE(connection_id);
   req.PackUINT32BE(action);
   req.PackUINT32BE(NewTransactionId());
   req.Append(GetInfoHash());
   req.Append(GetMyPeerId());
   req.PackUINT64BE(GetTotalRecv());
   req.PackUINT64BE(GetTotalLeft());
   req.PackUINT64BE(GetTotalSent());
   req.PackUINT32BE(current_event);

#if INET6
   if(action==a_announce6) {
      const char *ip=ResMgr::Query("torrent:ipv6",0);
      char ip_packed[16];
      memset(ip_packed,0,16);
      if(ip && ip[0])
	 inet_pton(AF_INET6,ip,ip_packed);
      req.Append(ip_packed,16);
   } else
#endif
   {
      const char *ip=ResMgr::Query("torrent:ip",0);
      char ip_packed[4];
      memset(ip_packed,0,4);
      if(ip && ip[0])
	 inet_pton(AF_INET,ip,ip_packed);
      req.Append(ip_packed,4);
   }

   req.PackUINT32BE(GetMyKeyNum());
   req.PackUINT32BE(GetWantedPeersCount());
   req.PackUINT16BE(GetPort());

   if(!SendPacket(req))
      return false;
   current_action=action;
   return true;
}

const char *UdpTracker::Status() const
{
   if(resolver)
      return(_("Resolving host address..."));
   if(!has_connection_id)
      return(_("Connecting..."));
   if(current_action!=a_none)
      return _("Waiting for response...");
   return "";
}

void UdpTracker::SendTrackerRequest(const char *event)
{
   current_event=ev_none;
   if(!event)
      return;
   if(!strcmp(event,"started"))
      current_event=ev_started;
   else if(!strcmp(event,"stopped"))
      current_event=ev_stopped;
   else if(!strcmp(event,"completed"))
      current_event=ev_completed;
}
