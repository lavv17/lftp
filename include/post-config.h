#ifndef POST_CONFIG_H
#define POST_CONFIG_H

#if defined(__cplusplus) && defined(inline)
# undef inline
#endif

#if !defined(HAVE_LSTAT)
# define lstat(file,stp)     stat((file),(stp))
#endif

#if !defined(HAVE_SETPGID) && !defined(SETPGRP_VOID)
# define setpgid(pid, pgrp)  setpgrp((pid),(pgrp))
#endif

#ifdef __cplusplus
# define CDECL extern "C"
# define CDECL_BEGIN CDECL {
# define CDECL_END   }
#else
# define CDECL
# define CDECL_BEGIN
# define CDECL_END
#endif

/* should be included before any real code. */
#include "xalloca.h"

#ifndef HAVE_STRERROR
/* there is substitute in lib/ */
CDECL const char *strerror(int);
#endif

#include <gettext.h>
#define _(Text) gettext (Text)
#define N_(Text) Text

#ifndef PARAMS
# if defined __STDC__ || defined __cplusplus
#  define PARAMS(x) x
# else
#  define PARAMS(x) ()
# endif
#endif

#define INET6 (defined(AF_INET6) \
	       && defined(HAVE_GETNAMEINFO) \
	       && defined(HAVE_GETADDRINFO))

#define USE_EXPAT (defined(HAVE_EXPAT_H) && defined(HAVE_LIBEXPAT))

#if defined(SOCKS4) || defined(SOCKS_DANTE)
# define connect     Rconnect
# define getsockname Rgetsockname
# define bind	     Rbind
# define accept	     Raccept
# define listen	     Rlisten
# define select	     Rselect
# ifdef HAVE_RPOLL
#  define poll	     Rpoll
# endif
CDECL void SOCKSinit(const char *);
#endif
#ifdef SOCKS_DANTE
# define rresvport Rrresvport
# define bindresvport Rbindresvport
# define gethostbyname Rgethostbyname
# define gethostbyname2 Rgethostbyname2
# define sendto Rsendto
# define recvfrom Rrecvfrom
# define recvfrom Rrecvfrom
# define write Rwrite
# define writev Rwritev
# define send Rsend
# define sendmsg Rsendmsg
# define read Rread
# define readv Rreadv
# define recv Rrecv
# define recvmsg Rrecvmsg
#endif

#ifdef SOCKS5
# define SOCKS
# include <socks.h>
# undef getc /* fix a problem with redefined getc */
#endif

#include <sys/types.h>
#include <sys/socket.h>
#ifdef lftp_socklen_t
# undef  socklen_t
# define socklen_t lftp_socklen_t
#endif

#if defined __MSDOS__ || defined __CYGWIN32__
# define NATIVE_CRLF 1
#endif

#define E_RETRY(e) ((e)==EAGAIN || (e)==EWOULDBLOCK || (e)==EINTR)
#define E_LOCK_IGNORE(e) ((e)==EINVAL || (e)==ENOLCK)

#ifndef HAVE_RANDOM
#define srandom(x) srand((x))
#define random() ((long)(rand()/(RAND_MAX+1.0)*2147483648.0))
#endif

#ifndef HAVE_INET_ATON
# define inet_aton(host,addr) (((addr)->s_addr=inet_addr(host))!=-1)
#endif

#if !HAVE_DECL_STRTOK_R /* prototype for lib/strtok_r.c */
CDECL char *strtok_r(char *str, const char *sep, char **lasts);
#endif

/* Tell the compiler when a conditional or integer expression is
   almost always true or almost always false.  */
#ifndef HAVE_BUILTIN_EXPECT
# define __builtin_expect(expr, val) (expr)
#endif

#ifdef __GNUC__
# define PRINTF_LIKE(n,m) __attribute__((format(__printf__,n,m)))
#else
# define PRINTF_LIKE(n,m)
#endif

#ifdef TRIO_REPLACE_STDIO
# undef HAVE_PRINTF
# undef HAVE_VPRINTF
# undef HAVE_FPRINTF
# undef HAVE_VFPRINTF
# undef HAVE_SPRINTF
# undef HAVE_VSPRINTF
# undef HAVE_SNPRINTF
# undef HAVE_VSNPRINTF
# undef HAVE_SCANF
# undef HAVE_VSCANF
# undef HAVE_FSCANF
# undef HAVE_VFSCANF
# undef HAVE_SSCANF
# undef HAVE_VSSCANF
# include "trio.h"
#endif

#define USE_SSL (defined(USE_GNUTLS) || defined(USE_OPENSSL))

#endif /* POST_CONFIG_H */
