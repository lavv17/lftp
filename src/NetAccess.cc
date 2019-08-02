/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2017 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <errno.h>
#include <assert.h>
#include <cmath>
#include <sys/types.h>

#include "NetAccess.h"
#include "log.h"
#include "url.h"
#include "LsCache.h"
#include "misc.h"
#include "Speedometer.h"

#define super FileAccess

xmap_p<NetAccess::SiteData> NetAccess::site_data;

void NetAccess::Init()
{
   resolver=0;
   idle_timer.SetResource("net:idle",0);
   timeout_timer.SetResource("net:timeout",0);
   max_persist_retries=0;
   persist_retries=0;
   socket_buffer=0;
   socket_maxseg=0;

   peer_curr=0;

   reconnect_interval=30;  // retry with 30 second interval
   reconnect_interval_multiplier=1.2;
   reconnect_interval_max=300;

   rate_limit=0;

   connection_limit=0;	// no limit.
   connection_takeover=false;

   Reconfig(0);
   reconnect_interval_current=reconnect_interval;
}

NetAccess::NetAccess()
{
   Init();
}
NetAccess::NetAccess(const NetAccess *o) : super(o)
{
   Init();
   if(o->peer)
   {
      peer.set(o->peer);
      peer_curr=o->peer_curr;
      if(peer_curr>=peer.count())
	 peer_curr=0;
   }
   home_auto.set(o->home_auto);
}
NetAccess::~NetAccess()
{
   ClearPeer();
}

void NetAccess::Cleanup()
{
   if(hostname==0)
      return;

   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
      fo->CleanupThis();

   CleanupThis();
}

void NetAccess::CleanupThis()
{
   if(!IsConnected() || mode!=CLOSED)
      return;
   Disconnect();
}

void NetAccess::Reconfig(const char *name)
{
   super::Reconfig(name);

   const char *c=hostname;

   reconnect_interval = ResMgr::Query("net:reconnect-interval-base",c);
   reconnect_interval_multiplier = ResMgr::Query("net:reconnect-interval-multiplier",c);
   if(reconnect_interval_multiplier<1)
      reconnect_interval_multiplier=1;
   reconnect_interval_max = ResMgr::Query("net:reconnect-interval-max",c);
   if(reconnect_interval_max<reconnect_interval)
      reconnect_interval_max=reconnect_interval;
   max_retries = ResMgr::Query("net:max-retries",c);
   max_persist_retries = ResMgr::Query("net:persist-retries",c);
   socket_buffer = ResMgr::Query("net:socket-buffer",c);
   socket_maxseg = ResMgr::Query("net:socket-maxseg",c);
   connection_limit = ResMgr::Query("net:connection-limit",c);
   connection_takeover = ResMgr::QueryBool("net:connection-takeover",c);

   if(rate_limit)
      rate_limit->Reconfig(name,c);
}

const char *NetAccess::CheckHangup(const struct pollfd *pfd,int num)
{
   for(int i=0; i<num; i++)
   {
#ifdef SO_ERROR
      int s_errno=0;
      errno=0;
      socklen_t len=sizeof(s_errno);
      getsockopt(pfd[i].fd,SOL_SOCKET,SO_ERROR,(char*)&s_errno,&len);
// Where does the error number go - to errno or to the pointer?
// It seems that to errno, but if the pointer is NULL it dumps core.
// (solaris 2.5)
// It seems to be different on glibc 2.0 - check both errno and s_errno
      if((errno!=0 || s_errno!=0) && errno!=ENOTSOCK)
	 return strerror(errno?errno:s_errno);
#endif /* SO_ERROR */
      if(pfd[i].revents&POLLERR)
	 return "POLLERR";
   } /* end for */
   return 0;
}
int NetAccess::Poll(int fd,int ev,const char **err)
{
   struct pollfd pfd;
   pfd.fd=fd;
   pfd.events=ev;
   pfd.revents=0;
   int res=poll(&pfd,1,0);
   if(res<1)
      return 0;
   *err=CheckHangup(&pfd,1);
   if(*err)
      return -1;
   if(pfd.revents)
      timeout_timer.Reset();
   return pfd.revents;
}

void NetAccess::SayConnectingTo()
{
   assert(peer_curr<peer.count());
   const char *h=(proxy?proxy:hostname);
   LogNote(1,_("Connecting to %s%s (%s) port %u"),proxy?"proxy ":"",
      h,SocketNumericAddress(&peer[peer_curr]),SocketPort(&peer[peer_curr]));
}

void NetAccess::SetProxy(const char *px)
{
   bool was_proxied=(proxy!=0);

   proxy.set(0);
   proxy_port.set(0);
   proxy_user.set(0);
   proxy_pass.set(0);
   proxy_proto.set(0);

   if(!px)
      px="";

   ParsedURL url(px);
   if(!url.host || url.host[0]==0)
   {
      if(was_proxied)
	 ClearPeer();
      return;
   }

   proxy.set(url.host);
   proxy_port.set(url.port);
   proxy_user.set(url.user);
   proxy_pass.set(url.pass);
   proxy_proto.set(url.proto);
   ClearPeer();
}

bool NetAccess::NoProxy(const char *hostname)
{
   // match hostname against no-proxy var.
   if(!hostname)
      return false;
   const char *no_proxy_c=ResMgr::Query("net:no-proxy",0);
   if(!no_proxy_c)
      return false;
   char *no_proxy=alloca_strdup(no_proxy_c);
   int h_len=strlen(hostname);
   for(char *p=strtok(no_proxy," ,"); p; p=strtok(0," ,"))
   {
      int p_len=strlen(p);
      if(p_len>h_len || p_len==0)
	 continue;
      if(!strcasecmp(hostname+h_len-p_len,p))
	 return true;
   }
   return false;
}

void NetAccess::HandleTimeout()
{
   LogError(0,_("Timeout - reconnecting"));
   Disconnect();
   timeout_timer.Reset();
}

bool NetAccess::CheckTimeout()
{
   if(timeout_timer.Stopped())
   {
      HandleTimeout();
      return(true);
   }
   return(false);
}

void NetAccess::ClearPeer()
{
   peer.unset();
   peer_curr=0;
}

void NetAccess::NextPeer()
{
   peer_curr++;
   if(peer_curr>=peer.count())
      peer_curr=0;
   else
   {
      if(retries>0)
	 retries--;
      DontSleep(); // try next address immediately
   }
}

void NetAccess::ResetLocationData()
{
   Disconnect();
   ClearPeer();
   super::ResetLocationData();
   timeout_timer.SetResource("net:timeout",hostname);
   idle_timer.SetResource("net:idle",hostname);
}

void NetAccess::Open(const char *fn,int mode,off_t offs)
{
   timeout_timer.Reset();
   super::Open(fn,mode,offs);
}

int NetAccess::Resolve(const char *defp,const char *ser,const char *pr)
{
   int m=STALL;

   if(!resolver)
   {
      peer.unset();
      if(proxy)
	 resolver=new Resolver(proxy,proxy_port,defp);
      else
	 resolver=new Resolver(hostname,portname,defp,ser,pr);
      if(!resolver)
	 return MOVED;
      resolver->Roll();
      m=MOVED;
   }

   if(!resolver->Done())
      return m;

   if(resolver->Error())
   {
      SetError(LOOKUP_ERROR,resolver->ErrorMsg());
      return(MOVED);
   }

   peer.set(resolver->Result());
   if(peer_curr>=peer.count())
      peer_curr=0;

   resolver=0;
   return MOVED;
}

bool NetAccess::ReconnectAllowed()
{
   if(max_retries>0 && retries>=max_retries)
      return true; // it will fault later - no need to wait.
   int connection_limit=GetConnectionLimit();
   if(connection_limit>0 && connection_limit<=CountConnections())
      return false;
   if(reconnect_timer.Stopped())
      return true;
   return false;
}

const char *NetAccess::DelayingMessage()
{
   int connection_limit=GetConnectionLimit();
   if(connection_limit>0 && connection_limit<=CountConnections())
      return _("Connection limit reached");
   long remains=reconnect_timer.TimeLeft();
   if(remains<=0)
      return "";
   current->TimeoutS(1);
   if(last_disconnect_cause && reconnect_timer.TimePassed()<5)
      return last_disconnect_cause;
   return xstring::format("%s: %ld",_("Delaying before reconnect"),remains);
}

bool NetAccess::NextTry()
{
   if(!CheckRetries())
      return false;
   if(retries==0)
      reconnect_interval_current=reconnect_interval;
   else if(reconnect_interval_multiplier>1)
   {
      reconnect_interval_current*=reconnect_interval_multiplier;
      if(reconnect_interval_current>reconnect_interval_max)
	 reconnect_interval_current=reconnect_interval_max;
   }
   retries++;
   LogNote(10,"attempt number %d (max_retries=%d)",retries,max_retries);
   return CheckRetries();
}
bool NetAccess::CheckRetries()
{
   if(max_retries>0 && retries>max_retries)
   {
      if(!IsConnected() && last_disconnect_cause)
	 Fatal(xstring::cat(_("max-retries exceeded")," (",last_disconnect_cause.get(),")",NULL));
      else
	 Fatal(_("max-retries exceeded"));
      return false;
   }
   reconnect_timer.Set(reconnect_interval_current);
   return true;
}
void NetAccess::TrySuccess()
{
   retries=0;
   persist_retries=0;
   reconnect_interval_current=reconnect_interval;
}

void NetAccess::Close()
{
   if(mode!=CLOSED)
      idle_timer.Reset();

   TrySuccess();
   resolver=0;
   super::Close();
}

int NetAccess::CountConnections()
{
   int count=0;
   for(FileAccess *o=FirstSameSite(); o!=0; o=NextSameSite(o))
   {
      if(o->IsConnected())
	 count++;
   }
   return count;
}

void NetAccess::PropagateHomeAuto()
{
   if(!home_auto)
      return;
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      NetAccess *o=(NetAccess*)fo; // we are sure it is NetAccess.
      if(!o->home_auto)
      {
	 o->home_auto.set(home_auto);
	 if(!o->home)
	    o->set_home(home_auto);
      }
   }
}
const char *NetAccess::FindHomeAuto()
{
   for(FA *fo=FirstSameSite(); fo!=0; fo=NextSameSite(fo))
   {
      NetAccess *o=(NetAccess*)fo; // we are sure it is NetAccess.
      if(o->home_auto)
	 return o->home_auto;
   }
   return 0;
}


// GenericParseListInfo implementation
int GenericParseListInfo::Do()
{
#define need_size (need&FileInfo::SIZE)
#define need_time (need&FileInfo::DATE)

   FileInfo *file;
   int res;
   int m=STALL;
   int old_mode=mode;
   Ref<FileSet> set;

do_again:
   if(done)
      return m;

   if(redir_resolution) {
      if(redir_session && redir_session->OpenMode()==FA::ARRAY_INFO) {
	 res=redir_session->Done();
	 if(res==FA::DO_AGAIN || res==FA::IN_PROGRESS)
	    return m;
	 redir_session->Close();
	 redir_fs->rewind();
	 FileInfo *fi=redir_fs->curr();
	 if(ResolveRedirect(fi))
	    return MOVED;
	 result->curr()->MergeInfo(*fi,~0U);
	 result->next();
      }
      redir_count=0;
      for(FileInfo *fi=result->curr(); fi; fi=result->next()) {
	 if(ResolveRedirect(fi))
	    return MOVED;
      }

      FileAccess::cache->UpdateFileSet(session,"",FA::MP_LIST,result);
      FileAccess::cache->UpdateFileSet(session,"",FA::LONG_LIST,result);
      done=true;
      return MOVED;
   }

   if(session->OpenMode()==FA::ARRAY_INFO)
   {
      res=session->Done();
      if(res==FA::DO_AGAIN)
	 return m;
      if(res==FA::IN_PROGRESS)
	 return m;
      session->Close();
      // start redirection resolution.
      redir_resolution=true;
      result->rewind();
      m=MOVED;
      goto do_again;
   }

   if(!ubuf)
   {
      const char *cache_buffer=0;
      int cache_buffer_size=0;
      const FileSet *cache_fset=0;
      int err;
      if(use_cache && FileAccess::cache->Find(session,"",mode,&err,
				    &cache_buffer,&cache_buffer_size,&cache_fset))
      {
	 if(err)
	 {
	    if(mode==FA::MP_LIST)
	    {
	       mode=FA::LONG_LIST;
	       goto do_again;
	    }
	    SetErrorCached(cache_buffer);
	    return MOVED;
	 }
	 if(cache_fset) {
	    Log::global->Write(11,"ListInfo: using cached file set\n");
	    set=new FileSet(cache_fset);
	    old_mode=mode;
	    goto got_fileset;
	 }
	 ubuf=new IOBuffer(IOBuffer::GET);
	 ubuf->Put(cache_buffer,cache_buffer_size);
	 ubuf->PutEOF();
      }
      else
      {
	 session->Open("",mode);
	 session->UseCache(use_cache);
	 ubuf=new IOBufferFileAccess(session);
	 ubuf->SetSpeedometer(new Speedometer());
	 if(FileAccess::cache->IsEnabled(session->GetHostName()))
	    ubuf->Save(FileAccess::cache->SizeLimit());
	 session->Roll();
	 ubuf->Roll();
      }
      m=MOVED;
   }
   if(ubuf)
   {
      if(ubuf->Error())
      {
	 FileAccess::cache->Add(session,"",mode,session->GetErrorCode(),ubuf);
	 if(mode==FA::MP_LIST)
	 {
	    mode=FA::LONG_LIST;
	    ubuf=0;
	    m=MOVED;
	    goto do_again;
	 }
	 SetError(ubuf->ErrorText());
	 ubuf=0;
	 return MOVED;
      }

      if(!ubuf->Eof())
	 return m;

      // now we have all the index in ubuf; parse it.
      const char *b;
      int len;
      ubuf->Get(&b,&len);
      old_mode=mode;
      set=Parse(b,len);

      // cache the list and the set.
      FileAccess::cache->Add(session,"",old_mode,FA::OK,ubuf,set);

got_fileset:
      if(set)
      {
	 set->rewind();
	 for(file=set->curr(); file!=0; file=set->next())
	 {
	    // tilde is special.
	    if(file->name[0]=='~')
	    {
	       // can't just update the name - it will break sorting
	       file=set->borrow_curr();
	       file->name.set_substr(0,0,"./");
	       if(!result)
		  result=new FileSet();
	       result->Add(file);
	    }
	 }
	 if(result)
	 {
	    result->Merge(set);
	    set=0; // free it now.
	 }
	 else
	    result=set.borrow();
      }

      ubuf=0;
      m=MOVED;

      // try another mode? Parse() can set mode to indicate it wants to try it.
      if(mode!=old_mode)
	 return m;

      if(!result)
	 result=new FileSet;

      if(exclude)
	 result->Exclude(exclude_prefix,exclude,excluded.get_non_const());
      result->rewind();
      for(file=result->curr(); file!=0; file=result->next())
      {
	 file->need=0;
	 if(need_size && !file->Has(file->SIZE))
	    file->Need(file->SIZE);
	 if(need_time && (!file->Has(file->DATE)
                          || (file->date.ts_prec>0 && can_get_prec_time)))
	    file->Need(file->DATE);

	 if(file->defined & file->TYPE)
	 {
	    if(file->filetype==file->SYMLINK && follow_symlinks)
	    {
	       file->filetype=file->UNKNOWN;
	       file->defined &= ~(file->SIZE|file->SYMLINK_DEF|file->MODE|file->DATE|file->TYPE);
	       file->Need(file->SIZE|file->DATE);
	    }
	    else if(file->filetype==file->SYMLINK)
	    {
	       // don't need these for symlinks
	       file->NoNeed(file->SIZE|file->DATE);
	       // but need the link target
	       if(!file->Has(file->SYMLINK_DEF))
		  file->Need(file->SYMLINK_DEF);
	    }
	    else if(file->filetype==file->DIRECTORY)
	    {
	       if(!get_time_for_dirs)
		  continue;
	       // don't need size for directories
	       file->NoNeed(file->SIZE);
	    }
	 }
      }
      session->GetInfoArray(result.get_non_const());
      session->Roll();
   }
   return m;
}

bool GenericParseListInfo::ResolveRedirect(const FileInfo *fi)
{
   if(fi->filetype!=fi->REDIRECT || redir_count>=max_redir)
      return false;

   redir_count++;
   Log::global->Format(9,"ListInfo: resolving redirection %s -> %s\n",fi->name.get(),fi->GetRedirect());

   Ref<FileInfo> redir_fi(new FileInfo());
   redir_fi->Need(fi->need);

   xstring loc(fi->GetRedirect());
   ParsedURL u(loc,true);
   if(!u.proto) {
      // relative URI
      redir_session=session->Clone();
      if(loc[0]=='/' || fi->uri) {
	 if(loc[0]!='/') {
	    const char *slash=strrchr(fi->uri,'/');
	    if(slash)
	       loc.prepend(fi->uri,slash+1-fi->uri);
	 }
	 redir_fi->uri.set(loc);
	 redir_fi->name.set(loc);
	 redir_fi->name.url_decode();
      } else {
	 loc.url_decode();
	 const char *slash=strrchr(fi->name,'/');
	 if(slash)
	    redir_fi->name.nset(fi->name,slash+1-fi->name);
	 redir_fi->name.append(loc);
      }
   } else { // u.proto
      // absolute URL
      redir_session=FileAccess::New(&u);
      redir_fi->name.set(u.path?u.path.get():"/");
      redir_fi->uri.set(url::path_ptr(u.orig_url));
   }

   if(!redir_fs)
      redir_fs=new FileSet();
   else
      redir_fs->Empty();
   redir_fs->Add(redir_fi.borrow());
   redir_session->GetInfoArray(redir_fs.get_non_const());
   redir_session->Roll();

   return true;
}

GenericParseListInfo::GenericParseListInfo(FileAccess *s,const char *p)
   : ListInfo(s,p), redir_resolution(false), redir_count(0),
     max_redir(ResMgr::Query("xfer:max-redirections",0))
{
   get_time_for_dirs=true;
   can_get_prec_time=true;
   mode=FA::MP_LIST;
}

const char *GenericParseListInfo::Status()
{
   if(ubuf && !ubuf->Eof() && session->IsOpen())
      return xstring::format("%s (%lld) %s[%s]",_("Getting directory contents"),
		     (long long)session->GetPos(),
		     ubuf->GetRateStrS(),session->CurrentStatus());
   if(session->OpenMode()==FA::ARRAY_INFO)
      return xstring::format("%s (%d%%) [%s]",_("Getting files information"),
		     session->InfoArrayPercentDone(),
		     session->CurrentStatus());
   return "";
}

CDECL void lftp_network_cleanup()
{
   NetAccess::ClassCleanup();
   RateLimit::ClassCleanup();
}
