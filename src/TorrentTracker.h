/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2014 by Alexander V. Lukyanov (lav@yars.free.net)
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

#ifndef TORRENTTRACKER_H
#define TORRENTTRACKER_H

#include "url.h"

class TrackerBackend;
class TorrentTracker : public SMTask, protected ProtoLog
{
   friend class Torrent;
   friend class TrackerBackend;

   Torrent *parent;

   xarray_p<xstring> tracker_urls;
   int current_tracker;
   SMTaskRef<TrackerBackend> backend;
   Timer tracker_timer;
   Timer tracker_timeout_timer;
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

   void SetError(const char *e);
   bool Failed() const { return error!=0 || tracker_urls.count()==0; }
   const char *ErrorText() const { return error->Text(); }

   void NextTracker();
   void CreateTrackerBackend();
   const char *GetLogContext() { return GetURL(); }

public:
   ~TorrentTracker() {}
   const char *NextRequestIn() const {
      return tracker_timer.TimeLeft().toString(
	 TimeInterval::TO_STR_TRANSLATE|TimeInterval::TO_STR_TERSE);
   }
   const char *GetURL() const {
      return tracker_urls[current_tracker]->get();
   }
   const char *Status() const;
   bool IsActive() const;
   void TrackerRequestFinished() { tracker_timer.Reset(); }
   void SetInterval(unsigned i) {
      if(i<30)
	 i=30;
      tracker_timer.Set(i);
      LogNote(4,"Tracker interval is %u",i);
   }
   void SetTrackerID(const xstring& id) {
      if(id)
	 tracker_id.set(id);
   }
   bool AddPeerCompact(const char *a,int len) const;
   bool AddPeer(const xstring& addr,int port) const;
   bool ShuttingDown();
};

class TrackerBackend : public SMTask, protected ProtoLog
{
protected:
   TorrentTracker *master;
   void SetError(const char *e) { master->SetError(e); }

   const char *GetURL() const { return master->GetURL(); }
   const xstring& GetInfoHash() const;
   const xstring& GetMyPeerId() const;
   int GetPort() const;
   unsigned long long GetTotalSent() const;
   unsigned long long GetTotalRecv() const;
   unsigned long long GetTotalLeft() const;
   bool HasMetadata() const;
   bool Complete() const;
   int GetWantedPeersCount() const;
   const xstring& GetMyKey() const;
   unsigned GetMyKeyNum() const;
   const char *GetTrackerId() const;
   void SetTrackerID(const xstring& id) const { master->SetTrackerID(id); }
   void SetInterval(unsigned i) const { master->SetInterval(i); }
   bool AddPeerCompact(const char *a,int len) const { return master->AddPeerCompact(a,len); }
   bool AddPeer(const xstring& addr,int port) const { return master->AddPeer(addr,port); }
   void NextTracker() const { master->NextTracker(); }
   bool ShuttingDown() const;
   void Started() const;
   void TrackerRequestFinished() const;
   const char *GetLogContext() { return master->GetLogContext(); }

public:
   TrackerBackend(TorrentTracker *m) : master(m) {}
   virtual ~TrackerBackend() {}
   virtual bool IsActive() const = 0;
   virtual void SendTrackerRequest(const char *event) = 0;
   virtual const char *Status() const = 0;
};
class HttpTracker : public TrackerBackend
{
   FileAccessRef t_session;
   SMTaskRef<IOBuffer> tracker_reply;
   int HandleTrackerReply();
public:
   bool IsActive() const { return tracker_reply!=0; }
   void SendTrackerRequest(const char *event);
   HttpTracker(TorrentTracker *m,ParsedURL *u)
      : TrackerBackend(m), t_session(FileAccess::New(u)) {}
   ~HttpTracker() {}
   int Do();
   const char *Status() const { return t_session->CurrentStatus(); }
};
class UdpTracker : public TrackerBackend, protected Networker
{
   xstring_c hostname;
   xstring_c portname;

   SMTaskRef<Resolver> resolver;

   xarray<sockaddr_u> peer;
   int peer_curr;
   void	 NextPeer();

   int sock;   // udp socket for packet exchange

   Timer timeout_timer;
   int try_number;   // timeout = 60 * 2^try_number

   bool has_connection_id;
   unsigned long long connection_id;

   enum action_t {
      a_none=-1,
      a_connect=0,
      a_announce=1,
      a_scrape=2,
      a_error=3,
      a_announce6=4,
   };
   enum event_t {
      ev_idle=-1,
      ev_none=0,
      ev_completed=1,
      ev_started=2,
      ev_stopped=3,
   };
   enum magic_t {
      connect_magic=0x41727101980ULL,
   };
   static const char *EventToString(event_t e);

   unsigned transaction_id;
   action_t current_action;
   event_t current_event;

   bool SendPacket(Buffer& req);
   bool SendConnectRequest();
   bool SendEventRequest();
   bool RecvReply();

   unsigned NewTransactionId() { return transaction_id=random(); }

public:
   UdpTracker(TorrentTracker *m,ParsedURL *u)
      : TrackerBackend(m),
        hostname(u->host.get()), portname(u->port.get()),
        peer_curr(0), sock(-1), timeout_timer(60), try_number(0),
        has_connection_id(false), connection_id(0),
	current_action(a_none), current_event(ev_idle) {}
   ~UdpTracker() {
      if(sock!=-1)
	 close(sock);
   }
   int Do();
   bool IsActive() const { return current_event!=ev_idle; }
   void SendTrackerRequest(const char *event);
   const char *Status() const;
};

#endif // TORRENTTRACKER_H
