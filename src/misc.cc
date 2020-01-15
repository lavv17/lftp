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
#include "xmalloc.h"
#include "xstring.h"
#include "trio.h"
#include <pwd.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <ctype.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef HAVE_TERMIOS_H
#include <termios.h>
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
#endif

#if LIBIDN2
# include <idn2.h>
#endif

#ifdef HAVE_DLFCN_H
# include <dlfcn.h>
#endif

CDECL_BEGIN
#include "regex.h"
#include "human.h"
CDECL_END
#include "misc.h"
#include "ProcWait.h"
#include "SignalHook.h"
#include "url.h"
#include "ResMgr.h"
#include "log.h"
#include <mbswidth.h>

const char *dir_file(const char *dir,const char *file)
{
   if(dir==0 || dir[0]==0)
      return file?file:dir;
   if(file==0 || file[0]==0)
      return dir;
   if(file[0]=='/')
      return file;
   if(file[0]=='.' && file[1]=='/')
      file+=2;

   xstring& buf=xstring::get_tmp();
   size_t len=strlen(dir);
   if(len==0)
      return buf.set(file);
   if(dir[len-1]=='/')
      return buf.vset(dir,file,NULL);
   return buf.vset(dir,"/",file,NULL);
}

const char *url_file(const char *url,const char *file)
{
   static xstring buf;

   if(buf && url==buf) // it is possible to url_file(url_file(url,dir),file)
      url=alloca_strdup(url);

   if(!url || url[0]==0)
   {
      buf.set(file?file:"");
      return buf;
   }
   ParsedURL u(url);
   if(!u.proto)
   {
      buf.set(dir_file(url,file));
      return buf;
   }
   if(file && file[0]=='~')
      u.path.set(file);
   else
      u.path.set(dir_file(u.path,file));
   buf.truncate();
   return u.CombineTo(buf);
}

const char *output_file_name(const char *src,const char *dst,bool dst_local,
			     const char *dst_base,bool make_dirs)
{
   bool dst_is_dir=false;
   if(dst)
   {
      if(dst_base)
	 dst=url_file(dst_base,dst);
      ParsedURL u_dst(dst,true);
      if(u_dst.proto)
	 dst_local=false;
      if(dst_local)
      {
	 dst=expand_home_relative(dst);
	 struct stat st;
	 if(stat(dst,&st)!=-1 && S_ISDIR(st.st_mode))
	    dst_is_dir=true;
      }
      else
      {
	 int len=xstrlen(u_dst.path);
	 if(len>0 && u_dst.path[len-1]=='/')
	    dst_is_dir=true;
      }
      if(!dst_is_dir)
	 return dst;
   }

   ParsedURL u_src(src,true);
   if(u_src.proto)
      src=u_src.path;
   if(!src)
      return "";  // there will be error anyway.
   const char *base=basename_ptr(src);
   if(make_dirs && !dst)
   {
      base=src;
      if(base[0]=='~')
      {
	 base=strchr(base,'/');
	 if(!base)
	    base="";
      }
      while(*base=='/')
	 base++;
   }
   return url_file(dst?dst:dst_base,base);
}

const char *basename_ptr(const char *s)
{
   const char *s1=s+strlen(s);
   while(s1>s && s1[-1]=='/')
      s1--;
   while(s1>s && s1[-1]!='/')
      s1--;
   return s1;
}

const char *expand_home_relative(const char *s)
{
   if(s[0]!='~')
      return s;

   const char *home=0;
   const char *sl=strchr(s+1,'/');
   static xstring ret_path;

   if(s[1]==0 || s[1]=='/')
   {
      home=get_home();
   }
   else
   {
      // extract user name and find the home
      int name_len=(sl?sl-s-1:strlen(s+1));
      struct passwd *pw=getpwnam(xstring::get_tmp(s+1,name_len));
      if(pw)
	 home=pw->pw_dir;
   }
   if(home==0)
      return s;

   if(sl)
      return ret_path.vset(home,sl,NULL);
   return home;
}

int   create_directories(char *path)
{
   char  *sl=path;
   int	 res;

   if(access(path,0)==0)
      return 0;

   for(;;)
   {
      sl=strchr(sl,'/');
      if(sl==path)
      {
	 sl++;
	 continue;
      }
      if(sl)
	 *sl=0;
      if(access(path,0)==-1)
      {
	 res=mkdir(path,0777);
	 if(res==-1)
	 {
	    if(errno!=EEXIST)
	    {
	       fprintf(stderr,"mkdir(%s): %s\n",path,strerror(errno));
	       if(sl)
		  *sl='/';
	       return(-1);
	    }
	 }
	 else
	    debug((9,"mkdir(%s): ok\n",path));
      }
      if(sl)
	 *sl++='/';
      else
	 break;
   }
   return 0;
}

void  truncate_file_tree(const char *dir)
{
   fflush(stderr);
   pid_t pid;
   switch(pid=fork())
   {
   case(0): // child
      SignalHook::Ignore(SIGINT);
      SignalHook::Ignore(SIGTSTP);
      SignalHook::Ignore(SIGQUIT);
      SignalHook::Ignore(SIGHUP);
      execlp("rm","rm","-rf",dir,(char*)NULL);
      perror("execlp(rm)");
      fflush(stderr);
      _exit(1);
   case(-1):   // error
      perror("fork()");
      return;
   default: // parent
      (new ProcWait(pid))->Auto();  // don't wait for termination
   }
}

int fd_width(int fd)
{
   if(fd == -1) return -1;
   if(!isatty(fd)) return 0;

#ifdef TIOCGWINSZ
   struct winsize sz;
   sz.ws_col=sz.ws_row=0;
   ioctl(fd,TIOCGWINSZ,&sz);
   if(sz.ws_col==0)
      sz.ws_col=80;
   return(sz.ws_col);
#else /* !TIOCGWINSZ */
   return 80;
#endif
}

char *xgetcwd()
{
   char *cwd=getcwd(0,0); // glibc extension
   if(cwd) {
      xmalloc_register_block(cwd);
      return cwd;
   }

   int size=256;
   cwd=(char*)xmalloc(size);
   for(;;)
   {
      if(getcwd(cwd,size))
	 return cwd;
      if(errno!=ERANGE)
	 return strcpy(cwd,".");
      cwd=(char*)xrealloc(cwd,size*=2);
   }
}

void xgetcwd_to(xstring& s)
{
   int size=256;
   for(;;) {
      s.get_space(size);
      if(getcwd(s.get_non_const(),size)) {
	 s.set_length(strlen(s.get()));
	 return;
      }
      if(errno!=ERANGE) {
	 s.set(".");
	 return;
      }
      size*=2;
   }
}

int parse_perms(const char *s)
{
   int p=0;

   if(strlen(s)!=9
   && !(strlen(s)==10 && s[9]=='+'))   // ACL tag
      bad: return -1;

   switch(s[0])
   {
   case('r'): p|=S_IRUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWUSR; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISUID; break;
   case('s'): p|=S_ISUID; // fall-through
   case('x'): p|=S_IXUSR; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IRGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWGRP; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('S'): p|=S_ISGID; break;
   case('s'): p|=S_ISGID; // fall-through
   case('x'): p|=S_IXGRP; break;
   case('-'): break;
   default: goto bad;
   }
   s+=3;
   switch(s[0])
   {
   case('r'): p|=S_IROTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[1])
   {
   case('w'): p|=S_IWOTH; break;
   case('-'): break;
   default: goto bad;
   }
   switch(s[2])
   {
   case('T'): p|=S_ISVTX; break;
   case('t'): p|=S_ISVTX; // fall-through
   case('x'): p|=S_IXOTH; break;
   case('l'): case('L'): p|=S_ISGID; p&=~S_IXGRP; break;
   case('-'): break;
   default: goto bad;
   }

   return p;
}

// it does not prepend file type.
const char *format_perms(int p)
{
   static char s[10];
   memset(s,'-',9);
   if(p&0400) s[0]='r';
   if(p&0200) s[1]='w';
   if(p&0100) s[2]='x';
   if(p&0040) s[3]='r';
   if(p&0020) s[4]='w';
   if(p&0010) s[5]='x';
   if(p&0004) s[6]='r';
   if(p&0002) s[7]='w';
   if(p&0001) s[8]='x';
   if(p&01000) s[8]=(p&0001?'t':'T');
   if(p&02000) s[5]=(p&0010?'s':'S');
   if(p&04000) s[2]=(p&0100?'s':'S');
   return s;
}

const char month_names[][4]={
   "Jan","Feb","Mar","Apr","May","Jun",
   "Jul","Aug","Sep","Oct","Nov","Dec",
   ""
};
int parse_month(const char *m)
{
   for(int i=0; month_names[i][0]; i++)
      if(!strcasecmp(month_names[i],m))
	 return(i%12);
   return -1;
}

int parse_year_or_time(const char *year_or_time,int *year,int *hour,int *minute)
{
   if(year_or_time[2]==':')
   {
      if(2!=sscanf(year_or_time,"%2d:%2d",hour,minute))
	 return -1;
      *year=-1;
   }
   else
   {
      if(1!=sscanf(year_or_time,"%d",year))
	 return -1;;
      *hour=*minute=0;
   }
   return 0;
}
int guess_year(int month,int day,int hour,int minute)
{
   const struct tm &now=SMTask::now;
   int year=now.tm_year+1900;
   if(month     *32+        day
    > now.tm_mon*32+now.tm_mday+6)
      year--;
   return year;
}
int percent(off_t offset,off_t size)
{
   if(offset>=size)
      return 100;
   // use floating point to avoid integer overflow.
   return int(double(offset)*100/size);
}

const char *squeeze_file_name(const char *name,int w)
{
   static xstring buf;
   int mbflags=0;

   name=url::remove_password(name);

   int name_width=mbswidth(name,mbflags);
   if(name_width<=w)
      return name;

   const char *b=basename_ptr(name);
   int b_width=name_width-mbsnwidth(name,b-name,mbflags);
   if(b_width<=w-4 && b_width>w-15)
      return buf.vset(".../",b,NULL);
   int b_len=strlen(b);
   while(b_width>(w<3?w-1:w-3) && b_len>0)
   {
      int ch_len=mblen(b,b_len);
      if(ch_len<1)
	 ch_len=1;
      b_width-=mbsnwidth(b,ch_len,mbflags);
      b+=ch_len;
      b_len-=ch_len;
   }
   if(w>=6)
      buf.set("...");
   else
      buf.set("<");
   return buf.append(b);
}

/* Converts struct tm to time_t, assuming the data in tm is UTC rather
   than local timezone (mktime assumes the latter).

   Contributed by Roger Beeman <beeman@cisco.com>, with the help of
   Mark Baushke <mdb@cisco.com> and the rest of the Gurus at CISCO.  */
time_t
mktime_from_utc (const struct tm *t)
{
   struct tm tc;
   memcpy(&tc, t, sizeof(struct tm));

   /* UTC times are never DST; if we say -1, we'll introduce odd localtime-
    * dependant errors. */

   tc.tm_isdst = 0;

   time_t tl = mktime (&tc);
   if (tl == -1)
      return -1;
   time_t tb = mktime (gmtime (&tl));

   return (tl <= tb ? (tl + (tl - tb)) : (tl - (tb - tl)));
}

static void set_tz(const char *tz)
{
   static char *put_tz;
   static int put_tz_alloc;

   if(!tz)
   {
      unsetenv("TZ");

      xfree(put_tz);
      put_tz=0;
      put_tz_alloc=0;

      tzset();
      return;
   }

   int tz_len=strlen(tz)+4;
   char *new_tz=put_tz;
   if(tz_len>put_tz_alloc)
      new_tz=(char*)xmalloc(put_tz_alloc=tz_len);
   snprintf(new_tz,tz_len,"TZ=%s",tz);
   if(new_tz!=put_tz)
   {
      putenv(new_tz);
      xfree(put_tz);
      put_tz=new_tz;
   }
   // now initialize libc variables from env TZ.
   tzset();
}
static char *saved_tz=0;
static void save_tz()
{
   xstrset(saved_tz,getenv("TZ"));
}
static void restore_tz()
{
   set_tz(saved_tz);
}
time_t mktime_from_tz(struct tm *t,const char *tz)
{
   if(!tz || !*tz)
      return mktime(t);
   if(!strcasecmp(tz,"GMT"))
      return mktime_from_utc(t);
   if(isdigit((unsigned char)*tz) || *tz=='+' || *tz=='-')
   {
      int tz1_len=strlen(tz)+4;
      char *tz1=string_alloca(tz1_len);
      snprintf(tz1,tz1_len,"GMT%s",tz);
      tz=tz1;
   }
   save_tz();
   set_tz(tz);
   time_t res=mktime(t);
   restore_tz();
   return res;
}

bool re_match(const char *line,const char *a,int flags)
{
   if(!a || !*a)
      return false;
   regex_t re;
   if(regcomp(&re,a,REG_EXTENDED|REG_NOSUB|flags))
      return false;
   bool res=(0==regexec(&re,line,0,0,0));
   regfree(&re);
   return res;
}

xstring& SubstTo(xstring& buf,const char *txt, const subst_t *s)
{
   buf.nset("",0);

   char str[3];
   bool last_subst_empty=true;

   while(*txt)
   {
      char ch = *txt++;
      const char *to_add = NULL;
      if(ch=='\\' && *txt && *txt!='\\')
      {
	 ch=*txt++;
	 if(ch >= '0' && ch < '8') {
	    int len;
	    unsigned code;
	    txt--;
	    if(sscanf(txt,"%3o%n",&code,&len)!=1)
	       continue; // should never happen.
	    ch=code;
	    txt+=len;
	    str[0]=ch;
	    str[1]=0;
	    to_add=str;
	 } else {
	    if(ch=='?')
	    {
	       if(last_subst_empty)
		  txt++;
	       to_add="";
	    }
	    for(int i = 0; s[i].from; i++) {
	       if(s[i].from != ch)
		  continue;
	       to_add=s[i].to;
	       if(!to_add)
		  to_add = "";
	       last_subst_empty = (*to_add==0);
	    }
	    if(!to_add) {
	       str[0]='\\';
	       str[1]=ch;
	       str[2]=0;
	       to_add=str;
	    }
	 }
      }
      else
      {
	 if(ch=='\\' && *txt=='\\')
	    txt++;
	 str[0]=ch;
	 str[1]=0;
	 to_add=str;
      }

      if(to_add==0)
	 continue;

      buf.append(to_add);
   }
   return(buf);
}

void xgettimeofday(time_t *sec, int *usec)
{
#ifdef HAVE_GETTIMEOFDAY
   struct timeval tv;
   gettimeofday(&tv,0);
   if(sec) *sec = tv.tv_sec;
   if(usec) *usec = tv.tv_usec;
#else
   if(sec) time(sec);
   if(usec) *usec = 0;
#endif
}

char *xstrftime(const char *format, const struct tm *tm)
{
   char *ret = NULL;
   int siz = 32;

   struct tm dummy;
   memset(&dummy, 0, sizeof(dummy));
   if(tm == NULL)
      tm = &dummy;

   for(;;)
   {
      ret = (char *) xrealloc(ret, siz);
      int res=strftime(ret, siz, format, tm);
      if(res>0 && res<siz)
	 return ret; /* success */
      /* more space */
      siz*=2;
   }
}

/* /file/name -> /file
 * /file -> /
 * file/name -> "file"
 * file/name/ -> "file"
 * file -> ""
 * note: the last differs from dirname(1) (which would return ".")
 *
 */
void strip_trailing_slashes(xstring& ret)
{
   int len=ret.length();
   while(len>0 && ret[len-1]=='/')
      len--;
   if(len==0 && ret[0]=='/')
      len=1+(ret[1]=='/');
   if(len>0)
      ret.truncate(len);
}
xstring& dirname_modify(xstring &ret)
{
   strip_trailing_slashes(ret);
   const char *slash=strrchr(ret,'/');
   if(!slash)
      ret.truncate(0); /* file with no path */
   else if(slash==ret)
      ret.truncate(1); /* the slash is the first character */
   else
      ret.truncate(slash-ret);
   return ret;
}
xstring& dirname(const char *path)
{
   return dirname_modify(xstring::get_tmp(path));
}

char last_char(const char *str)
{
   int len=strlen(str);
   return str[len-(len>0)];
}

/* How many bytes it will take to store LEN bytes in base64.  */
int
base64_length(int len)
{
  return (4 * (((len) + 2) / 3));
}

/* Encode the string S of length LENGTH to base64 format and place it
   to STORE.  STORE will be 0-terminated, and must point to a writable
   buffer of at least 1+BASE64_LENGTH(length) bytes.  */
void
base64_encode (const char *s, char *store, int length)
{
  /* Conversion table.  */
  static const char tbl[64] = {
    'A','B','C','D','E','F','G','H',
    'I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X',
    'Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n',
    'o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3',
    '4','5','6','7','8','9','+','/'
  };
  int i;
  unsigned char *p = (unsigned char *)store;

  /* Transform the 3x8 bits to 4x6 bits, as required by base64.  */
  for (i = 0; i < length; i += 3)
    {
      *p++ = tbl[s[0] >> 2];
      *p++ = tbl[((s[0] & 3) << 4) + (s[1] >> 4)];
      *p++ = tbl[((s[1] & 0xf) << 2) + (s[2] >> 6)];
      *p++ = tbl[s[2] & 0x3f];
      s += 3;
    }
  /* Pad the result if necessary...  */
  if (i == length + 1)
    *(p - 1) = '=';
  else if (i == length + 2)
    *(p - 1) = *(p - 2) = '=';
  /* ...and zero-terminate it.  */
  *p = '\0';
}

bool temporary_network_error(int err)
{
   switch(err)
   {
   case(EPIPE):
   case(EIO):
   case(ETIMEDOUT):
#ifdef ECONNRESET
   case(ECONNRESET):
#endif
   case(ECONNREFUSED):
#ifdef EHOSTUNREACH
   case(EHOSTUNREACH):
#endif
#ifdef EHOSTDOWN
   case(EHOSTDOWN):
#endif
#ifdef ENETRESET
   case(ENETRESET):
#endif
#ifdef ENETUNREACH
   case(ENETUNREACH):
#endif
#ifdef ENETDOWN
   case(ENETDOWN):
#endif
#ifdef ECONNABORTED
   case(ECONNABORTED):
#endif
#ifdef EADDRNOTAVAIL
   case(EADDRNOTAVAIL):
#endif
      return true;
   }
   return false;
}

const char *get_home()
{
   static char *home=NULL;
   if(home)
      return home;
   home=getenv("HOME");
   if(home)
      return home;
   struct passwd *pw=getpwuid(getuid());
   if(pw && pw->pw_dir)
      return home=pw->pw_dir;
   return NULL;
}

const char *get_lftp_home_nocreate()
{
   static char *lftp_home=NULL;

   if(lftp_home)
      return *lftp_home?lftp_home:NULL;

   lftp_home=getenv("LFTP_HOME");
   if(!lftp_home)
   {
      const char *h=get_home();
      if(h)
         lftp_home=xstring::cat(h,"/.lftp",NULL).borrow();
      else
         return NULL;
   }
   else
      lftp_home=xstrdup(lftp_home);

   return *lftp_home?lftp_home:NULL;
}
const char *get_lftp_home_if_exists()
{
   const char *home=get_lftp_home_nocreate();
   struct stat st;
   if(stat(home,&st)==-1 || !S_ISDIR(st.st_mode))
      return NULL;
   return home;
}

// new XDG directories
const char *get_lftp_dir(char *&cached_dir,const char *env,const char *def)
{
   if(cached_dir)
      return cached_dir;

   // use old existing directory for compatibility
   const char *dir=get_lftp_home_if_exists();
   if(dir)
      return cached_dir=xstrdup(dir);

   // use explicit directory if specified, otherwise use default under home
   const char *home=getenv(env);
   if(home) {
      // explicit XDG dir
      (void)mkdir(home,0755);
      dir=xstring::cat(home,"/lftp",NULL);
   } else {
      home=get_home();
      if(!home)
	 return NULL;
      xstring& path=xstring::get_tmp(home);
      path.append('/');
      const char *slash=strchr(def,'/');
      if(slash) {
	 path.append(def,slash-def);
	 (void)mkdir(path,0755);
	 path.append(slash);
      } else {
	 path.append(def);
      }
      (void)mkdir(path,0755);
      dir=path.append("/lftp");
   }
   (void)mkdir(dir,0755);
   return cached_dir=xstrdup(dir);
}
const char *get_lftp_config_dir()
{
   static char *config_dir;
   return get_lftp_dir(config_dir,"XDG_CONFIG_HOME",".config");
}
const char *get_lftp_data_dir()
{
   static char *data_dir;
   return get_lftp_dir(data_dir,"XDG_DATA_HOME",".local/share");
}
const char *get_lftp_cache_dir()
{
   static char *cache_dir;
   return get_lftp_dir(cache_dir,"XDG_CACHE_HOME",".cache");
}

const char *memrchr(const char *buf,char c,size_t len)
{
   buf+=len;
   while(len-->0)
      if(*--buf==c)
	 return buf;
   return 0;
}

bool is_shell_special(char c)
{
   switch (c)
   {
   case '\'':
   case '(': case ')':
   case '!': case '{': case '}':		/* reserved words */
   case '^':
   case '$': case '`':			/* expansion chars */
   case '*': case '[': case '?': case ']':	/* globbing chars */
   case ' ': case '\t': case '\n':		/* IFS white space */
   case '"': case '\\':		/* quoting chars */
   case '|': case '&': case ';':		/* shell metacharacters */
   case '<': case '>':
   case '#':				/* comment char */
      return true;
   }
   return false;
}

const xstring& shell_encode(const char *string,int len)
{
   if(!string)
      return xstring::null;

   static xstring result;

   result.get_space(2 + 2 * len);
   char *r = result.get_non_const();

   if(string[0]=='-' || string[0]=='~')
   {
      *r++='.';
      *r++='/';
   }

   int c;
   for (const char *s = string; s && (c = *s); s++)
   {
      if (is_shell_special(c))
	 *r++ = '\\';
      *r++ = c;
   }
   result.set_length(r-result);
   return (result);
}

int remove_tags(char *buf)
{
   int len=strlen(buf);
   int less = -1;
   for(int i = 0; i < len;i++)
   {
      if(strcmp(buf + i, "&nbsp;") == 0){
        for(int j = 0; j < 6; j++)buf[i + j] = 0;
        buf[i] = ' ';
        i += 6;
        i--;
        continue;
      }
      if(buf[i] == '<'){
        less = i;
        continue;

      }
      if(buf[i] == '>'){
        if(less != -1){
          for(int j = less; j <= i; j++)buf[j] = 0;
          less = -1;
        }

      }
   }
   int zero = 0;
   for(int i = 0; i < len;i++)
   {
     while(zero < i && buf[zero] != 0)zero++;
     if(buf[i] != 0 and zero != i){
      buf[zero] = buf[i];
      buf[i] = 0;
     }
   }
   return ++zero;
}

void rtrim(char *s)
{
   int len=strlen(s);
   while(len>0 && (s[len-1]==' ' || s[len-1]=='\t' || s[len-1]=='\r'))
      s[--len]=0;
}

bool in_foreground_pgrp()
{
   static int tty_fd;
   if(tty_fd==-1)
      return true;
   pid_t pg=tcgetpgrp(tty_fd);
   if(pg==(pid_t)-1 && !isatty(tty_fd)) {
      tty_fd=open("/dev/tty",O_RDONLY);
      if(tty_fd==-1)
	 return true;
      pg=tcgetpgrp(tty_fd);
   }
   if(pg==(pid_t)-1 || pg==getpgrp())
      return true;
   return false;
}

void random_init()
{
   static bool init;
   if(!init)
   {
      srandom(time(NULL)+getpid());
      init=true;
   }
}
double random01()
{
   return random()/2147483648.0;
}

#include <sys/utsname.h>
const char *get_nodename()
{
   static struct utsname u;
   if(uname(&u)==0)
      return u.nodename;
   return "NODE";
}

const char *xhuman(long long n)
{
   char *buf=xstring::tmp_buf(LONGEST_HUMAN_READABLE + 1);
   return human_readable(n, buf, human_autoscale|human_SI, 1, 1);
}

const char *xidna_to_ascii(const char *name)
{
#if LIBIDN2
   if(!name)
      return 0;
   static xstring_c name_ace_tmp;
   name_ace_tmp.unset();
   if(idn2_to_ascii_lz(name,name_ace_tmp.buf_ptr(),0)==IDN2_OK) {
      xmalloc_register_block((void*)name_ace_tmp.get());
      return name_ace_tmp;
   }
#endif//LIBIDN2
   return name;
}
bool xtld_name_ok(const char *name)
{
#if LIBIDN2
   if(mbswidth(name,MBSW_REJECT_INVALID|MBSW_REJECT_UNPRINTABLE)<=0)
      return false;
   if(idn2_lookup_ul(name,NULL,0)==IDN2_OK)
      return true;
#endif//LIBIDN2
   return false;
}

bool is_ipv4_address(const char *s)
{
   struct in_addr addr;
   return inet_pton(AF_INET,s,&addr)>0;
}
bool is_ipv6_address(const char *s)
{
#if INET6
   struct in6_addr addr;
   return inet_pton(AF_INET6,s,&addr)>0;
#else
   return false;
#endif
}

int lftp_fallocate(int fd,off_t sz)
{
#if defined(HAVE_FALLOCATE)
   return fallocate(fd,0,0,sz);
#elif defined(HAVE_POSIX_FALLOCATE)
   return posix_fallocate(fd,0,sz);
#else
   errno=ENOSYS;
   return -1;
#endif
}

void call_dynamic_hook(const char *name) {
#if defined(HAVE_DLOPEN) && defined(RTLD_DEFAULT)
   typedef void (*func)();
   func f=(func)dlsym(RTLD_DEFAULT,name);
   if(f) f();
#endif
}
