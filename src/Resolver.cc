/*
 * lftp and utils
 *
 * Copyright (c) 1996-2000 by Alexander V. Lukyanov (lav@yars.free.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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

#include "Resolver.h"
#include "SignalHook.h"
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <ctype.h>
#include <fcntl.h>

#include <netinet/in.h>
#ifdef HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
#ifdef HAVE_RESOLV_H
# include <resolv.h>
#endif

#include "xstring.h"
#include "xmalloc.h"
#include "ResMgr.h"
#include "log.h"

#ifndef T_SRV
# define T_SRV 33
#endif

#if !defined(HAVE_HSTRERROR_DECL)
extern "C" { const char *hstrerror(int); }
#endif

#ifndef HAVE_H_ERRNO_DECL
CDECL int h_errno;
#endif

#if defined(HAVE_RES_SEARCH) && !defined(HAVE_RES_SEARCH_DECL)
CDECL int res_search(const char*,int,int,unsigned char*,int);
#endif

#if INET6
# define DEFAULT_ORDER "inet inet6"
#else
# define DEFAULT_ORDER "inet"
#endif

static ResDecl
   res_cache_enable("dns:cache-enable", "yes", ResMgr::BoolValidate,0),
   res_cache_expire("dns:cache-expire", "24h", ResMgr::TimeIntervalValidate,0),
   res_cache_size  ("dns:cache-size",   "256", ResMgr::UNumberValidate,0),
   res_timeout	   ("dns:fatal-timeout","0",   ResMgr::UNumberValidate,0),
   res_order	   ("dns:order",	DEFAULT_ORDER, Resolver::OrderValidate,0),
   res_query_srv   ("dns:SRV-query",    "no",  ResMgr::BoolValidate,0),
   res_use_fork	   ("dns:use-fork",     "yes", ResMgr::BoolValidate,0);


struct address_family
{
   int number;
   const char *name;
};
static const address_family af_list[]=
{
   { AF_INET,  "inet"  },
#if INET6
   { AF_INET6, "inet6" },
#endif
   { -1, 0 }
};

ResolverCache *Resolver::cache;

Resolver::Resolver(const char *h,const char *p,const char *defp,
		   const char *ser,const char *pr)
{
   hostname=xstrdup(h);
   portname=xstrdup(p);
   port_number=0;

   service=xstrdup(ser);
   proto=xstrdup(pr);
   defport=xstrdup(defp);

   pipe_to_child[0]=pipe_to_child[1]=-1;
   w=0;
   err_msg=0;
   done=false;
   timeout=0;
   Reconfig();
   time(&start_time);
   addr=0;
   addr_num=0;
   buf=0;
   use_fork=res_use_fork.Query(0);

   error=0;

   no_cache=false;
}

Resolver::~Resolver()
{
   if(pipe_to_child[0]!=-1)
      close(pipe_to_child[0]);
   if(pipe_to_child[1]!=-1)
      close(pipe_to_child[1]);

   xfree(hostname);
   xfree(portname);
   xfree(service);
   xfree(proto);
   xfree(defport);

   xfree(err_msg);
   xfree(addr);
   if(w)
   {
      w->Kill(SIGKILL);
      w->Auto();
   }
   Delete(buf);
}

int   Resolver::Do()
{
   if(done)
      return STALL;

   int m=STALL;

   if(!no_cache)
   {
      const sockaddr_u *a;
      int n;
      cache->Find(hostname,portname,defport,service,proto,&a,&n);
      if(a && n>0)
      {
	 Log::global->Write(10,"dns cache hit\n");
	 addr_num=n;
	 addr=(sockaddr_u*)xmalloc(n*sizeof(*addr));
	 memcpy(addr,a,n*sizeof(*addr));
	 done=true;
	 return MOVED;
      }
      no_cache=true;
   }

   if(pipe_to_child[0]==-1)
   {
      int res=pipe(pipe_to_child);
      if(res==-1)
      {
	 if(errno==ENFILE || errno==EMFILE)
	 {
	    block+=TimeOut(1000);
	    return m;
	 }
	 MakeErrMsg("pipe()");
	 return MOVED;
      }
      fcntl(pipe_to_child[0],F_SETFL,O_NONBLOCK);
      fcntl(pipe_to_child[0],F_SETFD,FD_CLOEXEC);
      fcntl(pipe_to_child[1],F_SETFD,FD_CLOEXEC);
      m=MOVED;
      Log::global->Format(4,"---- %s\n",_("Resolving host address..."));
   }

   if(use_fork)
   {
      if(!w && !buf)
      {
	 pid_t proc=fork();
	 if(proc==-1)
	 {
	    block+=TimeOut(1000);
	    return m;
	 }
	 if(proc==0)
	 {	 // child
	    SignalHook::Ignore(SIGINT);
	    SignalHook::Ignore(SIGTSTP);
	    SignalHook::Ignore(SIGQUIT);
	    SignalHook::Ignore(SIGHUP);
	    close(0);
	    dup2(pipe_to_child[1],1);
	    close(pipe_to_child[1]);
	    close(pipe_to_child[0]);
	    DoGethostbyname();
	    _exit(0);
	 }
	 // parent
	 close(pipe_to_child[1]);
	 pipe_to_child[1]=-1;

	 w=new ProcWait(proc);
	 m=MOVED;
      }
      if(!buf)
      {
	 buf=new FileInputBuffer(new FDStream(pipe_to_child[0],"<pipe>"));
	 Roll(buf);
	 m=MOVED;
      }
   }
   else
   {
      if(!buf)
      {
	 buf=new Buffer();
	 DoGethostbyname();
	 if(deleting)
	    return MOVED;
      }
   }

   if(buf->Error())
   {
      err_msg=xstrdup(buf->ErrorText());
      done=true;
      return MOVED;
   }

   if(!buf->Eof())   // wait for all data to arrive (not too much)
   {
      if(timeout>0)
      {
	 if(now-start_time > timeout)
	 {
	    err_msg=xstrdup(_("host name resolve timeout"));
	    done=true;
	    return MOVED;
	 }
	 block+=TimeOut((timeout-(now-start_time))*1000);
      }
      return m;
   }

   const char *s;
   char c;
   int n;

   buf->Get(&s,&n);
   if(n<1)
      goto proto_error;
   c=*s;
   buf->Skip(1);
   buf->Get(&s,&n);
   if(c=='E' || c=='P') // error
   {
      const char *tport=portname?portname:defport;
      err_msg=(char*)xmalloc(strlen(hostname)+strlen(tport)+n+3);
      sprintf(err_msg,"%s: ",(c=='E'?hostname:tport));
      char *e=err_msg+strlen(err_msg);
      memcpy(e,s,n);
      e[n]=0;
      done=true;
      return MOVED;
   }
   if((unsigned)n<sizeof(sockaddr_u))
   {
   proto_error:
      // protocol error
      err_msg=xstrdup(_("child returned invalid data"));
      done=true;
      return MOVED;
   }
   addr_num=n/sizeof(*addr);
   addr=(sockaddr_u*)xmalloc(n);
   memcpy(addr,s,n);
   done=true;
   cache->Add(hostname,portname,defport,service,proto,addr,addr_num);
   return MOVED;
}

void Resolver::MakeErrMsg(const char *f)
{
   const char *e=strerror(errno);
   err_msg=(char*)xmalloc(strlen(e)+strlen(f)+3);
   sprintf(err_msg,"%s: %s",f,e);
   done=true;
}

void Resolver::AddAddress(int family,const char *address,int len)
{
   addr=(sockaddr_u*)xrealloc(addr,(addr_num+1)*sizeof(*addr));
   sockaddr_u *add=addr+addr_num;
   addr_num++;

   memset(add,0,sizeof(*add));

   add->sa.sa_family=family;
   switch(family)
   {
   case AF_INET:
      memcpy(&add->in.sin_addr,address,len);
      add->in.sin_port=port_number;
      break;

#if INET6
   case AF_INET6:
      memcpy(&add->in6.sin6_addr,address,len);
      add->in6.sin6_port=port_number;
      break;
#endif

   default:
      addr_num--;
      return;
   }
}

const char *Resolver::ParseOrder(const char *s,int *o)
{
   static char *error=0;

   const char * const delim="\t ";
   char *s1=alloca_strdup(s);
   int idx=0;

   for(s1=strtok(s1,delim); s1; s1=strtok(0,delim))
   {
      const address_family *f;
      for(f=af_list; f->name; f++)
      {
	 if(!strcasecmp(s1,f->name))
	    break;
      }
      if(!f->name)
      {
	 const char * const format=_("unknown address family `%s'");
	 error=(char*)xrealloc(error,strlen(format)+strlen(s1));
	 sprintf(error,format,s1);
	 return error;
      }
      if(idx<15)
      {
	 if(o) o[idx]=f->number;
	 idx++;
      }
   }
   if(o) o[idx]=-1;
   return 0;
}

const char *Resolver::OrderValidate(char **s)
{
   return ParseOrder(*s,0);
}

#ifdef HAVE_RES_SEARCH
static
int extract_domain(const unsigned char *answer,const unsigned char *scan,int len,
		     char *store,int store_len)
{
   int count=1;	  // reserve space for \0
   int refs=0;
   int consumed=0;
   const unsigned char *start=scan;
   for(;;)
   {
      if(len<=0)
	 break;
      int label_len=*scan;
      scan++;
      len--;

      if((label_len & 0xC0) == 0xC0)   // compression
      {
	 if(len<=0)
	    break;
	 int offset=((label_len&0x3F)<<8) + *scan;
	 scan++;
	 len--;

	 if(refs==0)
	    consumed=scan-start;

	 if(answer+offset>=scan+len)
	    break; // error

	 len=scan+len-answer+offset;
	 scan=answer+offset;
	 if(++refs > 256)
	    break;   // too many hops.
	 continue;
      }

      if(label_len==0)
	 break;

      while(label_len>0)
      {
	 if(len<=0)
	    break;
	 if(store && count<store_len)
	    *store++=*scan;
	 count++;
	 scan++;
	 len--;
	 label_len--;
      }
      if(store && count<store_len)
	 *store++='.';
      count++;
   }
   if(store)
      *store=0;
   if(refs==0)
      consumed=scan-start;
   return consumed;
}

struct SRV
{
   char domain[256];
   int port;
   int priority;
   int weight;
   int order;
};

static
int SRV_compare(const void *a,const void *b)
{
   struct SRV *sa=(struct SRV*)a;
   struct SRV *sb=(struct SRV*)b;
   if(sa->priority < sb->priority)
      return -1;
   if(sa->priority > sb->priority)
      return 1;
   if(sa->order < sb->order)
      return -1;
   if(sa->order > sb->order)
      return 1;
   if(sa->weight > sb->weight)
      return -1;
   if(sa->weight < sb->weight)
      return 1;
   return 0;
}
#endif // RES_SEARCH

void Resolver::LookupSRV_RR()
{
   if(!(bool)res_query_srv.Query(hostname))
      return;
#ifdef HAVE_RES_SEARCH
   const char *tproto=proto?proto:"tcp";
   time_t try_time;
   unsigned char answer[0x1000];
   char *srv_name=string_alloca(strlen(service)+1+strlen(tproto)+1+strlen(hostname)+1);
   sprintf(srv_name,"%s.%s.%s",service,tproto,hostname);

   int len;
   for(;;)
   {
      time(&try_time);
      len=res_search(srv_name, C_IN, T_SRV, (u_char*)answer, sizeof(answer));
      if(len>=0)
	 break;
#ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN)
	 return;
      time_t t=time(0);
      if(t-try_time<5)
	 sleep(5-(t-try_time));
#else // no h_errno
      return;
#endif
   }

   if(len>(int)sizeof(answer))
      len=sizeof(answer);

   if(len<12)
      return;

   int question_count=(answer[4]<<8)+answer[5];
   int answer_count  =(answer[6]<<8)+answer[7];

   // skip header
   unsigned char *scan=answer+12;
   len-=12;

   // skip questions section
   for( ; question_count>0; question_count--)
   {
      int dom_len=extract_domain(answer,scan,len,0,0);
      scan+=dom_len;
      len-=dom_len;
      if(len<4)
	 return;
      scan+=4;
      len-=4;
   }

   struct SRV *SRVs=0;
   int SRV_num=0;

   // now process answers section
   for( ; answer_count>0; answer_count--)
   {
      int dom_len=extract_domain(answer,scan,len,0,0);
      scan+=dom_len;
      len-=dom_len;
      if(len<8)
	 return;
      scan+=8;
      len-=8;  // skip type,class,ttl

      if(len<2)
	 return;

      int data_len=(scan[0]<<8)+scan[1];
      scan+=2;
      len-=2;

      if(len<data_len)
	 return;

      if(data_len<6)
	 return;

      SRV_num++;
      SRVs=(struct SRV*)xrealloc(SRVs,sizeof(*SRVs)*SRV_num);
      struct SRV *t=SRVs+SRV_num-1;

      t->priority=(scan[0]<<8)+scan[1];
      t->weight  =(scan[2]<<8)+scan[3];
      t->port    =(scan[4]<<8)+scan[5];

      t->order=0;

      scan+=6;
      len-=6;

      dom_len=extract_domain(answer,scan,len,t->domain,sizeof(t->domain));
      scan+=dom_len;
      len-=dom_len;
   }

   // now sort and randomize the list.
   qsort(SRVs,SRV_num,sizeof(*SRVs),SRV_compare);

   srand(time(0));

   struct SRV *SRVscan,*base=0;
   int curr_priority=-1;
   int weight_sum=0;
   for(SRVscan=SRVs; ; SRVscan++)
   {
      if(SRVscan-SRVs==SRV_num || SRVscan->priority!=curr_priority)
      {
	 if(base)
	 {
	    int o=1;
	    struct SRV *s;
	    while(weight_sum>0)
	    {
	       int r=int((float(rand())/RAND_MAX)*weight_sum);
	       if(r==weight_sum)
		  r=weight_sum-1;
	       int w=0;
	       for(s=base; s<SRVscan; s++)
	       {
		  if(s->order!=0)
		     continue;
		  w+=s->weight;
		  if(r<w)
		  {
		     s->order=o;
		     o++;
		     weight_sum-=s->weight;
		     break;
		  }
	       }
	    }
	 }
	 if(SRVscan-SRVs==SRV_num)
	    break;
	 base=SRVscan;
	 curr_priority=SRVscan->priority;
	 weight_sum=0;
      }
      weight_sum+=SRVscan->weight;
   }

   qsort(SRVs,SRV_num,sizeof(*SRVs),SRV_compare);

   int oldport=port_number;
   for(SRVscan=SRVs; SRVscan-SRVs<SRV_num; SRVscan++)
   {
      port_number=htons(SRVscan->port);
      LookupOne(SRVscan->domain);
   }
   port_number=oldport;

#endif // HAVE_RES_SEARCH
}

void Resolver::LookupOne(const char *name)
{
   time_t try_time;
   int af_index=0;
   int af_order[16];

   const char *order=res_order.Query(name);

   const char *proto_delim=strchr(name,',');
   if(proto_delim)
   {
      char *o=string_alloca(proto_delim-name+1);
      memcpy(o,name,proto_delim-name);
      o[proto_delim-name]=0;
      if(ParseOrder(o,0)==0) // check if the protocol name is valid.
	 order=o;
      name=proto_delim+1;
   }

   ParseOrder(order,af_order);

   for(;;)
   {
      if(!use_fork)
      {
	 Schedule();
	 if(deleting)
	    return;
      }

      time(&try_time);

      int af=af_order[af_index];
      if(af==-1)
	 break;

      struct hostent *ha;
#ifdef HAVE_GETHOSTBYNAME2
      ha=gethostbyname2(name,af);
#else
      if(af==AF_INET)
	 ha=gethostbyname(name);
      else
      {
	 af_index++;
	 goto next;
      }
#endif

      if(ha)
      {
	 const char * const *a;
	 for(a=ha->h_addr_list; *a; a++)
	    AddAddress(ha->h_addrtype, *a, ha->h_length);
	 af_index++;
	 goto next;
      }
#ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN)
#endif
      {
	 if(error==0)
	 {
#ifdef HAVE_H_ERRNO
	    error=hstrerror(h_errno);
#else
	    error="Host name lookup failure";
#endif
	 }
	 af_index++;
	 goto next; // try other address families
      }
      time_t t;
      if((t=time(0))-try_time<5)
      {
	 if(use_fork)
	    sleep(5-(t-try_time));
	 else
	    TimeoutS(5-(t-try_time));
      }
      else
      {
      next:
	 if(!use_fork)
	    block+=NoWait();
      }
   }
}

void Resolver::DoGethostbyname()
{
   if(port_number==0)
   {
      const char *tproto=proto?proto:"tcp";
      const char *tport=portname?portname:defport;

      if(isdigit((unsigned char)tport[0]))
	 port_number=htons(atoi(tport));
      else
      {
	 struct servent *se=getservbyname(tport,tproto);
	 if(se)
	    port_number=se->s_port;
	 else
	 {
	    buf->Put("P");
	    char *msg=string_alloca(64+strlen(tproto));
	    sprintf(msg,"no such %s service",tproto);
	    buf->Put(msg);
	    buf->PutEOF();
	    return;
	 }
      }
   }

   if(service && !portname && !isdigit((unsigned char)hostname[0]))
      LookupSRV_RR();

   if(!use_fork && deleting)
      return;

   LookupOne(hostname);

   if(!use_fork && deleting)
      return;

   if(!buf)
      buf=new FileOutputBuffer(new FDStream(1,"<pipe>"));

   if(addr_num==0)
   {
      buf->Put("E");
      if(error==0)
	 error="No address found";
      buf->Put(error);
      buf->PutEOF();
      return;
   }
   buf->Put("O");
   buf->Put((char*)addr,addr_num*sizeof(*addr));
   buf->PutEOF();

   if(use_fork)
   {
      while(buf->Size()>0)
	 Roll(buf);  // should flush quickly.
   }
   return;
}

void Resolver::Reconfig(const char *name)
{
   timeout = res_timeout.Query(0);
   if(!name || strncmp(name,"dns:",4))
      return;
   cache->Clear();
}

void Resolver::ClassInit()
{
   cache=new ResolverCache;
}


ResolverCache::ResolverCache()
{
   chain=0;
}
void ResolverCache::Add(const char *h,const char *p,const char *defp,
	 const char *ser,const char *pr,const sockaddr_u *a,int n)
{
   Entry **ptr=FindPtr(h,p,defp,ser,pr);
   if(ptr && *ptr)
   {
      // delete old
      Entry *next=(*ptr)->next;
      delete *ptr;
      *ptr=next;
   }
   chain=new Entry(chain,h,p,defp,ser,pr,a,n);
}
ResolverCache::Entry **ResolverCache::FindPtr(const char *h,const char *p,
	 const char *defp,const char *ser,const char *pr)
{
   CacheCheck();
   Entry **scan=&chain;
   while(*scan)
   {
      Entry *s=*scan;
      if(!xstrcmp(s->hostname,h)
      && !xstrcmp(s->portname,p)
      && !xstrcmp(s->defport,defp)
      && !xstrcmp(s->service,ser)
      && !xstrcmp(s->proto,pr))
	 return scan;
      scan=&s->next;
   }
   return 0;
}
void ResolverCache::Clear()
{
   while(chain)
   {
      Entry *next=chain->next;
      delete chain;
      chain=next;
   }
}
void ResolverCache::Find(const char *h,const char *p,const char *defp,
	 const char *ser,const char *pr,const sockaddr_u **a,int *n)
{
   *a=0;
   *n=0;

   // if cache is disabled for this host, return nothing.
   if(!(bool)res_cache_enable.Query(h))
      return;

   Entry **ptr=FindPtr(h,p,defp,ser,pr);
   if(ptr && *ptr)
   {
      Entry *s=*ptr;
      *a=s->addr;
      *n=s->addr_num;
   }
}

// FIXME: this function can be speed-optimized.
void ResolverCache::CacheCheck()
{
   int countlimit=res_cache_size.Query(0);
   int count=0;
   Entry **scan=&chain;
   while(*scan)
   {
      Entry *s=*scan;
      TimeInterval expire((const char *)res_cache_expire.Query(s->hostname));
      if((!expire.IsInfty() && SMTask::now-s->timestamp>expire.Seconds())
      || (count>=countlimit))
      {
	 *scan=s->next;
	 delete s;
	 continue;
      }
      scan=&s->next;
      count++;
   }
}
