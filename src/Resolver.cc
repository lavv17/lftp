/*
 * lftp and utils
 *
 * Copyright (c) 1996-2001 by Alexander V. Lukyanov (lav@yars.free.net)
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
# define class _class // workaround for FreeBSD 3.2.
# include <arpa/nameser.h>
# undef class
#endif
#ifdef HAVE_RESOLV_H
# include <resolv.h>
#endif

#include "xstring.h"
#include "xmalloc.h"
#include "ResMgr.h"
#include "log.h"
#include "plural.h"

#ifndef T_SRV
# define T_SRV 33
#endif

#if !defined(HAVE_HSTRERROR_DECL)
extern "C" { const char *hstrerror(int); }
#endif

#ifdef HAVE_H_ERRNO
#ifndef HAVE_H_ERRNO_DECL
CDECL int h_errno;
#endif
#endif

#if defined(HAVE_RES_SEARCH) && !defined(HAVE_RES_SEARCH_DECL)
CDECL int res_search(const char*,int,int,unsigned char*,int);
#endif

#if INET6
# define DEFAULT_ORDER "inet inet6"
#else
# define DEFAULT_ORDER "inet"
#endif


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

ResolverCache *Resolver::cache=new ResolverCache;

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
   use_fork=ResMgr::Query("dns:use-fork",0);

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
	 Log::global->Write(10,_("dns cache hit\n"));
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
	    TimeoutS(1);
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
	    TimeoutS(1);
	    return m;
	 }
	 if(proc==0)
	 {	 // child
	    SignalHook::Ignore(SIGINT);
	    SignalHook::Ignore(SIGTSTP);
	    SignalHook::Ignore(SIGQUIT);
	    SignalHook::Ignore(SIGHUP);
	    close(0);	// no input will be needed.
	    close(pipe_to_child[0]);
	    pipe_to_child[0]=-1;
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
	 buf=new IOBufferFDStream(new FDStream(pipe_to_child[0],"<pipe-in>"),IOBuffer::GET);
// 	 Roll(buf);
	 m=MOVED;
      }
   }
   else /* !use_fork */
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
	 TimeoutS(timeout-(now-start_time));
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
   Log::global->Format(4,plural("---- %d address$|es$ found\n",addr_num),addr_num);
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

int Resolver::FindAddressFamily(const char *name)
{
   for(const address_family *f=af_list; f->name; f++)
   {
      if(!strcasecmp(name,f->name))
	 return f->number;
   }
   return -1;
}

void Resolver::ParseOrder(const char *s,int *o)
{
   const char * const delim="\t ";
   char *s1=alloca_strdup(s);
   int idx=0;

   for(s1=strtok(s1,delim); s1; s1=strtok(0,delim))
   {
      int af=FindAddressFamily(s1);
      if(af!=-1 && idx<15)
      {
	 if(o) o[idx]=af;
	 idx++;
      }
   }
   if(o) o[idx]=-1;
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
   if(!(bool)ResMgr::Query("dns:SRV-query",hostname))
      return;
#ifdef HAVE_RES_SEARCH
   const char *tproto=proto?proto:"tcp";
   time_t try_time;
   unsigned char answer[0x1000];
   char *srv_name=string_alloca(strlen(service)+1+strlen(tproto)+1+strlen(hostname)+1);
   sprintf(srv_name,"_%s._%s.%s",service,tproto,hostname);

   int len;
   for(;;)
   {
      if(!use_fork)
      {
	 Schedule();
	 if(deleting)
	    return;
      }
      time(&try_time);
      len=res_search(srv_name, C_IN, T_SRV, answer, sizeof(answer));
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

   const char *order=ResMgr::Query("dns:order",name);

   const char *proto_delim=strchr(name,',');
   if(proto_delim)
   {
      char *o=string_alloca(proto_delim-name+1);
      memcpy(o,name,proto_delim-name);
      o[proto_delim-name]=0;
      // check if the protocol name is valid.
      if(FindAddressFamily(o)!=-1)
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

#if defined(HAVE_GETADDRINFO) && INET6 \
   && !defined(HAVE_GETHOSTBYNAME2) \
   && !defined(HAVE_GETIPNODEBYNAME)

      // getaddrinfo support by Brandon Hume
      struct addrinfo	    *ainfo=0,
			    *a_res,
			    a_hint;
      int		    ainfo_res;
      struct sockaddr	    *sockname;
      struct sockaddr_in    *inet_addr;
      struct sockaddr_in6   *inet6_addr;
      const char	    *addr_data;

      a_hint.ai_flags	    = AI_PASSIVE;
      a_hint.ai_family	    = PF_UNSPEC;
      a_hint.ai_socktype    = 0;
      a_hint.ai_protocol    = 0;
      a_hint.ai_addrlen	    = 0;
      a_hint.ai_canonname   = NULL;
      a_hint.ai_addr	    = NULL;
      a_hint.ai_next	    = NULL;

      ainfo_res	= getaddrinfo(name, NULL, &a_hint, &ainfo);

      if(ainfo_res == 0)
      {
        // by lav: add addresses in specified order.
	for(int af=af_order[af_index]; af!=-1; af=af_order[++af_index])
	{
	    for(a_res = ainfo; a_res != NULL; a_res = a_res->ai_next)
	    {
	       if(a_res->ai_family!=af)
		  continue;

	       sockname	= a_res->ai_addr;

	       switch(a_res->ai_family)
	       {
	       case AF_INET:
		  inet_addr	= (sockaddr_in *)sockname;
		  addr_data	= (const char *)&inet_addr->sin_addr.s_addr;
		  break;
	       case AF_INET6:
		  inet6_addr	= (sockaddr_in6 *)sockname;
		  addr_data	= (const char *)inet6_addr->sin6_addr.s6_addr;
		  break;
	       default:
		  continue;
	       }

	       AddAddress(a_res->ai_family, addr_data, a_res->ai_addrlen);
	    }
	 }

	 freeaddrinfo(ainfo);
	 break;
      }

      if(ainfo_res != EAI_AGAIN)
      {
	 error = gai_strerror(ainfo_res);
	 break;
      }

#else // !HAVE_GETADDRINFO

      int af=af_order[af_index];
      if(af==-1)
	 break;

      struct hostent *ha;
# if defined(HAVE_GETIPNODEBYNAME)
#  ifndef HAVE_H_ERRNO
#   define HAVE_H_ERRNO 1
#  endif
      int h_errno=0;
      ha=getipnodebyname(name,af,0,&h_errno);
# elif defined(HAVE_GETHOSTBYNAME2)
      ha=gethostbyname2(name,af);
# else
      if(af==AF_INET)
	 ha=gethostbyname(name);
      else
      {
	 af_index++;
	 continue;
      }
# endif

      if(ha)
      {
	 const char * const *a;
	 for(a=ha->h_addr_list; *a; a++)
	    AddAddress(ha->h_addrtype, *a, ha->h_length);
	 af_index++;
	 continue;
      }

# ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN)
# endif
      {
	 if(error==0)
	 {
# ifdef HAVE_H_ERRNO
	    error=hstrerror(h_errno);
# else
	    error=_("Host name lookup failure");
# endif
	 }
	 af_index++;
	 continue; // try other address families
      }
#endif /* HAVE_GETADDRINFO */

      time_t t;
      if((t=time(0))-try_time<5)
	 sleep(5-(t-try_time));
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
	    sprintf(msg,_("no such %s service"),tproto);
	    buf->Put(msg);
	    goto flush;
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
      buf=new IOBufferFDStream(new FDStream(pipe_to_child[1],"<pipe-out>"),IOBuffer::PUT);

   if(addr_num==0)
   {
      buf->Put("E");
      if(error==0)
	 error=_("No address found");
      buf->Put(error);
      goto flush;
   }
   buf->Put("O");
   buf->Put((char*)addr,addr_num*sizeof(*addr));
   xfree(addr);
   addr=0;

flush:
   buf->PutEOF();
   if(use_fork)
   {
      while(buf->Size()>0 && !buf->Error() && !buf->Broken())
	 Roll(buf);  // should flush quickly.
   }
}

void Resolver::Reconfig(const char *name)
{
   timeout = ResMgr::Query("dns:fatal-timeout",hostname);
   if(!name || strncmp(name,"dns:",4))
      return;
   cache->Clear();
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
   if(!(bool)ResMgr::Query("dns:cache-enable",h))
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
   int countlimit=ResMgr::Query("dns:cache-size",0);
   int count=0;
   Entry **scan=&chain;
   while(*scan)
   {
      Entry *s=*scan;
      TimeInterval expire((const char *)ResMgr::Query("dns:cache-expire",s->hostname));
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
