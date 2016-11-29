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

#ifndef DHT_H
#define DHT_H

class Torrent;

class DHT : public SMTask, protected ProtoLog, public ResClient
{
   static const int K = 8;
   static const int MAX_NODES = 160*K;
   static const int MAX_TORRENTS = 1024;
   static const int MAX_PEERS = 60; // per torrent
   static const int MAX_SEND_QUEUE = 256;

   class Node
   {
   public:
      xstring id;
      xstring token;
      xstring my_token;
      xstring my_last_token;
      xstring origin_id;
      sockaddr_u addr;

      Timer good_timer; // 15 minutes, questionable when expired
      Timer token_timer;
      Timer ping_timer; // don't send pings too often
      bool responded; // has ever responded to our query
      bool in_routes; // belongs to the routing table;
      int ping_lost_count;
      int id_change_count;
      int bad_node_count;

      bool IsGood() const { return !good_timer.Stopped(); }
      void SetGood() { good_timer.Reset(); }
      bool IsBad() const { return (!IsGood() && ping_lost_count>=2) || id_change_count>=2; }
      void LostPing() { ping_lost_count++; }
      void ResetLostPing() { ping_lost_count=0; }
      void SetOrigin(const Node *o) { origin_id.set(o->id); }

      const char *GetName() const { return addr.to_string(); }

      Node(const xstring& i,const sockaddr_u& a)
	 : id(i.copy()), addr(a), good_timer(15*60), token_timer(5*60),
	   ping_timer(30), responded(false), in_routes(false),
	   ping_lost_count(0), id_change_count(0), bad_node_count(0)
      {
	 good_timer.Stop();
	 ping_timer.Stop();
      }

      const xstring& GetToken();
      bool TokenIsValid(const xstring& token) const;
   };
   class RouteBucket
   {
   public:
      int prefix_bits;
      xstring prefix;
      xarray<Node*> nodes;
      Timer fresh_timer;

      bool IsFresh() const { return !fresh_timer.Stopped(); }
      void SetFresh() { fresh_timer.Reset(); }
      bool PrefixMatch(const xstring& i,int skew=0) const;
      void RemoveNode(Node *n);
      void RemoveNode(int i);
      bool HasGoodNodes() const {
	 for(int i=0; i<nodes.count(); i++)
	    if(nodes[i]->IsGood())
	       return true;
	 return false;
      }

      RouteBucket(int pb,const xstring& p)
	 : prefix_bits(pb), prefix(p.copy()), fresh_timer(15*60)
      {
	 assert(prefix.length()>=size_t((prefix_bits+7)/8));
      }
      const char *to_string() const;
   };
   class Request
   {
   public:
      Ref<BeNode> data;
      sockaddr_u addr;
      xstring node_id;
      Timer expire_timer;
      bool Expired() const { return expire_timer.Stopped(); }
      const xstring& GetNodeId() const { return node_id; }
      const xstring& GetSearchTarget() const;

      Request(BeNode *b,const sockaddr_u& a,const xstring& id)
	 : data(b), addr(a), node_id(id.copy()), expire_timer(180) {}
   };
   class Search
   {
   public:
      xstring target_id;
      xstring best_node_id;
      xmap<bool> searched;
      int depth;
      Timer search_timer;
      bool want_peers;
      bool noseed;
      bool bootstrap;

      Search(const xstring& i)
	 : target_id(i.copy()), depth(0), search_timer(185),
	   want_peers(false), noseed(false), bootstrap(false) {}

      bool IsFeasible(const xstring &id) const;
      bool IsFeasible(const Node *n) const { return IsFeasible(n->id); }
      void ContinueOn(DHT *d,const Node *n);
      void WantPeers(bool ns) { want_peers=true; noseed=ns; }
      void Bootstrap() { bootstrap=true; }
   };
   class Peer
   {
   public:
      sockaddr_compact compact_addr;
      Timer good_timer;
      bool seed;

      Peer(const sockaddr_compact &a,bool s)
	 : compact_addr(a), good_timer(15*60), seed(s) {}
      bool IsGood() const { return !good_timer.Stopped(); }
   };
   class KnownTorrent
   {
   public:
      xarray_p<Peer> peers;
      void AddPeer(Peer *);
   };
   class BlackList : private ProtoLog
   {
      xmap_p<Timer> bl;
   public:
      bool Listed(const sockaddr_u &a);
      void Add(const sockaddr_u &a,const char *t="1h");
   };

   int af;

   BlackList black_list;
   RateLimit rate_limit;
   RefQueue<Request> send_queue;
   xmap_p<Request> sent_req; // the key is "t"
   Timer sent_req_expire_scan;
   Timer search_cleanup_timer;
   Timer refresh_timer;
   Timer nodes_cleanup_timer;
   Timer save_timer;

   // voting for new external IP
   xmap<unsigned> ip_votes;
   xmap<bool> ip_voted;

   xstring node_id;
   xmap_p<Node> nodes;
   xmap<Node*> node_by_addr;
   RefArray<RouteBucket> routes;
   xmap_p<Search> search;
   xmap_p<KnownTorrent> torrents;

   xqueue_p<xstring> bootstrap_nodes;
   SMTaskRef<Resolver> resolver;

   void Bootstrap();
   void AddNode(Node *);
   void RemoveNode(Node *);
   void ChangeNodeId(Node *n,const xstring& new_node_id);
   void BlackListNode(Node *n,const char *timeout);
   void AddRoute(Node *);
   void RemoveRoute(Node *n);
   bool SplitRoute0();
   Node *FoundNode(const xstring& id,const sockaddr_u& a,bool responded,Search *s=0);
   int FindRoute(const xstring& i,int start=0,int skew=0);
   void FindNodes(const xstring& i,xarray<Node*> &a,int max_count,bool only_good,const xmap<bool> *exclude=0);
   void StartSearch(Search *s);
   void RestartSearch(Search *s);
   void AddPeer(const xstring& ih,const sockaddr_compact& ca,bool seed);
   Node *GetOrigin(const Node *n);

   unsigned t; // transaction id

   BeNode *NewQuery(const char *q,xmap_p<BeNode>& a);
   BeNode *NewReply(const xstring& t0,xmap_p<BeNode>& r);
   BeNode *NewError(const xstring& t0,int code,const char *msg);
   void SendMessage(BeNode *q,const sockaddr_u& a,const xstring& id=xstring::null);
   void SendMessage(Request *);
   bool MaySendMessage();
   static const char *MessageType(BeNode *q);
   static int AddNodesToReply(xmap_p<BeNode> &r,const xstring& target,bool want_n4,bool want_n6);
   int AddNodesToReply(xmap_p<BeNode> &r,const xstring& target,int max_count);

   enum {
      ERR_GENERIC=201,
      ERR_SERVER=202,
      ERR_PROTOCOL=203,
      ERR_UNKNOWN_METHOD=204,
   };

   SMTaskRef<IOBuffer> state_io;

public:
   DHT(int af,const xstring &id);
   ~DHT();
   int Do();

   static void MakeNodeId(xstring &id,const sockaddr_compact& ip,int r=random()/13);
   static bool ValidNodeId(const xstring &id,const sockaddr_compact& ip);
   void Restart();

   void Reconfig(const char *name);
   const char *GetLogContext() { return af==AF_INET?"DHT":"DHT6"; }
   const xstring& GetNodeID() const { return node_id; }

   void SendPing(const sockaddr_u& addr,const xstring& id=xstring::null);
   void SendPing(Node *n);
   int PingQuestionable(const xarray<Node*>& nodes,int limit);
   void AnnouncePeer(const Torrent *);
   void DenouncePeer(const Torrent *);
   void HandlePacket(BeNode *b,const sockaddr_u& src);

   void Save(const SMTaskRef<IOBuffer>& buf);
   void Load(const SMTaskRef<IOBuffer>& buf);
   xstring state_file;
   void Save();
   void Load();

   void AddBootstrapNode(const char *n) { bootstrap_nodes.push(new xstring(n)); }
};

#endif//DHT_H
