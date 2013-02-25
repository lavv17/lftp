/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2013 by Alexander V. Lukyanov (lav@yars.free.net)
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
      if(i>30) {
	 tracker_timer.Set(i);
	 LogNote(4,"Tracker interval is %u",i);
      }
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
   const char *GetInfoHash() const;
   const char *GetMyPeerId() const;
   int GetPort() const;
   unsigned long long GetTotalSent() const;
   unsigned long long GetTotalRecv() const;
   unsigned long long GetTotalLeft() const;
   bool HasMetadata() const;
   int GetWantedPeersCount() const;
   const xstring& GetMyKey() const;
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
class UdpTracker : public TrackerBackend
{
};

#endif // TORRENTTRACKER_H
