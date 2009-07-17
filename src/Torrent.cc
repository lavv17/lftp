/*
 * lftp and utils
 *
 * Copyright (c) 2009 by Alexander V. Lukyanov (lav@yars.free.net)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* $Id$ */

#include <config.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

CDECL_BEGIN
#include <sha1.h>
CDECL_END

#include "Torrent.h"
#include "log.h"
#include "url.h"
#include "misc.h"

static ResType torrent_vars[] = {
   {"torrent:port-range", "6881-6889", ResMgr::RangeValidate, ResMgr::NoClosure},
   {"torrent:request-timeout", "120",  ResMgr::TimeIntervalValidate, ResMgr::NoClosure},
   {0}
};
static ResDecls torrent_vars_register(torrent_vars);

xstring Torrent::my_peer_id;
Ref<TorrentListener> Torrent::listener;

Torrent::Torrent(const char *mf)
   : metainfo_url(mf),
     tracker_timer(600), pieces_needed_rebuild_timer(10),
     cwd(xgetcwd()), rate_limit(mf)
{
   started=false;
   shutting_down=false;
   complete=false;
   validated=false;
   stopped=false;
   end_game=false;
   validating=false;
   validate_index=0;
   tracker_url=0;
   piece_length=0;
   last_piece_length=0;
   total_length=0;
   total_sent=0;
   total_recv=0;
   total_left=0;
   complete_pieces=0;
   active_peers_count=0;
   complete_peers_count=0;
   max_peers=60;
   last_piece=TorrentPeer::NO_PIECE;
   Reconfig(0);

   if(listener==0)
      listener=new TorrentListener();
   if(!my_peer_id) {
      my_peer_id.set("-lf0000-");
      my_peer_id.appendf("%04x",(unsigned)getpid());
      my_peer_id.appendf("%08x",(unsigned)now.UnixTime());
      assert(my_peer_id.length()==PEER_ID_LEN);
   }
}

int Torrent::Done()
{
   return (shutting_down && !tracker_reply);
}

void Torrent::Shutdown()
{
   if(shutting_down)
      return;
   LogNote(3,"Shutting down...");
   shutting_down=true;
   if(listener)
      listener->RemoveTorrent(this);
   if(started || tracker_reply)
      SendTrackerRequest("stopped",0);
   if(listener && listener->GetTorrentsCount()==0)
      listener=0;
   peers.unset();
}

void Torrent::PrepareToDie()
{
   peers.unset();
   if(listener)
      listener->RemoveTorrent(this);
}

void Torrent::SetError(Error *e)
{
   if(invalid_cause)
      return;
   invalid_cause=e;
   LogError(0,"%s: %s",
      invalid_cause->IsFatal()?"Fatal error":"Transient error",
      invalid_cause->Text());
   tracker_reply=0;
   Shutdown();
}

void Torrent::SetDownloader(unsigned piece,unsigned block,TorrentPeer *o,TorrentPeer *n)
{
   TorrentPeer*& downloader=piece_info[piece]->downloader[block];
   if(downloader==o)
      downloader=n;
}

BeNode *Torrent::Lookup(xmap_p<BeNode>& dict,const char *name,BeNode::be_type_t type)
{
   BeNode *node=dict.lookup(name);
   if(!node) {
      SetError(Error::Fatal(xstring::format("Meta-data: `%s' key missing",name)));
      return 0;
   }
   if(node->type!=type) {
      SetError(Error::Fatal(xstring::format("Meta-data: wrong `%s' type, must be %s",name,BeNode::TypeName(type))));
      return 0;
   }
   return node;
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
      piece_info[p]->have=false;
      piece_info[p]->block_map.clear();
   } else {
      LogNote(11,"piece %u ok",p);
      if(!my_bitfield->get_bit(p)) {
	 total_left-=PieceLength(p);
	 complete_pieces++;
	 my_bitfield->set_bit(p,1);
      }
      piece_info[p]->have=true;
   }
}

int Torrent::PeersCompareForUnchoking(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2)
{
   return 0;
}

bool TorrentPiece::has_a_downloader()
{
   for(int i=0; i<downloader.count(); i++) {
      if(downloader[i]) {
	 return true;
      }
   }
   return false;
}

static int int_cmp(int a,int b)
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
   int ra=cmp_torrent->piece_info[*a]->sources_count+cmp_torrent->piece_info[*a]->rnd;
   int rb=cmp_torrent->piece_info[*b]->sources_count+cmp_torrent->piece_info[*b]->rnd;
   return int_cmp(ra,rb);
}
int Torrent::PeersCompareInterest(const SMTaskRef<TorrentPeer> *p1,const SMTaskRef<TorrentPeer> *p2)
{
   TimeDiff i1((*p1)->interest_timer.TimePassed());
   TimeDiff i2((*p2)->interest_timer.TimePassed());
   return int_cmp(i1.Seconds(),i2.Seconds());
}

int Torrent::Do()
{
   int m=STALL;
   if(Done())
      return m;

   // retrieve metainfo if don't have already.
   if(!metainfo_tree) {
      if(!metainfo_fa) {
	 LogNote(9,"Retrieving meta-data from `%s'...\n",metainfo_url.get());
	 ParsedURL u(metainfo_url,true);
	 if(!u.proto)
	    u.proto.set("file");
	 metainfo_fa=FileAccess::New(&u);
	 metainfo_fa->Open(u.path,FA::RETRIEVE);
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
	 SetError(Error::Fatal("Meta-data parse error"));
	 return MOVED;
      }
      if(rest>0) {
	 SetError(Error::Fatal("Junk at the end of Meta-data"));
	 return MOVED;
      }
      LogNote(10,"Received meta-data:");
      Log::global->Write(10,metainfo_tree->Format());

      if(metainfo_tree->type!=BeNode::BE_DICT) {
	 SetError(Error::Fatal("Meta-data: wrong top level type, must be DICT"));
         return MOVED;
      }
      BeNode *announce=Lookup(metainfo_tree,"announce",BeNode::BE_STR);
      if(!announce)
         return MOVED;

      tracker_url=&announce->str;
      LogNote(4,"Tracker URL is `%s'",tracker_url->get());
      ParsedURL u(tracker_url->get(),true);
      if(u.proto.ne("http") && u.proto.ne("https")) {
	 SetError(Error::Fatal("Meta-data: wrong `announce' protocol, must be http or https"));
         return MOVED;
      }
      // fix the URL: append either ? or & if missing.
      if(tracker_url->last_char()!='?' && tracker_url->last_char()!='&')
	 tracker_url->append(strchr(tracker_url->get(),'?')?'&':'?');

      info=Lookup(metainfo_tree,"info",BeNode::BE_DICT);
      if(!info)
         return MOVED;

      SHA1(info->str,info_hash);

      BeNode *b_piece_length=Lookup(info,"piece length",BeNode::BE_INT);
      piece_length=b_piece_length->num;
      LogNote(4,"Piece length is %u",piece_length);

      Lookup(info,"name",BeNode::BE_STR);

      BeNode *files=info->dict.lookup("files");
      if(!files) {
	 single_file=true;
	 BeNode *length=Lookup(info,"length",BeNode::BE_INT);
	 if(!length)
	    return MOVED;
	 total_length=length->num;
      } else {
	 single_file=false;
	 if(files->type!=BeNode::BE_LIST) {
	    SetError(Error::Fatal("Meta-data: wrong `info/files' type, must be LIST"));
	    return MOVED;
	 }
	 total_length=0;
	 for(int i=0; i<files->list.length(); i++) {
	    if(files->list[i]->type!=BeNode::BE_DICT) {
	       SetError(Error::Fatal(xstring::format("Meta-data: wrong `info/files[%d]' type, must be LIST",i)));
	       return MOVED;
	    }
	    BeNode *f=Lookup(files->list[i]->dict,"length",BeNode::BE_INT);
	    if(!f)
	       return MOVED;
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
	 return MOVED;
      pieces=&b_pieces->str;
      if(pieces->length()!=SHA1_DIGEST_SIZE*total_pieces) {
	 SetError(Error::Fatal("Meta-data: invalid `pieces' length"));
	 return MOVED;
      }

      my_bitfield=new BitField(total_pieces);

      blocks_per_piece=(piece_length+BLOCK_SIZE-1)/BLOCK_SIZE;
      for(unsigned p=0; p<total_pieces; p++)
	 piece_info.append(new TorrentPiece(blocks_per_piece));

      validate_index=0;
      validating=true;
   }
   if(validating) {
      ValidatePiece(validate_index++);
      if(validate_index<total_pieces)
	 return MOVED;
      validating=false;
      validated=true;
      if(total_left==0)
	 complete=true;
   }
   if(!t_session && !started && !shutting_down) {
      if(listener->GetPort()==0)
	 return m;
      ParsedURL u(tracker_url->get(),true);
      t_session=FileAccess::New(&u);
      listener->AddTorrent(this);
      SendTrackerRequest("started",-1);
      m=MOVED;
   }

   // scan existing peers
   for(int i=0; i<peers.count(); i++) {
      if(peers[i]->Failed()) {
	 LogError(2,"Peer %s failed: %s",peers[i]->GetName(),peers[i]->ErrorText());
	 peers.remove(i--);
      } else if(complete && (!peers[i]->Connected() || peers[i]->Complete())) {
	 peers.remove(i--);
      }
   }
   //peers.qsort(PeersCompareForUnchoking);
   // FIXME: unchoke algorithm
   if(peers.count()>max_peers) {
      // remove least interesting peers.
      peers.qsort(PeersCompareInterest);
      int to_remove=peers.count()-max_peers;
      while(to_remove-->0) {
	 LogNote(3,"removing peer %s (too many)",peers.last()->GetName());
	 peers.chop();
      }
   }
   // count peers
   active_peers_count=0;
   complete_peers_count=0;
   for(int i=0; i<peers.count(); i++) {
      active_peers_count+=peers[i]->Active();
      complete_peers_count+=peers[i]->Complete();
   }

   // rebuild lists of needed pieces
   if(pieces_needed_rebuild_timer.Stopped()) {
      pieces_needed.truncate();
      bool enter_end_game=true;
      for(unsigned i=0; i<total_pieces; i++) {
	 if(!my_bitfield->get_bit(i)) {
	    if(!piece_info[i]->has_a_downloader())
	       enter_end_game=false;
	    if(piece_info[i]->sources_count==0)
	       continue;
	    piece_info[i]->rnd=rand()/13%4;
	    pieces_needed.append(i);
	 }
      }
      if(enter_end_game) {
	 LogNote(1,"entering End Game mode");
	 end_game=true;
      }
      cmp_torrent=this;
      pieces_needed.qsort(PiecesNeededCmp);
      pieces_needed_rebuild_timer.Reset();
   }

   if(tracker_reply) {
      if(tracker_reply->Error()) {
	 SetError(new Error(-1,tracker_reply->ErrorText(),false));
	 t_session->Close();
	 tracker_reply=0;
	 return MOVED;
      }
      if(tracker_reply->Eof()) {
	 t_session->Close();
	 int rest;
	 Ref<BeNode> reply(BeNode::Parse(tracker_reply->Get(),tracker_reply->Size(),&rest));
	 LogNote(10,"Received tracker reply:");
	 Log::global->Write(10,reply->Format());

	 if(shutting_down) {
	    tracker_reply=0;
	    return MOVED;
	 }
	 started=true;

	 if(reply->type!=BeNode::BE_DICT) {
	    SetError(Error::Fatal("Reply: wrong reply type, must be DICT"));
	    return MOVED;
	 }

	 BeNode *b_failure_reason=reply->dict.lookup("failure reason");
	 if(b_failure_reason) {
	    if(b_failure_reason->type==BeNode::BE_STR)
	       SetError(Error::Fatal(b_failure_reason->str));
	    else
	       SetError(Error::Fatal("Reply: wrong `failure reason' type, must be STR"));
	    return MOVED;
	 }

	 BeNode *b_interval=reply->dict.lookup("interval");
	 if(b_interval && b_interval->type==BeNode::BE_INT) {
	    LogNote(4,"Setting tracker interval to %llu",b_interval->num);
	    tracker_timer.Set(b_interval->num);
	 }

	 BeNode *b_tracker_id=reply->dict.lookup("tracker id");
	 if(!tracker_id && b_tracker_id && b_tracker_id->type==BeNode::BE_STR)
	    tracker_id.set(b_tracker_id->str);

	 BeNode *b_peers=reply->dict.lookup("peers");
	 if(b_peers) {
	    if(b_peers->type==BeNode::BE_STR) { // binary model
	       const char *data=b_peers->str;
	       int len=b_peers->str.length();
	       while(len>=6) {
		  sockaddr_u a;
		  a.sa.sa_family=AF_INET;
		  memcpy(&a.in.sin_addr,data,4);
		  memcpy(&a.in.sin_port,data+4,2);
		  data+=6;
		  len-=6;
		  AddPeer(new TorrentPeer(this,&a));
	       }
	    } else if(b_peers->type==BeNode::BE_LIST) { // dictionary model
	       for(int p=0; p<b_peers->list.count(); p++) {
		  BeNode *b_peer=b_peers->list[p];
		  if(b_peer->type!=BeNode::BE_DICT)
		     continue;
		  BeNode *b_ip=b_peer->dict.lookup("ip");
		  if(b_ip->type!=BeNode::BE_STR)
		     continue;
		  BeNode *b_port=b_peer->dict.lookup("port");
		  if(b_port->type!=BeNode::BE_INT)
		     continue;
		  sockaddr_u a;
		  a.sa.sa_family=AF_INET;
		  if(!inet_aton(b_ip->str,&a.in.sin_addr))
		     continue;
		  a.in.sin_port=htons(b_port->num);
		  AddPeer(new TorrentPeer(this,&a));
	       }
	    }
	 }

	 tracker_timer.Reset();
	 tracker_reply=0;
      }
   } else {
      if(tracker_timer.Stopped()) {
	 // remove uninteresting peers and request more
	 for(int i=0; i<peers.count(); i++) {
	    if(peers[i]->InterestTimedOut())
	       peers.remove(i--);
	 }
	 int numwant=0;
 	 if(!complete && peers.count()<30 /*&& download_rate<wanted_download_rate*/)
 	    numwant=30-peers.count();
	 SendTrackerRequest(0,numwant);
      }
   }
   return m;
}

void Torrent::Accept(int s,IOBuffer *rb)
{
   sockaddr_u addr;
   socklen_t sock_len;
   getpeername(s,&addr.sa,&sock_len);
   TorrentPeer *p=new TorrentPeer(this,&addr);
   p->Connect(s,rb);
   AddPeer(p);
}

void Torrent::AddPeer(TorrentPeer *peer)
{
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

void Torrent::SendTrackerRequest(const char *event,int numwant)
{
   if(!t_session)
      return;
   xstring request;
   request.setf("info_hash=%s",url::encode(info_hash,URL_PATH_UNSAFE).get());
   request.appendf("&peer_id=%s",url::encode(my_peer_id,URL_PATH_UNSAFE).get());
   request.appendf("&port=%d",listener->GetPort());
   if(event)
      request.appendf("&event=%s",event);
   request.appendf("&uploaded=%llu",total_sent);
   request.appendf("&downloaded=%llu",total_recv);
   request.appendf("&left=%llu",total_left);
   request.append("&compact=1");
   if(numwant>=0)
      request.appendf("&numwant=%d",numwant);
   if(tracker_id)
      request.appendf("&trackerid=%s",url::encode(tracker_id,URL_PATH_UNSAFE).get());
   LogSend(4,request);
   t_session->Open(request,FA::RETRIEVE);
   t_session->SetFileURL(xstring::cat(tracker_url->get(),request.get(),NULL));
   tracker_reply=new IOBufferFileAccess(t_session);
}

const char *Torrent::MakePath(BeNode *p)
{
   BeNode *name=Lookup(info,"name",BeNode::BE_STR);
   BeNode *path=Lookup(p->dict,"path",BeNode::BE_LIST);

   static xstring buf;
   buf.set(name->str);
   if(buf.eq("..") || buf[0]=='/') {
      buf.set_substr(0,0,"_",1);
   }
   for(int i=0; i<path->list.count(); i++) {
      BeNode *e=path->list[i];
      if(e->type==BeNode::BE_STR) {
	 buf.append('/');
	 if(e->str.eq(".."))
	    buf.append('_');
	 buf.append(e->str);
      }
   }
   return buf;
}
const char *Torrent::FindFileByPosition(unsigned piece,unsigned begin,off_t *f_pos,off_t *f_tail)
{
   BeNode *name=Lookup(info,"name",BeNode::BE_STR);
   BeNode *files=info->dict.lookup("files");
   off_t target_pos=piece*piece_length+begin;
   if(!files) {
      *f_pos=target_pos;
      *f_tail=total_length-target_pos;
      return name->str;
   } else {
      off_t scan_pos=0;
      for(int i=0; i<files->list.length(); i++) {
	 BeNode *f=Lookup(files->list[i]->dict,"length",BeNode::BE_INT);
	 off_t file_length=f->num;
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
int Torrent::OpenFile(const char *file,int m)
{
   const char *cf=dir_file(cwd,file);
   LogNote(9,"opening %s",cf);
   int fd=open(cf,m,0664);
   if(fd==-1 && errno==ENOENT) {
      LogError(10,"open(%s): %s",cf,strerror(errno));
      const char *sl=strchr(file,'/');
      while(sl)
      {
	 if(sl>file) {
	    if(mkdir(cf=dir_file(cwd,xstring::get_tmp(file,sl-file)),0775)==-1 && errno!=EEXIST)
	       LogError(9,"mkdir(%s): %s",cf,strerror(errno));
	 }
	 sl=strchr(sl+1,'/');
      }
      fd=open(cf=dir_file(cwd,file),m,0664);
   }
   return fd;
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
void Torrent::SetPieceWanted(unsigned piece)
{
   if(piece_info[piece]->sources_count==0)
      return;
   for(int j=0; j<pieces_needed.count(); j++) {
      if(pieces_needed[j]==piece)
	 break;
      if(j==pieces_needed.count()-1)
	 pieces_needed.append(piece);
   }
}

void Torrent::StoreBlock(unsigned piece,unsigned begin,unsigned len,const char *buf)
{
   for(int i=0; i<peers.count(); i++)
      peers[i]->CancelBlock(piece,begin);

   off_t f_pos=0;
   off_t f_rest=len;
   unsigned b=begin/BLOCK_SIZE;
   int bc=(len+BLOCK_SIZE-1)/BLOCK_SIZE;
   while(len>0) {
      const char *file=FindFileByPosition(piece,begin,&f_pos,&f_rest);
      if(f_rest>len)
	 f_rest=len;
      int fd=OpenFile(file,O_RDWR|O_CREAT);
      if(fd==-1) {
	 SetError(Error::Fatal(xstring::format("open(%s): %s",file,strerror(errno))));
	 return;
      }
      int w=pwrite(fd,buf,f_rest,f_pos);
      int saved_errno=errno;
      close(fd);
      if(w==-1) {
	 SetError(Error::Fatal(xstring::format("pwrite(%s): %s",file,strerror(saved_errno))));
	 return;
      }
      if(w==0) {
	 SetError(Error::Fatal(xstring::format("pwrite(%s): write error - disk full?",file)));
	 return;
      }
      buf+=w;
      begin+=w;
      len-=w;
   }
   while(bc-->0) {
      piece_info[piece]->block_map.set_bit(b++,1);
   }
   if(piece_info[piece]->block_map.has_all_set() && !piece_info[piece]->have) {
      ValidatePiece(piece);
      if(!piece_info[piece]->have) {
	 LogError(0,"new piece %u digest mismatch",piece);
	 return;
      }
      LogNote(4,"piece %u complete",piece);
      LogSend(5,xstring::format("broadcast have(%u)",piece));
      SetPieceNotWanted(piece);
      for(int i=0; i<peers.count(); i++)
	 peers[i]->Have(piece);
      if(my_bitfield->has_all_set() && !complete) {
	 complete=true;
	 SendTrackerRequest("complete",0);
      }
   }
}

const xstring& Torrent::RetrieveBlock(unsigned piece,unsigned begin,unsigned len)
{
   static xstring buf;
   buf.set("");
   off_t f_pos=0;
   off_t f_rest=len;
   while(len>0) {
      const char *file=FindFileByPosition(piece,begin,&f_pos,&f_rest);
      if(f_rest>len)
	 f_rest=len;
      int fd=OpenFile(file,O_RDONLY);
      if(fd==-1)
	 return xstring::null;
      int w=pread(fd,buf.add_space(f_rest),f_rest,f_pos);
      int saved_errno=errno;
      close(fd);
      if(w==-1) {
	 SetError(Error::Fatal(xstring::format("pread(%s): %s",file,strerror(saved_errno))));
	 return xstring::null;
      }
      if(w==0) {
// 	 buf.append_padding(len,'\0');
	 break;
      }
      buf.add_commit(w);
      begin+=w;
      len-=w;
   }
   return buf;
}

int Torrent::PeerBytesAllowed(TorrentPeer *peer,RateLimit::dir_t dir)
{
   float peer_rate=(dir==RateLimit::GET ? peer->peer_send_rate : peer->peer_recv_rate).Get();
   float rate=(dir==RateLimit::GET ? send_rate : recv_rate).Get();
   int min_rate = 1000;
   // the more is the opposite rate the more rate allowed, with a minimum
   int bytes = rate_limit.BytesAllowed(dir);
   bytes *= (peer_rate + min_rate);
   bytes /= (rate + active_peers_count*min_rate);
   return bytes;
}
void Torrent::PeerBytesUsed(int b,RateLimit::dir_t dir)
{
   rate_limit.BytesUsed(b,dir);
}
void Torrent::Reconfig(const char *name)
{
   max_peers=ResMgr::Query("torrent:max-peers",0);
   rate_limit.Reconfig(name,metainfo_url);
}

const char *Torrent::Status()
{
   if(metainfo_data)
      return xstring::format("Getting meta-data: %s",metainfo_data->Status());
   if(validating) {
      return xstring::format("Validation: %u/%u (%u%%)",validate_index,total_pieces,
	 validate_index*100/total_pieces);
   }
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


TorrentPeer::TorrentPeer(Torrent *p,const sockaddr_u *a)
   : timeout_timer(360), retry_timer(30), keepalive_timer(120), choke_timer(10), interest_timer(300)
{
   parent=p;
   addr=*a;
   sock=-1;
   peer_choking=true;
   am_choking=true;
   peer_interested=false;
   am_interested=false;
   peer_complete_pieces=0;
   retry_timer.Stop();
   last_piece=NO_PIECE;
   if(addr.is_reserved() || addr.is_multicast())
      SetError("invalid peer address");
   peer_bytes_pool[0]=peer_bytes_pool[1]=0;
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
}

void TorrentPeer::SetError(const char *s)
{
   error=Error::Fatal(s);
   Disconnect();
}

void TorrentPeer::Disconnect()
{
   if(peer_bitfield) {
      for(unsigned i=0; i<parent->total_pieces; i++)
	 if(peer_bitfield->get_bit(i))
	    parent->piece_info[i]->sources_count--;
   }
   recv_queue.empty();
   ClearSentQueue();
   recv_buf=0;
   send_buf=0;
   close(sock);
   sock=-1;
   connected=false;
   peer_bitfield=0;
   am_interested=false;
   am_choking=true;
   peer_interested=false;
   peer_choking=true;
   peer_complete_pieces=0;
   retry_timer.Reset();
   // return to main pool
   parent->PeerBytesUsed(-peer_bytes_pool[RateLimit::GET],RateLimit::GET);
   parent->PeerBytesUsed(-peer_bytes_pool[RateLimit::PUT],RateLimit::PUT);
   peer_bytes_pool[0]=peer_bytes_pool[1]=0;
}

void TorrentPeer::SendHandshake()
{
   const char *const protocol="BitTorrent protocol";
   int proto_len=strlen(protocol);
   send_buf->PackUINT8(proto_len);
   send_buf->Put(protocol,proto_len);
   send_buf->Put("\0\0\0\0\0\0\0",8);
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
   unpacked+=8; // 8 bytes are reserved

   xstring peer_info_hash(data+unpacked,SHA1_DIGEST_SIZE);
   unpacked+=SHA1_DIGEST_SIZE;
   if(peer_info_hash.ne(parent->info_hash)) {
      LogError(0,"got info_hash: %s, wanted: %s",peer_info_hash.dump(),parent->info_hash.dump());
      SetError("peer info_hash mismatch");
      return UNPACK_WRONG_FORMAT;
   }

   peer_id.nset(recv_buf->Get()+unpacked,Torrent::PEER_ID_LEN);
   unpacked+=Torrent::PEER_ID_LEN;

   recv_buf->Skip(unpacked);
   LogRecv(4,xstring::format("handshake, %s, peer_id: %s",protocol.dump(),peer_id.dump()));

   return UNPACK_SUCCESS;
}

void TorrentPeer::SendDataReply()
{
   const PacketRequest *p=recv_queue.next();
   Enter(parent);
   const xstring& data=parent->RetrieveBlock(p->index,p->begin,p->req_length);
   Leave(parent);
   if(data.length()!=p->req_length)
      return;
   LogSend(6,xstring::format("piece:%u begin:%u size:%u",p->index,p->begin,p->req_length));
   PacketPiece(p->index,p->begin,data).Pack(send_buf);
   parent->total_sent+=data.length();
   parent->send_rate.Add(data.length());
   peer_send_rate.Add(data.length());
   interest_timer.Reset();
}

int TorrentPeer::SendDataRequests(unsigned p)
{
   if(p==NO_PIECE)
      return 0;

   int sent=0;
   for(unsigned b=0; b<parent->blocks_per_piece; b++) {
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

      if(p==parent->total_pieces-1 && begin>=parent->last_piece_length)
	 break;

      if(b==parent->blocks_per_piece-1 && begin+len>parent->piece_length)
	 len=parent->piece_length-begin;

      if(p==parent->total_pieces-1 && begin+len>parent->last_piece_length)
	 len=parent->last_piece_length-begin;

      if(!BytesAllowedToGet(len))
	 break;

      parent->SetDownloader(p,b,0,this);
      PacketRequest *req=new PacketRequest(p,b*Torrent::BLOCK_SIZE,len);
      LogSend(6,xstring::format("request piece:%u begin:%u size:%u",p,b*Torrent::BLOCK_SIZE,len));
      req->Pack(send_buf);
      sent_queue.push(req);
      SetLastPiece(p);
      sent++;
      interest_timer.Reset();
      BytesGot(len);

      if(sent_queue.count()>=MAX_QUEUE_LEN)
	 break;
   }
   return sent;
}

void TorrentPeer::SendDataRequests()
{
   if(sent_queue.count()>=MAX_QUEUE_LEN)
      return;
   if(!BytesAllowedToGet(Torrent::BLOCK_SIZE))
      return;
   if(SendDataRequests(GetLastPiece())>0)
      return;

   unsigned p=NO_PIECE;
   if(peer_bitfield) {
      for(int i=0; i<parent->pieces_needed.count(); i++) {
	 if(peer_bitfield->get_bit(parent->pieces_needed[i])) {
	    p=parent->pieces_needed[i];
	    break;
	 }
      }
   }
   if(p==NO_PIECE) {
      Packet(MSG_UNINTERESTED).Pack(send_buf);
      am_interested=false;
      return;
   }
   SendDataRequests(p);
}

void TorrentPeer::Have(unsigned p)
{
   if(!send_buf)
      return;
   Enter();
   PacketHave(p).Pack(send_buf);
   LogSend(9,xstring::format("have(%u)",p));
   Leave();
}
int TorrentPeer::FindRequest(unsigned piece,unsigned begin)
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

void TorrentPeer::ClearSentQueue(int i)
{
   while(i-->=0) {
      const PacketRequest *req=sent_queue.next();
      parent->PeerBytesGot(-req->req_length);
      parent->SetDownloader(req->index,req->begin/Torrent::BLOCK_SIZE,this,0);
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

unsigned TorrentPeer::GetLastPiece()
{
   unsigned p=last_piece;
   // continue if have any blocks already
   if(p!=NO_PIECE && !parent->piece_info[p]->have && parent->piece_info[p]->block_map.has_any_set())
      return p;
   p=parent->last_piece;
   if(p!=NO_PIECE && !parent->piece_info[p]->have)
      return p;
   p=last_piece;
   if(p!=NO_PIECE && !parent->piece_info[p]->have)
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
	 PacketHave *pp=static_cast<PacketHave*>(p);
	 LogRecv(5,xstring::format("have(%u)",pp->piece));
	 if(!peer_bitfield)
	    peer_bitfield=new BitField(parent->total_pieces);
	 if(!peer_bitfield->valid_index(pp->piece)) {
	    SetError("invalid piece index");
	    break;
	 }

	 if(!parent->my_bitfield->get_bit(pp->piece)) {
	    SetLastPiece(pp->piece);
	 }

	 if(!peer_bitfield->get_bit(pp->piece)) {
	    peer_bitfield->set_bit(pp->piece,1);
	    peer_complete_pieces++;
	    parent->piece_info[pp->piece]->sources_count++;
	    if(!am_interested && !parent->my_bitfield->get_bit(pp->piece)) {
	       am_interested=true;
	       Packet(MSG_INTERESTED).Pack(send_buf);
	    }
	 }
	 break;
      }
   case MSG_BITFIELD: {
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
	 peer_bitfield=pp->bitfield.borrow();
	 peer_bitfield->set_bit_length(parent->total_pieces);
	 for(unsigned i=0; i<parent->total_pieces; i++) {
	    if(peer_bitfield->get_bit(i)) {
	       peer_complete_pieces++;
	       parent->piece_info[i]->sources_count++;
	       if(!parent->my_bitfield->get_bit(i))
		  am_interested=true;
	    }
	 }
	 LogRecv(5,xstring::format("bitfield(%u/%u)",peer_complete_pieces,parent->total_pieces));
	 if(am_interested)
	    Packet(MSG_INTERESTED).Pack(send_buf);
	 break;
      }
   case MSG_PORT: {
	 PacketPort *pp=static_cast<PacketPort*>(p);
	 LogRecv(5,xstring::format("port(%u)",pp->port));
	 break;
      }
   case MSG_PIECE: {
	 PacketPiece *pp=static_cast<PacketPiece*>(p);
	 LogRecv(5,xstring::format("piece:%u part offset:%u size:%u",pp->index,pp->begin,pp->data.length()));
	 if(pp->index>=parent->total_pieces) {
	    SetError("invalid piece number");
	    break;
	 }
	 if(pp->begin>=parent->PieceLength(pp->index)) {
	    SetError("invalid data offset");
	    break;
	 }
	 for(int i=0; i<sent_queue.count(); i++) {
	    const PacketRequest *req=sent_queue[i];
	    if(req->index==pp->index && req->begin==pp->begin) {
	       ClearSentQueue(i); // including previous unanswered requests
	       parent->PeerBytesGot(pp->data.length()); // re-take the bytes returned by ClearSentQueue
	       break;
	    }
	 }
	 Enter(parent);
	 parent->StoreBlock(pp->index,pp->begin,pp->data.length(),pp->data.get());
	 Leave(parent);

	 int len=pp->data.length();
	 parent->total_recv+=len;
	 parent->recv_rate.Add(len);
	 peer_recv_rate.Add(len);

	 // request another block from the same piece
	 if(am_interested && !peer_choking)
	    SendDataRequests(pp->index);
	 break;
      }
   case MSG_REQUEST: {
	 PacketRequest *pp=static_cast<PacketRequest*>(p);
	 LogRecv(5,xstring::format("request for piece:%u part offset:%u size:%u",pp->index,pp->begin,pp->req_length));
	 if(pp->req_length>Torrent::BLOCK_SIZE*2) {
	    LogError(0,"too large request");
	    Disconnect();
	    break;
	 }
	 if(pp->index>=parent->total_pieces)
	    break;
	 if(pp->begin>=parent->PieceLength(pp->index))
	    break;
	 recv_queue.push(pp);
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

bool TorrentPeer::HasNeededPieces()
{
   if(GetLastPiece()!=NO_PIECE)
      return true;
   if(!peer_bitfield)
      return false;
   for(int i=0; i<parent->pieces_needed.count(); i++)
      if(peer_bitfield->get_bit(parent->pieces_needed[i]))
	 return true;
   return false;
}

int TorrentPeer::Do()
{
   int m=STALL;
   if(error)
      return m;
   if(sock==-1) {
      if(parent->complete)
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
      LogNote(1,_("Connecting to peer %s port %u"),SocketNumericAddress(&addr),SocketPort(&addr));
      connected=false;
   }
   if(!connected) {
      int res=SocketConnect(sock,&addr);
      if(res==-1 && errno!=EINPROGRESS && errno!=EALREADY && errno!=EISCONN)
      {
	 int e=errno;
	 LogError(0,"connect: %s\n",strerror(e));
	 Disconnect();
	 if(FA::NotSerious(e))
	 {
	    Disconnect();
	    return MOVED;
	 }
	 SetError(strerror(e));
	 return MOVED;
      }
      if(res==-1 && errno!=EISCONN) {
	 Block(sock,POLLOUT);
	 return m;
      }
      connected=true;
      timeout_timer.Reset();
      interest_timer.Reset();
      m=MOVED;
   }
   if(!recv_buf) {
      recv_buf=new IOBufferFDStream(new FDStream(sock,"<input-socket>"),IOBuffer::GET);
   }
   if(!send_buf) {
      send_buf=new IOBufferFDStream(new FDStream(sock,"<output-socket>"),IOBuffer::PUT);
      SendHandshake();
   }
   if(!peer_id) {
      // expect handshake
      unpack_status_t s=RecvHandshake();
      if(s==UNPACK_NO_DATA_YET)
	 return m;
      if(s!=UNPACK_SUCCESS) {
	 Disconnect();
	 return MOVED;
      }
      timeout_timer.Reset();
      if(parent->my_bitfield->has_any_set()) {
	 LogSend(5,"bitfield");
	 PacketBitField(parent->my_bitfield).Pack(send_buf);
      }
      int port=Torrent::listener->GetPort();
      LogSend(5,xstring::format("port(%d)",port));
      PacketPort(port).Pack(send_buf);

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

   if(!am_interested && HasNeededPieces()) {
      Packet(MSG_INTERESTED).Pack(send_buf);
      am_interested=true;
   }

   if(am_interested && !peer_choking && sent_queue.count()<MAX_QUEUE_LEN)
      SendDataRequests();

   if(am_choking && recv_queue.count()<MAX_QUEUE_LEN && choke_timer.Stopped()) {
      LogSend(5,"unchoke");
      Packet(MSG_UNCHOKE).Pack(send_buf);
      choke_timer.Reset();
      am_choking=false;
   }
   if(!am_choking && recv_queue.count()>MAX_QUEUE_LEN && choke_timer.Stopped()) {
      LogSend(5,"choke");
      Packet(MSG_CHOKE).Pack(send_buf);
      choke_timer.Reset();
      am_choking=true;
   }

   if(recv_queue.count()>0 && send_buf->Size()<(int)Torrent::BLOCK_SIZE*2
   && BytesAllowedToPut(recv_queue[0]->req_length))
      SendDataReply();

   if(recv_buf->Eof() && recv_buf->Size()==0) {
      LogError(4,"peer closed connection");
      Disconnect();
      return MOVED;
   }

   Packet *reply=0;
   unpack_status_t st=UnpackPacket(recv_buf,&reply);
   if(st==UNPACK_NO_DATA_YET)
      return m;
   if(st!=UNPACK_SUCCESS)
   {
      if(st==UNPACK_PREMATURE_EOF)
	 LogError(2,"peer unexpectedly closed connection");
      else
	 LogError(2,"invalid peer response format");
      Disconnect();
      return MOVED;
   }
   reply->DropData(recv_buf);
   HandlePacket(reply);

   if(sent_queue.count()>0 && sent_queue[0]->expire.Stopped()) {
      const PacketRequest *req=sent_queue[0];
      LogError(1,"request expired, piece:%u begin:%u size:%u",req->index,req->begin,req->req_length);
      ClearSentQueue(0);
   }

   return m;
}

TorrentPeer::unpack_status_t TorrentPeer::UnpackPacket(Ref<IOBuffer>& b,TorrentPeer::Packet **p)
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

const char *TorrentPeer::Packet::GetPacketTypeText()
{
   const char *const text_table[]={
      "keep-alive", "choke", "unchoke", "interested", "uninterested",
      "have", "bitfield", "request", "piece", "cancel", "port"
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
   if(b->Size()<length+4)
      return b->Eof()?UNPACK_PREMATURE_EOF:UNPACK_NO_DATA_YET;
   int t=b->UnpackUINT8(4);
   unpacked++;
   if(!is_valid_reply(t))
      return UNPACK_WRONG_FORMAT;
   type=(packet_type)t;
   return UNPACK_SUCCESS;
}

bool TorrentPeer::AddressEq(const TorrentPeer *o) const
{
   return !memcmp(&addr,&o->addr,sizeof(addr));
}

const char *TorrentPeer::GetName()
{
   return xstring::format("[%s]:%d",addr.address(),addr.port());
}

const char *TorrentPeer::Status()
{
   if(sock==-1)
      return "Not connected";
   if(!connected)
      return "Connecting...";
   if(!peer_id)
      return "Handshaking...";
   xstring &buf=xstring::format("dn:%s up:%s",
      peer_recv_rate.GetStr().get(),peer_send_rate.GetStr().get());
   if(peer_interested)
      buf.append(" peer-interested");
   if(peer_choking)
      buf.append(" peer-choking");
   if(am_interested)
      buf.append(" am-interested");
   if(am_choking)
      buf.append(" am-choking");
   buf.appendf(" sent-queue:%d",sent_queue.count());
   buf.appendf(" complete:%u/%u (%u%%)",peer_complete_pieces,parent->total_pieces,
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
void TorrentPeer::Packet::Pack(Ref<IOBuffer>& b)
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
void TorrentPeer::PacketBitField::Pack(Ref<IOBuffer>& b)
{
   Packet::Pack(b);
   b->Put((const char*)(bitfield->get()),bitfield->count());
}

TorrentPeer::PacketRequest::PacketRequest(unsigned i,unsigned b,unsigned l)
   : Packet(MSG_REQUEST), expire("torrent:request-timeout",0),
     index(i), begin(b), req_length(l)
{
   length+=12;
}
TorrentPeer::unpack_status_t TorrentPeer::PacketRequest::Unpack(const Buffer *b)
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
void TorrentPeer::PacketRequest::ComputeLength()
{
   Packet::ComputeLength();
   length+=12;
}
void TorrentPeer::PacketRequest::Pack(Ref<IOBuffer>& b)
{
   Packet::Pack(b);
   b->PackUINT32BE(index);
   b->PackUINT32BE(begin);
   b->PackUINT32BE(req_length);
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


TorrentListener::TorrentListener()
{
   sock=-1;
}
TorrentListener::~TorrentListener()
{
   if(sock!=-1)
      close(sock);
}
void TorrentListener::AddTorrent(Torrent *t)
{
   torrents.add(t->GetInfoHash(),t);
}
void TorrentListener::RemoveTorrent(Torrent *t)
{
   torrents.remove(t->GetInfoHash());
}
int TorrentListener::Do()
{
   int m=STALL;
   if(error)
      return m;
   if(sock==-1) {
      sock=SocketCreateTCP(AF_INET,0);
      // Try to assign a port from given range
      Range range(ResMgr::Query("torrent:port-range",0));
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

	 if(!port)
	     break;	// nothing to bind

	 addr.sa.sa_family=AF_INET;
	 addr.in.sin_port=htons(port);

	 if(addr.bind_to(sock)==0)
	    break;
	 int saved_errno=errno;

	 // Fail unless socket was already taken
	 if(errno!=EINVAL && errno!=EADDRINUSE)
	 {
	    LogError(0,"bind([%s]:%d): %s",addr.address(),port,strerror(saved_errno));
	    close(sock);
	    sock=-1;
	    if(NonFatalError(errno))
	    {
	       TimeoutS(1);
	       return m;
	    }
	    error=Error::Fatal("Cannot bind a socket for torrent:port-range");
	    return MOVED;
	 }
	 LogError(10,"bind([%s]:%d): %s",addr.address(),port,strerror(saved_errno));
      }
      listen(sock,5);

      // get the allocated port
      socklen_t addr_len=sizeof(addr);
      getsockname(sock,&addr.sa,&addr_len);
      m=MOVED;
   }

   if(rate.Get()>5)
   {
      TimeoutS(1);
      return m;
   }

   sockaddr_u remote_addr;
   socklen_t addr_len=sizeof(remote_addr);
   int a=accept(sock,&remote_addr.sa,&addr_len);
   if(a==-1) {
      Block(sock,POLLIN);
      return m;
   }
   rate.Add(1);
   LogNote(3,"Accepted connection from [%s]:%d",remote_addr.address(),remote_addr.port());
   (void)new TorrentDispatcher(a);
   m=MOVED;

   return m;
}

void TorrentListener::Dispatch(const xstring& info_hash,int sock,IOBuffer *recv_buf)
{
   Torrent *t=torrents.lookup(info_hash);
   if(!t) {
      LogError(1,"peer sent unknown info_hash=%s in handshake",info_hash.dump());
      close(sock);
      delete recv_buf;
      return;
   }
   t->Accept(sock,recv_buf);
}

TorrentDispatcher::TorrentDispatcher(int s)
   : sock(s),
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
      LogError(1,"peer handshake timeout");
      deleting=true;
      return MOVED;
   }

   unsigned proto_len=0;
   if(recv_buf->Size()>0)
      proto_len=recv_buf->UnpackUINT8();

   if((unsigned)recv_buf->Size()<1+proto_len+8+SHA1_DIGEST_SIZE) {
      if(recv_buf->Eof()) {
	 LogError(1,"peer short handshake");
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

   const Ref<TorrentListener>& listener=Torrent::GetListener();
   if(listener) {
      listener->Dispatch(peer_info_hash,sock,recv_buf.borrow());
      sock=-1;
   }
   deleting=true;
   return MOVED;
}

///
TorrentJob::TorrentJob(const char *mf)
   : torrent(new Torrent(mf)), done(false)
{
}
TorrentJob::~TorrentJob()
{
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
   return STALL;
}

void TorrentJob::PrintStatus(int v,const char *tab)
{
   printf("\t%s\n",torrent->Status());
   if(v>2)
      printf("\tinfo_hash: %s\n",torrent->GetInfoHash().dump());

   if(torrent->GetPeersCount()<=5 || v>1) {
      const TaskRefArray<TorrentPeer>& peers=torrent->GetPeers();
      for(int i=0; i<peers.count(); i++)
	 printf("\t  %s: %s\n",peers[i]->GetName(),peers[i]->Status());
   } else {
      printf("\t  peers:%d active:%d complete:%d\n",
	 torrent->GetPeersCount(),torrent->GetActivePeersCount(),
	 torrent->GetCompletePeersCount());
   }
}

void TorrentJob::ShowRunStatus(const SMTaskRef<StatusLine>& s)
{
   s->Show("%s",torrent->Status());
}

int TorrentJob::AcceptSig(int sig)
{
   if(!torrent)
      return WANTDIE;
   torrent->Shutdown();
   return MOVED;
}


#include "CmdExec.h"

CMD(torrent)
{
#define args (parent->args)
#define eprintf parent->eprintf

   return new TorrentJob(args->getarg(1));

#undef args
}

#include "modconfig.h"
#ifdef MODULE_CMD_TORRENT
void module_init()
{
   CmdExec::RegisterCommand("torrent",cmd_torrent);
}
#endif
