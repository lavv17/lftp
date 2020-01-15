/*
 * lftp - file transfer program
 *
 * Copyright (c) 1996-2020 by Alexander V. Lukyanov (lav@yars.free.net)
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

#include <stddef.h>
#include "Resolver.h"
#include "SignalHook.h"
#include <errno.h>
#include <unistd.h>
#include "trio.h"
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
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

#if LIBIDN2
# include <idn2.h>
#endif

#ifdef DNSSEC_LOCAL_VALIDATION
# include "validator/validator.h"
#endif

#include "xstring.h"
#include "ResMgr.h"
#include "log.h"
#include "plural.h"

#ifndef C_IN
# define C_IN 1
#endif
#ifndef T_SRV
# define T_SRV 33
#endif

#if !HAVE_DECL_HSTRERROR
extern "C" { const char *hstrerror(int); }
#endif

#ifdef HAVE_H_ERRNO
#if !HAVE_DECL_H_ERRNO
CDECL int h_errno;
#endif
#endif

#if HAVE_RES_SEARCH && !HAVE_DECL_RES_SEARCH
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

ResolverCache *Resolver::cache;


Resolver::Resolver(const char *h,const char *p,const char *defp,
		   const char *ser,const char *pr)
   : hostname(h), portname(p), service(ser), proto(pr), defport(defp)
{
   port_number=0;

   pipe_to_child[0]=pipe_to_child[1]=-1;
   done=false;
   timeout_timer.SetResource("dns:fatal-timeout",hostname);
   Reconfig();
   use_fork=ResMgr::QueryBool("dns:use-fork",0);

   error=0;

   no_cache=false;
}

Resolver::~Resolver()
{
   if(pipe_to_child[0]!=-1)
      close(pipe_to_child[0]);
   if(pipe_to_child[1]!=-1)
      close(pipe_to_child[1]);

   if(w)
   {
      w->Kill(SIGKILL);
      w.borrow()->Auto();
   }
}

int   Resolver::Do()
{
   if(done)
      return STALL;

   int m=STALL;

   if(!no_cache && cache)
   {
      const sockaddr_u *a;
      int n;
      cache->Find(hostname,portname,defport,service,proto,&a,&n);
      if(a && n>0)
      {
	 LogNote(10,"dns cache hit");
	 addr.nset(a,n);
	 done=true;
	 return MOVED;
      }
      no_cache=true;
   }

   if(use_fork)
   {
      if(pipe_to_child[0]==-1)
      {
	 int res=pipe(pipe_to_child);
	 if(res==-1)
	 {
	    if(NonFatalError(errno))
	       return m;
	    MakeErrMsg("pipe()");
	    return MOVED;
	 }
	 fcntl(pipe_to_child[0],F_SETFL,O_NONBLOCK);
	 fcntl(pipe_to_child[0],F_SETFD,FD_CLOEXEC);
	 fcntl(pipe_to_child[1],F_SETFD,FD_CLOEXEC);
	 m=MOVED;
	 LogNote(4,_("Resolving host address..."));
      }

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
	    buf=new IOBufferFDStream(new FDStream(pipe_to_child[1],"<pipe-out>"),IOBuffer::PUT);
	    DoGethostbyname();
	    buf->PutEOF();
	    while(buf->Size()>0 && !buf->Error() && !buf->Broken())
	       buf->Roll();  // should flush quickly.
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
	 LogNote(4,_("Resolving host address..."));
	 buf=new IOBuffer(IOBuffer::GET);
	 DoGethostbyname();
	 if(Deleted())
	    return MOVED;
      }
   }

   if(buf->Error())
   {
      err_msg.set(buf->ErrorText());
      done=true;
      return MOVED;
   }

   if(!buf->Eof())   // wait for all data to arrive (not too much)
   {
      if(timeout_timer.Stopped())
      {
	 err_msg.set(_("host name resolve timeout"));
	 done=true;
	 return MOVED;
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
      const char *tport=portname?portname.get():defport.get();
      err_msg.vset(c=='E'?hostname.get():tport,": ",s,NULL);
      done=true;
      return MOVED;
   }
   if((unsigned)n<addr.get_element_size())
   {
   proto_error:
      if(use_fork)
      {
	 // e.g. under gdb child fails.
	 LogError(4,"child failed, retrying with dns:use-fork=no");
	 use_fork=false;
	 buf=0;
	 return MOVED;
      }
      err_msg.set("BUG: internal class Resolver error");
      done=true;
      return MOVED;
   }
   addr.nset((const sockaddr_u*)s,n/addr.get_element_size());
   done=true;
   if(!cache)
      cache=new ResolverCache;
   cache->Add(hostname,portname,defport,service,proto,addr.get(),addr.count());

   xstring report;
   report.set(xstring::format(plural("%d address$|es$ found",addr.count()),addr.count()));
   if(addr.count()>0) {
      report.append(": ");
      for(int i=0; i<addr.count(); i++) {
	 report.append(addr[i].address());
	 if(i<addr.count()-1)
	    report.append(", ");
      }
   }
   LogNote(4,"%s",report.get());

   return MOVED;
}

void Resolver::MakeErrMsg(const char *f)
{
   const char *e=strerror(errno);
   err_msg.vset(f,": ",e,NULL);
   done=true;
}

void Resolver::AddAddress(int family,const char *address,int len, unsigned int scope)
{
   sockaddr_u add;
   memset(&add,0,sizeof(add));

   add.sa.sa_family=family;
   switch(family)
   {
   case AF_INET:
      if(sizeof(add.in.sin_addr) != len)
         return;
      memcpy(&add.in.sin_addr,address,len);
      add.in.sin_port=port_number;
#ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
      add.sa.sa_len=sizeof(add.in);
#endif
      break;

#if INET6
   case AF_INET6:
      if(sizeof(add.in6.sin6_addr) != len)
         return;
      memcpy(&add.in6.sin6_addr,address,len);
      if(IN6_IS_ADDR_LINKLOCAL(&add.in6.sin6_addr) && scope==0) {
	 error=_("Link-local IPv6 address should have a scope");
	 return;
      }
      add.in6.sin6_port=port_number;
# ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
      add.in6.sin6_scope_id=scope;
# endif
# ifdef HAVE_STRUCT_SOCKADDR_SA_LEN
      add.sa.sa_len=sizeof(add.in6);
# endif
      break;
#endif

   default:
      return;
   }
   if(addr.count()>0 && addr.last()==add)
      return;
   addr.append(add);
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

bool Resolver::IsAddressFamilySupporded(int af)
{
#if INET6
   // check if ipv6 is really supported
   if(af==AF_INET6 && (!FindGlobalIPv6Address() || !CanCreateIpv6Socket()))
   {
      LogNote(4, "IPv6 is not supported or configured");
      return false;
   }
#endif // INET6
   return true;
}

void Resolver::ParseOrder(const char *s,int *o)
{
   const char * const delim="\t ";
   char *s1=alloca_strdup(s);
   int idx=0;

   for(s1=strtok(s1,delim); s1; s1=strtok(0,delim))
   {
      int af=FindAddressFamily(s1);
      if(af!=-1 && idx<15 && IsAddressFamilySupporded(af))
      {
	 if(o) o[idx]=af;
	 idx++;
      }
   }
   if(o) o[idx]=-1;
}

#if defined(HAVE_DN_EXPAND) && !HAVE_DECL_DN_EXPAND
CDECL int dn_expand(const unsigned char *msg,const unsigned char *eomorig,const unsigned char *comp_dn,char *exp_dn,int length);
CDECL int dn_skipname(const unsigned char *msg,const unsigned char *eomorig);
#endif

#ifdef HAVE_RES_SEARCH
static
int extract_domain(const unsigned char *answer,const unsigned char *scan,int len,
		     char *store,int store_len)
{
#ifdef HAVE_DN_EXPAND // newer resolver versions have dn_expand and dn_skipname
   if(store)
      dn_expand(answer,scan+len,scan,store,store_len);
   return dn_skipname(scan,scan+len);
#else // ...older don't.
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
#endif // DN_EXPAND
}

#ifndef NS_MAXDNAME
# define NS_MAXDNAME 1025
#endif
#ifndef NS_HFIXEDSZ
# define NS_HFIXEDSZ 12
#endif

struct SRV
{
   char domain[NS_MAXDNAME];
   int port;
   int priority;
   int weight;
   int order;
};

static
int SRV_compare(const SRV *sa,const SRV *sb)
{
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
   if(!ResMgr::QueryBool("dns:SRV-query",hostname))
      return;
#ifdef HAVE_RES_SEARCH
   const char *tproto=proto?proto.get():"tcp";
   time_t try_time;
   unsigned char answer[0x1000];
   const char *srv_name=xstring::format("_%s._%s.%s",service.get(),tproto,hostname.get());
   srv_name=alloca_strdup(srv_name);

   int retries=0;
   int max_retries=ResMgr::Query("dns:max-retries",hostname);
   int len;
   for(;;)
   {
      if(!use_fork)
      {
	 Schedule();
	 if(Deleted())
	    return;
      }
      time(&try_time);

#ifndef DNSSEC_LOCAL_VALIDATION
      len=res_search(srv_name, C_IN, T_SRV, answer, sizeof(answer));
      if(len>=0)
	 break;
#else
      val_status_t val_status;
      bool require_trust = ResMgr::QueryBool("dns:strict-dnssec",hostname);
      len=val_res_search(NULL, srv_name, C_IN, T_SRV, answer, sizeof(answer), &val_status);
      if(len>=0) {
          if(require_trust && !val_istrusted(val_status))
              return;
          else
              break;
      }
#endif
#ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN)
	 return;
      if(++retries>=max_retries && max_retries)
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

   if(len<NS_HFIXEDSZ)
      return;

   int question_count=(answer[4]<<8)+answer[5];
   int answer_count  =(answer[6]<<8)+answer[7];

   // skip header
   unsigned char *scan=answer+NS_HFIXEDSZ;
   len-=NS_HFIXEDSZ;

   // skip questions section
   for( ; question_count>0; question_count--)
   {
      int dom_len=extract_domain(answer,scan,len,0,0);
      if(dom_len<0)
	 return;
      scan+=dom_len;
      len-=dom_len;
      if(len<4)
	 return;
      scan+=4;
      len-=4;
   }

   xarray<SRV> SRVs;

   // now process answers section
   for( ; answer_count>0; answer_count--)
   {
      int dom_len=extract_domain(answer,scan,len,0,0);
      if(dom_len<0)
	 return;
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

      struct SRV t;
      t.priority=(scan[0]<<8)+scan[1];
      t.weight  =(scan[2]<<8)+scan[3];
      t.port    =(scan[4]<<8)+scan[5];
      t.order=0;

      scan+=6;
      len-=6;

      dom_len=extract_domain(answer,scan,len,t.domain,sizeof(t.domain));
      if(dom_len<0)
	 return;

      scan+=dom_len;
      len-=dom_len;

      // add unless the service is decidedly not available at this domain.
      if(strcmp(t.domain,"."))
	 SRVs.append(t);
   }

   // now sort and randomize the list.
   SRVs.qsort(SRV_compare);

   srand(time(0));

   int SRVscan;
   int base=0;
   int curr_priority=-1;
   int weight_sum=0;
   for(SRVscan=0; ; SRVscan++)
   {
      if(SRVscan==SRVs.count() || SRVs[SRVscan].priority!=curr_priority)
      {
	 if(base)
	 {
	    int o=1;
	    int s;
	    while(weight_sum>0)
	    {
	       int r=int(rand()/(RAND_MAX+1.0)*weight_sum);
	       if(r>=weight_sum)
		  r=weight_sum-1;
	       int w=0;
	       for(s=base; s<SRVscan; s++)
	       {
		  if(SRVs[s].order!=0)
		     continue;
		  w+=SRVs[s].weight;
		  if(r<w)
		  {
		     SRVs[s].order=o;
		     o++;
		     weight_sum-=SRVs[s].weight;
		     break;
		  }
	       }
	    }
	 }
	 if(SRVscan==SRVs.count())
	    break;
	 base=SRVscan;
	 curr_priority=SRVs[SRVscan].priority;
	 weight_sum=0;
      }
      weight_sum+=SRVs[SRVscan].weight;
   }

   SRVs.qsort(SRV_compare);

   int oldport=port_number;
   for(SRVscan=0; SRVscan<SRVs.count(); SRVscan++)
   {
      port_number=htons(SRVs[SRVscan].port);
      LookupOne(SRVs[SRVscan].domain);
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

#if LIBIDN2
   xstring_c ascii_name;
   int rc=idn2_lookup_ul(name,ascii_name.buf_ptr(),0);
   if(rc!=IDN2_OK) {
      error=idn2_strerror(rc);
      return;
   }
   name=ascii_name;
#endif//LIBIDN2

   ParseOrder(order,af_order);

   int retries=0;
   int max_retries=ResMgr::Query("dns:max-retries",name);
   for(;;)
   {
      if(!use_fork)
      {
	 Schedule();
	 if(Deleted())
	    return;
      }

      time(&try_time);

// Prefer getaddrinfo over gethostbyname2 and getipnodebyname, as
// approach with multiple lookups works badly when host name is in hosts file
// and no dns servers are reachable.
#if defined(HAVE_GETADDRINFO) && INET6
/* && !defined(HAVE_GETHOSTBYNAME2) \
   && !defined(HAVE_GETIPNODEBYNAME) */

      // getaddrinfo support by Brandon Hume
      struct addrinfo	    *ainfo=0,
			    *a_res,
			    a_hint;
      int		    ainfo_res;
      struct sockaddr	    *sockname;
      struct sockaddr_in    *inet_addr;
      struct sockaddr_in6   *inet6_addr;
      const char	    *addr_data;
      int		    addr_len;
      unsigned int          addr_scope;

      memset(&a_hint, 0, sizeof(a_hint));
      a_hint.ai_flags	    = AI_PASSIVE;
      a_hint.ai_family	    = PF_UNSPEC;

#ifndef DNSSEC_LOCAL_VALIDATION
      ainfo_res	= getaddrinfo(name, NULL, &a_hint, &ainfo);
#else
      val_status_t val_status;
      bool require_trust=ResMgr::QueryBool("dns:strict-dnssec",name);
      ainfo_res	= val_getaddrinfo(NULL, name, NULL, &a_hint, &ainfo,
                                  &val_status);
      if(VAL_GETADDRINFO_HAS_STATUS(ainfo_res) && !val_istrusted(val_status))
      {
          if(require_trust) {
              // untrusted answer
              error = _("DNS resolution not trusted.");
              break;
          } else {
              fprintf(stderr,"\nWARNING: DNS lookup failed validation: %s\n",
                      p_val_status(val_status));
	      fflush(stderr);
          }
      }
#endif

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
	       addr_scope = 0;

	       switch(a_res->ai_family)
	       {
	       case AF_INET:
		  inet_addr   = (sockaddr_in *)sockname;
		  addr_data   = (const char *)&(inet_addr->sin_addr.s_addr);
		  addr_len    = sizeof(inet_addr->sin_addr.s_addr);
		  break;
	       case AF_INET6:
		  inet6_addr  = (sockaddr_in6 *)sockname;
		  addr_data   = (const char *)&(inet6_addr->sin6_addr.s6_addr);
#ifdef HAVE_STRUCT_SOCKADDR_IN6_SIN6_SCOPE_ID
		  addr_scope  = inet6_addr->sin6_scope_id;
#endif
		  addr_len    = sizeof(inet6_addr->sin6_addr.s6_addr);
		  break;
	       default:
		  continue;
	       }
	       AddAddress(a_res->ai_family, addr_data, addr_len, addr_scope);
	    }
	 }

	 freeaddrinfo(ainfo);
	 break;
      }

      if(ainfo_res != EAI_AGAIN
      || (++retries>=max_retries && max_retries))
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
#  undef h_errno // it could be a macro, but we want it to be local variable.
      int h_errno=0;
      ha=getipnodebyname(name,af,0,&h_errno);
# elif defined(HAVE_GETHOSTBYNAME2)
      ha=gethostbyname2(name,af);
# else
      if(af==AF_INET)
	 ha=gethostbyname(name);
      else
      {
	 retries=0;
	 af_index++;
	 continue;
      }
# endif

      if(ha)
      {
	 const char * const *a;
	 for(a=ha->h_addr_list; *a; a++)
	    AddAddress(ha->h_addrtype, *a, ha->h_length, 0);
	 retries=0;
	 af_index++;
# if defined(HAVE_GETIPNODEBYNAME)
	 freehostent(ha);
# endif
	 continue;
      }

# ifdef HAVE_H_ERRNO
      if(h_errno!=TRY_AGAIN
      || (++retries>=max_retries && max_retries))
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
	 retries=0;
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
      const char *tproto=proto?proto.get():"tcp";
      const char *tport=portname?portname.get():defport.get();

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
	    buf->Format(_("no such %s service"),tproto);
	    return;
	 }
      }
   }

   if(service && !portname && !isdigit((unsigned char)hostname[0]))
      LookupSRV_RR();

   if(!use_fork && Deleted())
      return;

   const char *h=ResMgr::Query("dns:name",hostname);
   if(!h || !*h)
      h=hostname;
   char *hs=alloca_strdup(h);
   char *tok;
   for(hs=strtok_r(hs,",",&tok); hs; hs=strtok_r(NULL,",",&tok))
      LookupOne(hs);

   if(!use_fork && Deleted())
      return;

   if(addr.count()==0)
   {
      buf->Put("E");
      if(error==0)
	 error=_("No address found");
      buf->Put(error);
      return;
   }
   buf->Put("O");
   buf->Put((const char*)addr.get(),addr.count()*addr.get_element_size());
   addr.unset();
}

void Resolver::Reconfig(const char *name)
{
   if(!name || strncmp(name,"dns:",4))
      return;
}


ResolverCache::ResolverCache()
   : Cache(ResMgr::FindRes("dns:cache-size"),ResMgr::FindRes("dns:cache-enable"))
{
}
void ResolverCache::Reconfig(const char *r)
{
   if(!xstrcmp(r,"dns:SRV-query")
   || !xstrcmp(r,"dns:order"))
      Flush();
}
ResolverCacheEntry *ResolverCache::Find(const char *h,const char *p,const char *defp,const char *ser,const char *pr)
{
   for(ResolverCacheEntry *c=IterateFirst(); c; c=IterateNext())
   {
      if(c->Matches(h,p,defp,ser,pr))
	 return c;
   }
   return 0;
}
void ResolverCache::Add(const char *h,const char *p,const char *defp,
	 const char *ser,const char *pr,const sockaddr_u *a,int n)
{
   Trim();
   ResolverCacheEntry *c=Find(h,p,defp,ser,pr);
   if(c)
      c->SetData(a,n);
   else
   {
      if(!IsEnabled(h))
	 return;
      AddCacheEntry(new ResolverCacheEntry(h,p,defp,ser,pr,a,n));
   }
}
bool ResolverCacheEntryLoc::Matches(const char *h,const char *p,
	 const char *defp,const char *ser,const char *pr)
{
   return (!xstrcasecmp(hostname,h)
      && !xstrcmp(portname,p)
      && !xstrcmp(defport,defp)
      && !xstrcmp(service,ser)
      && !xstrcmp(proto,pr));
}
void ResolverCache::Find(const char *h,const char *p,const char *defp,
	 const char *ser,const char *pr,const sockaddr_u **a,int *n)
{
   *a=0;
   *n=0;

   // if cache is disabled for this host, return nothing.
   if(!IsEnabled(h))
      return;

   ResolverCacheEntry *c=Find(h,p,defp,ser,pr);
   if(c)
   {
      if(c->Stopped())
      {
	 Trim();
	 return;
      }
      c->GetData(a,n);
   }
}
