/*
 * lftp - file transfer program
 *
 * Copyright (c) 2012-2016 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include "Torrent.h"
#include "DHT.h"
#include "log.h"
#include "url.h"
#include "misc.h"
#include "plural.h"

DHT::DHT(int af,const xstring& id)
   : af(af), rate_limit("DHT"),
     sent_req_expire_scan(5), search_cleanup_timer(5),
     refresh_timer(1), nodes_cleanup_timer(30), save_timer(300),
     node_id(id.copy()), t(random())
{
   LogNote(10,"creating DHT with id=%s",node_id.hexdump());
   Reconfig(0);
}
DHT::~DHT()
{
}
void DHT::Bootstrap()
{
   // run bootstrap search
   LogNote(9,"bootstrapping");
   Search *s=new Search(node_id);
   s->Bootstrap();
   StartSearch(s);
}
int DHT::Do()
{
   int m=STALL;
   if(state_io) {
      if(state_io->GetDirection()==IOBuffer::GET) {
	 if(state_io->Error()) {
	    LogError(1,"loading state: %s",state_io->ErrorText());
	    state_io=0;
	    m=MOVED;
	 } else if(state_io->Eof()) {
	    Load(state_io);
	    state_io=0;
	    m=MOVED;
	 }
      } else {
	 if(state_io->Error())
	    LogError(1,"saving state: %s",state_io->ErrorText());
	 if(state_io->Done()) {
	    state_io=0;
	    m=MOVED;
	 }
      }
   }
   if(sent_req_expire_scan.Stopped()) {
      for(const Request *r=sent_req.each_begin(); r; r=sent_req.each_next()) {
	 if(!r->Expired())
	    continue;
	 Ref<Request> rr(sent_req.borrow(sent_req.each_key()));
	 LogError(4,"DHT request %s to %s timed out",r->data->lookup_str("q").get(),r->addr.to_string());
	 Node *n=nodes.lookup(r->GetNodeId());
	 if(n) {
	    n->LostPing();
	    LogNote(4,"DHT node %s has lost %d packets",n->GetName(),n->ping_lost_count);
	 }
	 const xstring& target=r->GetSearchTarget();
	 if(target) {
	    Search *s=search.lookup(target);
	    // if we have lost a search request and have not received any
	    // reply yet - try to restart the search with good nodes.
	    if(s && !s->best_node_id)
	       RestartSearch(s);
	 }
      }
      sent_req_expire_scan.Reset();
   }
   if(search_cleanup_timer.Stopped()) {
      for(Search *s=search.each_begin(); s; s=search.each_next()) {
	 if(s->search_timer.Stopped())
	    search.remove(search.each_key());
      }
      search_cleanup_timer.Reset();
   }
   if(nodes_cleanup_timer.Stopped()) {
      for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
	 if(n->IsBad()) {
	    LogNote(9,"removing bad node %s",n->GetName());
	    RemoveNode(n);
	 }
      }
      if(nodes.count()>MAX_NODES) {
	 // remove some nodes.
	 int to_remove=nodes.count()-MAX_NODES;
	 for(Node *n=nodes.each_begin(); n && to_remove>0; n=nodes.each_next()) {
	    if(!n->IsGood() && !n->in_routes) {
	       LogNote(9,"removing node %s (not good)",n->GetName());
	       RemoveNode(n);
	       to_remove--;
	    }
	 }
	 for(Node *n=nodes.each_begin(); n && to_remove>0; n=nodes.each_next()) {
	    if(!n->in_routes && !n->responded) {
	       LogNote(9,"removing node %s (never responded)",n->GetName());
	       RemoveNode(n);
	       to_remove--;
	    }
	 }
	 LogNote(9,"node count=%d",nodes.count());
      }
      for(int i=1; i<routes.count(); i++) {
	 if(routes[i]->nodes.count()>K) {
	    xarray<Node*>& nodes=routes[i]->nodes;
	    int q_num=PingQuestionable(nodes,nodes.count()-K);
	    if(nodes.count()>K+q_num)
	       routes[i]->RemoveNode(K); // too many candidates, trim one
	 }
      }
      // remove bad peers
      for(KnownTorrent *t=torrents.each_begin(); t; t=torrents.each_next()) {
	 xarray_p<Peer>& p=t->peers;
	 int seeds=0;
	 for(int i=0; i<p.count(); i++) {
	    if(!p[i]->IsGood())
	       p.remove(i--);
	    else
	       seeds+=p[i]->seed;
	 }
	 LogNote(9,"torrent %s has %d known peers (%d seeds)",
	    torrents.each_key().hexdump(),p.count(),seeds);
	 if(p.count()==0)
	    torrents.remove(torrents.each_key());
      }
      nodes_cleanup_timer.Reset();
      if(save_timer.Stopped()) {
	 Save();
	 save_timer.Reset();
      }
      if(nodes.count()>0 && routes[0]->nodes.count()<2 && search.count()==0)
	 Bootstrap(); // lost almost all nodes, try to bootstrap again
   }
   if(refresh_timer.Stopped()) {
      for(int i=0; i<routes.count(); i++) {
	 if(!routes[i]->IsFresh()) {
	    LogNote(9,"refreshing route bucket %d (prefix=%s)",i,routes[i]->to_string());
	    // make random id in the range
	    int bytes=routes[i]->prefix_bits/8;
	    int bits=routes[i]->prefix_bits%8;
	    xstring random_id(routes[i]->prefix.get(),bytes+(bits>0));
	    if(bits>0) {
	       unsigned mask=(1<<(8-bits))-1;
	       assert(!(random_id[bytes]&mask));
	       random_id.get_non_const()[bytes]|=(random()/13)&mask;
	    }
	    while(random_id.length()<20)
	       random_id.append(char(random()/13));
	    StartSearch(new Search(random_id));
	    routes[i]->fresh_timer.Reset();
	 }
      }
      refresh_timer.Reset();
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

   while(send_queue.count()>0 && MaySendMessage()) {
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
   if(send_queue.count()>MAX_SEND_QUEUE) {
      LogError(9,"tail dropping output message");
      delete q;
      return;
   }
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
   if(res!=-1 && q->lookup_str("y").eq("q")) {
      sent_req.add(q->lookup_str("t"),req);
      rate_limit.BytesPut(res);
   } else {
      delete req;
   }
}
bool DHT::MaySendMessage()
{
   return rate_limit.BytesAllowedToPut()>=256
      && Torrent::GetUDPSocket(af)->MaySendUDP();
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
void DHT::SendPing(Node *n)
{
   SendPing(n->addr,n->id);
   n->ping_timer.Reset();
}
void DHT::AnnouncePeer(const Torrent *t)
{
   const xstring& info_hash=t->GetInfoHash();
   // check for duplicated announce
   if(search.exists(info_hash))
      return;
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
void DHT::DenouncePeer(const Torrent *t)
{
   // remove any search for this torrent
   search.remove(t->GetInfoHash());
}
int DHT::AddNodesToReply(xmap_p<BeNode> &r,const xstring& target,int max_count)
{
   xarray<Node*> n;
   FindNodes(target,n,max_count,true);
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
const xstring& DHT::Node::GetToken()
{
   if(!my_token || token_timer.Stopped()) {
      // make new token
      my_last_token.set(my_token);
      my_token.truncate();
      for(int i=0; i<16; i++)
	 my_token.append(char(random()/13));
      token_timer.Reset();
   }
   return my_token;
}
bool DHT::Node::TokenIsValid(const xstring& token) const
{
   if(!token || !my_token || token_timer.Stopped())
      return false;
   return token.eq(my_token) || token.eq(my_last_token);
}
const xstring& DHT::Request::GetSearchTarget() const
{
   const BeNode *a=data->lookup("a",BeNode::BE_DICT);
   if(!a)
      return xstring::null;
   const xstring& q=data->lookup_str("q");
   const char *target=q.eq("find_node")?"target":"info_hash";
   return a->lookup_str(target);
}
void DHT::HandlePacket(BeNode *p,const sockaddr_u& src)
{
   LogRecv(4,xstring::format("received DHT %s from %s %s",MessageType(p),
      src.to_string(),p->Format1()));
   int pkt_len=p->str.length();
   const xstring& t=p->lookup_str("t");
   if(!t)
      return;
   const xstring& y=p->lookup_str("y");
   if(!y)
      return;
   if(y.eq("q")) { // query
      if(rate_limit.BytesAllowedToGet()<pkt_len) {
	 LogError(9,"dropping incoming message (rate limit exceeded)");
	 return;
      }
      rate_limit.BytesGot(pkt_len);
      const xstring& q=p->lookup_str("q");
      if(!q)
	 return;
      BeNode *a=p->lookup("a",BeNode::BE_DICT);
      if(!a)
	 return;
      const xstring& id=a->lookup_str("id");
      if(id.length()!=20)
	 return;
      Node *node=FoundNode(id,src,false);
      if(!node)
	 return;

      xmap_p<BeNode> r;
      if(src.family()==AF_INET)
	 r.add("ip",new BeNode(src.compact_addr()));

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
	 KnownTorrent *torrent=torrents.lookup(info_hash);
	 int nodes_count=0;
	 int values_count=0;
	 if(!torrent || torrent->peers.count()==0) {
	    nodes_count=AddNodesToReply(r,info_hash,want_n4,want_n6);
	 } else {
	    xarray_p<Peer>& p=torrent->peers;
	    xarray_p<BeNode> values;
	    for(int i=0; i<p.count() && values_count<K; i++) {
	       Peer *peer=p[i];
	       if(noseed && peer->seed)
		  continue;
	       if(!peer->IsGood())
		  continue;
	       if(peer->compact_addr.family()==AF_INET && !want_n4)
		  continue;
#if INET6
	       if(peer->compact_addr.family()==AF_INET6 && !want_n6)
		  continue;
#endif
	       values.append(new BeNode(peer->compact_addr));
	       values_count++;
	    }
	    if(values_count>0)
	       r.add("values",new BeNode(&values));
	    else
	       nodes_count=AddNodesToReply(r,info_hash,want_n4,want_n6);
	 }
	 r.add("token",new BeNode(node->GetToken()));
	 LogSend(5,xstring::format("DHT get_peers reply with %d values and %d nodes to %s",
	    values_count,nodes_count,src.to_string()));
	 SendMessage(NewReply(t,r),src);
      } else if(q.eq("announce_peer")) {
	 // need a valid token
	 if(!node->TokenIsValid(a->lookup_str("token"))) {
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
      } else if(q.eq("vote")) {
#if 0
	 // need a valid token
	 if(!node->TokenIsValid(a->lookup_str("token"))) {
	    SendMessage(NewError(t,ERR_PROTOCOL,"invalid token"),src);
	    return;
	 }
	 // target is sha1(info_hash+"rating")
	 const xstring& target=a->lookup_str("target");
	 if(target.length()!=20)
            return;
         unsigned vote=a->lookup_int("vote");
	 // store the vote
	 // return what?
	 SendMessage(NewReply(t,r),src);
#endif
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
   if(req->addr!=src) {
      LogError(2,"got DHT reply from %s instead of %s",src.to_string(),req->addr.to_string());
      return;
   }

   const xstring& q=req->data->lookup_str("q");
   if(y.eq("r")) { // reply
      BeNode *r=p->lookup("r",BeNode::BE_DICT);
      if(!r)
	 return;
      const xstring& id=r->lookup_str("id");
      if(id.length()!=20)
	 return;

      Node *node=FoundNode(id,src,true);
      if(!node)
	 return;

      const sockaddr_compact& ip=sockaddr_compact::cast(r->lookup_str("ip"));
      if(ip && !ValidNodeId(node_id,ip)) {
	 const xstring &src_ip=xstring::get_tmp(src.address());
	 if(src_ip.eq(ip.address())) {
	    LogError(2,"%s incorrectly reported our IP as %s",src.to_string(),ip.address());
	    black_list.Add(src,"1d");
	    return;
	 } else if(!ip_voted.lookup(src_ip)) {
	    ip_voted.add(src_ip,true);
	    unsigned& votes=ip_votes.lookup_Lv(ip);
	    votes++;
	    LogNote(2,"%s reported our IP as %s (votes=%u)",src.to_string(),ip.address(),votes);
	    if(votes>=4) {
	       // we have incorrect node_id, restart with correct one.
	       MakeNodeId(node_id,ip);
	       LogNote(0,"restarting DHT with new id %s",node_id.hexdump());
	       Restart();
	    }
	 }
      }

      if(q.eq("get_peers")) {
	 const xstring& info_hash=req->GetSearchTarget();
	 Torrent *torrent=Torrent::FindTorrent(info_hash);
	 BeNode *values=r->lookup("values",BeNode::BE_LIST);
	 if(values) {
	    // some peers found.
	    for(int i=0; i<values->list.count(); i++) {
	       if(values->list[i]->type!=BeNode::BE_STR)
		  continue;
	       const sockaddr_compact &c=sockaddr_compact::cast(values->list[i]->str);
	       sockaddr_u a(c);
	       if(!a.port())
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
	    torrent->DHT_Announced(src.family());
	 }
      }
      if(q.eq("find_node") || q.eq("get_peers")) {
	 const xstring& target=req->GetSearchTarget();
	 const xstring &node_id=req->GetNodeId();
	 LogNote(9,"got reply for %s with target %s from node %s",q.get(),target.hexdump(),node_id.hexdump());

	 Search *s=search.lookup(target);
	 if(s && s->IsFeasible(node_id)) {
	    s->best_node_id.set(node_id);
	    s->depth++;
	    LogNote(9,"search for %s goes to depth=%d with best_node_id=%s",
	       s->target_id.hexdump(),s->depth,node_id.hexdump());
	 }

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
	       Node *new_node=FoundNode(id,a,false,s);
	       if(new_node)
		  new_node->SetOrigin(node);
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
	       Node *new_node=FoundNode(id,a,false,s);
	       if(new_node)
		  new_node->SetOrigin(node);
	    }
	 }
#endif //INET6
      }
   } else if(y.eq("e")) { // error
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

bool DHT::Search::IsFeasible(const xstring &id) const
{
   if(!best_node_id)
      return true;
   for(int i=0; i<20; i++) {
      unsigned char a=id[i]^target_id[i];
      unsigned char b=best_node_id[i]^target_id[i];
      if(a<b)
	 return true;
      if(a>b)
	 return false;
   }
   return false;
}

void DHT::Search::ContinueOn(DHT *d,const Node *n)
{
   if(searched.exists(n->id)) {
      LogNote(9,"skipping search on %s, already searched",n->GetName());
      return;
   }
   LogNote(3,"search for %s continues on %s (%s) depth=%d",
      target_id.hexdump(),n->id.hexdump(),n->GetName(),depth);

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
   searched.add(n->id,true);
   search_timer.Reset();
}

void DHT::StartSearch(Search *s)
{
   LogNote(9,"starting search for %s",s->target_id.hexdump());
   xarray<Node*> n;
   FindNodes(s->target_id,n,K,true);
   if(n.count()<=K/2) {
      LogNote(2,"too few good nodes found in the routing table");
      FindNodes(s->target_id,n,K,false);
      if(n.count()==0)
	 LogError(1,"no nodes found in the routing table");
   }
   for(int i=0; i<n.count(); i++)
      s->ContinueOn(this,n[i]);
   search.add(s->target_id,s);
}

void DHT::RestartSearch(Search *s)
{
   xarray<Node*> n;
   FindNodes(s->target_id,n,K,true,&s->searched);
   for(int i=0; i<n.count(); i++)
      s->ContinueOn(this,n[i]);
}

DHT::Node *DHT::GetOrigin(const Node *n) {
   if(!n->origin_id)
      return 0;
   Node *origin=nodes.lookup(n->origin_id);
   if(!origin || origin==n)
      return 0;
   return origin;
}

DHT::Node *DHT::FoundNode(const xstring& id,const sockaddr_u& a,bool responded,Search *s)
{
   if(a.port()==0 || a.is_private() || a.is_reserved() || a.is_multicast()) {
      LogError(9,"node address %s is not valid",a.to_string());
      return 0;
   }

   if(a.family()!=af)
      return 0;

   if(id.eq(node_id)) {
      LogNote(9,"node %s has our own id",a.to_string());
      return 0;
   }

   if(black_list.Listed(a)) {
      LogNote(9,"node %s is blacklisted",a.to_string());
      return 0;
   }

   Node *n=nodes.lookup(id);
   if(!n) {
      n=node_by_addr.lookup(a.compact());
      if(n) {
	 if(!responded) {
	    // change node id only by a message from the node.
	    return 0;
	 }
	 if(n->id_change_count>0) {
	    LogError(9,"%s changes node id again",n->addr.to_string());
	    BlackListNode(n,"1d");
	    return 0;
	 }
	 ChangeNodeId(n,id);
      } else {
	 n=new Node(id,a);
	 AddNode(n);
      }
   } else {
      AddRoute(n);
   }

   if(responded) {
      n->responded=true;
      n->ResetLostPing();
      Node *origin=GetOrigin(n);
      if(origin)
	 origin->bad_node_count/=2;
   }
   if(n->responded)
      n->SetGood();

   // continue search
   if(s && s->IsFeasible(n))
      s->ContinueOn(this,n);

   return n;
}
void DHT::RemoveNode(Node *n)
{
   Node *origin=GetOrigin(n);
   if(origin && !n->responded && n->ping_lost_count>1) {
      origin->bad_node_count++;
      if(origin->bad_node_count>2*K)
	 BlackListNode(origin,"1h");
   }
   RemoveRoute(n);
   node_by_addr.remove(n->addr.compact());
   nodes.remove(n->id);
}
void DHT::ChangeNodeId(Node *n,const xstring& new_node_id)
{
   LogNote(1,"node_id changed for %s, old_node_id=%s, new_node_id=%s",
      n->GetName(),n->id.hexdump(),new_node_id.hexdump());
   n->id_change_count++;

   // change node_id in the in-flight requests
   for(Request *r=sent_req.each_begin(); r; r=sent_req.each_next()) {
      if(r->node_id.eq(n->id) && r->addr==n->addr)
	 r->node_id.set(new_node_id);
   }

   RemoveRoute(n);
   nodes.borrow(n->id);	// borrow to avoid freeing the node
   n->id.set(new_node_id);
   nodes.add(n->id,n);
   AddRoute(n);
}
void DHT::BlackListNode(Node *n,const char *timeout)
{
   black_list.Add(n->addr,timeout);
   for(int i=0; i<send_queue.count(); i++) {
      if(send_queue[i]->node_id.eq(n->id))
	 send_queue.remove(i);
   }
   for(const Request *r=sent_req.each_begin(); r; r=sent_req.each_next()) {
      if(r->GetNodeId().eq(n->id))
	 sent_req.remove(sent_req.each_key());
   }
   RemoveNode(n);
}

void DHT::AddNode(Node *n)
{
   assert(n->id.length()==20);
   assert(!nodes.exists(n->id));
   assert(!node_by_addr.exists(n->addr.compact()));

   nodes.add(n->id,n);
   node_by_addr.add(n->addr.compact(),n);

   AddRoute(n);

   if(nodes.count()==1 && search.count()==0 && !state_io)
      Bootstrap();
}
int DHT::FindRoute(const xstring& id,int i,int skew)
{
   // routes are ordered by prefix length decreasing
   // the first route bucket always matches our node_id
   for( ; i<routes.count(); i++) {
      if(routes[i]->PrefixMatch(id,skew)) {
	 return i;
      }
   }
   return -1;
}
void DHT::RemoveRoute(Node *n)
{
   int i=FindRoute(n->id);
   if(i==-1)
      return;
   routes[i]->RemoveNode(n);
}
void DHT::AddRoute(Node *n)
{
try_again:
   int i=FindRoute(n->id);
   if(i==-1) {
      assert(routes.count()==0);
      routes.append(new RouteBucket(0,xstring::null));
      i=0;
   }
   const Ref<RouteBucket>& r=routes[i];
   xarray<Node*> &nodes=r->nodes;
   // check if the node is already in the bucket
   for(int j=0; j<nodes.count(); j++) {
      if(nodes[j]==n) {
	 // K nodes are stable, other nodes are candidates
	 if(j<K) {
	    // move the stable node to the end of K set
	    // and set the bucket as fresh.
	    r->SetFresh();
	    nodes.remove(j);
	    if(nodes.count()<K)
	       nodes.append(n);
	    else
	       nodes.insert(n,K-1);
	    // remove a candidate
	 }
	 return;
      }
   }
   // remove a bad node to free space
   if(nodes.count()>=K) {
      for(int j=0; j<nodes.count(); j++) {
	 if(nodes[j]->IsBad()) {
	    r->RemoveNode(j);
	    break;
	 }
      }
   }
   // prefer responded nodes
   if(i>0 && nodes.count()>=K && n->responded) {
      for(int j=0; j<nodes.count(); j++) {
	 if(!nodes[j]->responded) {
	    r->RemoveNode(j);
	    break;
	 }
      }
   }
   // remove a non-good and not responded node to free space
   if(i>0 && nodes.count()>=K) {
      for(int j=0; j<nodes.count(); j++) {
	 if(!nodes[j]->IsGood() && !nodes[j]->responded) {
	    r->RemoveNode(j);
	    break;
	 }
      }
   }

   if(state_io && i==0 && nodes.count()>=K && SplitRoute0())
      goto try_again;

   if(nodes.count()>=K) {
      int q_num=PingQuestionable(nodes,nodes.count()-K+1);
      // check if we have already candidates for the questionable nodes
      if(nodes.count()>=K+q_num) {
	 if(i==0 && SplitRoute0())
	    goto try_again;
	 else
	    LogNote(9,"skipping node %s, route bucket %d (prefix=%s) has %d nodes",n->GetName(),i,r->to_string(),nodes.count());
	 return;
      }
   }
   r->SetFresh();
   LogNote(3,"adding node %s to route bucket %d (prefix=%s)",n->GetName(),i,r->to_string());
   n->in_routes=true;
   nodes.append(n);
}
bool DHT::SplitRoute0()
{
   const Ref<RouteBucket>& r=routes[0];
   xarray<Node*> &nodes=r->nodes;

   if(nodes.count()<K || r->prefix_bits>=160)
      return false;
   if(routes.count()>1 && !routes[1]->HasGoodNodes() && !state_io)
      return false;

   LogNote(9,"splitting route bucket 0, nodes=%d",nodes.count());
   int bits=r->prefix_bits;
   size_t byte=bits/8;
   unsigned mask = 1<<(7-bits%8);
   if(r->prefix.length()<=byte)
      r->prefix.append('\0');
   xstring p1(r->prefix.copy());
   xstring p2(r->prefix.copy());
   // new bit in p1 is already cleared.
   p2.get_non_const()[byte]|=mask; // set new bit in p2
   RouteBucket *b1=new RouteBucket(bits+1,p1);
   RouteBucket *b2=new RouteBucket(bits+1,p2);
   // distribute the nodes between two buckets
   for(int j=0; j<nodes.count(); j++) {
      if(nodes[j]->id[byte]&mask)
	 b2->nodes.append(nodes[j]);
      else
	 b1->nodes.append(nodes[j]);
   }
   if(node_id[byte]&mask) {
      routes[0]=b2;
      routes.insert(b1,1);
   } else {
      routes[0]=b1;
      routes.insert(b2,1);
   }
   LogNote(9,"new route[0] prefix=%s nodes=%d",routes[0]->to_string(),routes[0]->nodes.count());
   LogNote(9,"new route[1] prefix=%s nodes=%d",routes[1]->to_string(),routes[1]->nodes.count());
   assert(routes[0]->PrefixMatch(node_id));
   return true;
}
int DHT::PingQuestionable(const xarray<Node*>& nodes,int limit)
{
   int q_num=0;
   // ping questionable nodes, return the number of questionable nodes
   for(int j=0; j<nodes.count() && j<K && q_num<limit; j++) {
      Node *n=nodes[j];
      if(!n->IsGood()) {
	 q_num++;
	 if(n->ping_timer.Stopped())
	    SendPing(n);
      }
   }
   return q_num;
}
void DHT::RouteBucket::RemoveNode(Node *n)
{
   for(int j=0; j<nodes.count(); j++) {
      if(nodes[j]==n) {
	 RemoveNode(j);
	 break;
      }
   }
}
void DHT::RouteBucket::RemoveNode(int i)
{
   assert(i>=0 && i<nodes.count());
   nodes[i]->in_routes=false;
   nodes.remove(i);
}
bool DHT::RouteBucket::PrefixMatch(const xstring& id,int skew) const
{
   assert(skew>=0);
   int prefix_bits=this->prefix_bits-skew;
   if(prefix_bits<=0)
      return true;
   int bytes=prefix_bits/8;
   int bits=prefix_bits%8;
   unsigned mask=~((1<<(8-bits))-1);
   if(bytes>0 && memcmp(prefix.get(),id.get(),bytes))
      return false;
   if(bits>0 && (prefix[bytes]&mask)!=(id[bytes]&mask))
      return false;
   return true;
}
const char *DHT::RouteBucket::to_string() const
{
   xstring &buf=xstring::get_tmp("",0);
   prefix.hexdump_to(buf);
   buf.truncate((prefix_bits+3)/4);
   buf.append('/');
   buf.appendf("%d",prefix_bits);
   return buf;
}

void DHT::FindNodes(const xstring& target_id,xarray<Node*> &a,int max_count,bool only_good,const xmap<bool> *exclude)
{
   a.truncate();
   for(int skew=0; skew<160; skew++) {
      int i=FindRoute(target_id,0,skew);
      if(i<0)
	 continue;
      const xarray<Node*> &nodes=routes[i]->nodes;
      for(int j=0; j<nodes.count(); j++) {
	 if(!nodes[j]->IsBad() && (!only_good || nodes[j]->IsGood())
	 && nodes[j]->ping_lost_count<2 && a.search(nodes[j])==-1
	 && (!exclude || !exclude->exists(nodes[j]->id))) {
	    a.append(nodes[j]);
	    if(a.count()>=max_count)
	       return;
	 }
      }
   }
}
void DHT::AddPeer(const xstring& info_hash,const sockaddr_compact& a,bool seed)
{
   KnownTorrent *t=torrents.lookup(info_hash);
   if(!t) {
      if(torrents.count()>=MAX_TORRENTS) {
	 // remove random torrent
	 int r=random()/13%torrents.count();
	 int i=0;
	 for(torrents.each_begin(); ; torrents.each_next(), i++) {
	    if(i==r) {
	       torrents.remove(torrents.each_key());
	       break;
	    }
	 }
      }
      torrents.add(info_hash,t=new KnownTorrent());
   }
   t->AddPeer(new Peer(a,seed));

   sockaddr_u addr(a);
   LogNote(9,"added peer %s to torrent %s",addr.to_string(),info_hash.hexdump());
}
void DHT::KnownTorrent::AddPeer(Peer *peer)
{
   for(int i=0; i<peers.count(); i++) {
      if(peers[i]->compact_addr.eq(peer->compact_addr)) {
	 peers.remove(i);
	 break;
      }
   }
   if(peers.count()>=MAX_PEERS)
      peers.remove(0);
   peers.append(peer);
}

void DHT::MakeNodeId(xstring &id,const sockaddr_compact& ip,int r)
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
bool DHT::ValidNodeId(const xstring& id,const sockaddr_compact& ip)
{
   if(id.length()!=20)
      return false;

   sockaddr_u addr(ip);
   if(!addr.family())
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
   Enter(this);
   xmap_p<BeNode> state;
   state.add("id",new BeNode(node_id));
   int count=0;
   int responded_count=0;
   xstring compact_nodes;
   for(Node *n=nodes.each_begin(); n; n=nodes.each_next()) {
      if(n->IsGood() || n->in_routes) {
	 compact_nodes.append(n->id);
	 compact_nodes.append(n->addr.compact());
	 count++;
	 responded_count+=n->responded;
      }
   }
   LogNote(9,"saving state, %d nodes (%d responded)",count,responded_count);
   if(compact_nodes)
      state.add("nodes",new BeNode(compact_nodes));
   BeNode(&state).Pack(buf);
   for(int i=0; i<routes.count(); i++) {
      const RouteBucket *r=routes[i];
      LogNote(9,"route bucket %d: nodes count=%d prefix=%s",i,
	 (int)r->nodes.count(),r->to_string());
   }
   Leave();
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
   const xstring& nodes=state->lookup_str("nodes");
   if(!nodes)
      return;
   const char *data=nodes;
   int len=nodes.length();
   const int addr_len=(af==AF_INET?6:18);
   const int node_len=20+addr_len;
   while(len>=node_len) {
      xstring id(data,20);
      sockaddr_u a;
      a.set_compact(data+20,addr_len);
      data+=node_len;
      len-=node_len;
      FoundNode(id,a,false);
   }
   // refresh routes after loading
   for(int i=0; i<routes.count(); i++)
      routes[i]->fresh_timer.StopDelayed(i*15+3);
}
void DHT::Save()
{
   if(!state_file)
      return;
   FileStream *f=new FileStream(state_file,O_WRONLY|O_TRUNC|O_CREAT);
   f->set_lock();
   f->set_create_mode(0600);
   f->dont_keep_backup();
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
   state_io->Roll();
   Roll();
}

void DHT::Reconfig(const char *name)
{
   rate_limit.Reconfig(name,"DHT");
}

bool DHT::BlackList::Listed(const sockaddr_u& addr)
{
   const xstring &key=addr.to_xstring();
   Timer *e=bl.lookup(key);
   if(!e)
      return false;
   if(e->Stopped()) {
      LogNote(4,"black-delisting node %s\n",key.get());
      bl.remove(key);
      return false;
   }
   return true;
}
void DHT::BlackList::Add(const sockaddr_u &a,const char *t)
{
   if(Listed(a))
      return;
   LogNote(4,"black-listing node %s (%s)\n",a.to_string(),t);
   bl.add(a.to_xstring(),new Timer(TimeIntervalR(t)));
}
