dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.63])
AC_INIT([lftp],m4_esyscmd([build-aux/git-version-gen .tarball-version]),[lftp-bugs@lftp.yar.ru])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_LIBOBJ_DIR([lib])
AC_CONFIG_SRCDIR([src/ftpclass.cc])
AM_CONFIG_HEADER([lib/config.h])
AM_INIT_AUTOMAKE

dnl This doesn't *require* GNU extensions; it merely enables them if
dnl they're there.
AC_USE_SYSTEM_EXTENSIONS

test -z "$CXX"	     && DEFAULT_CXX=yes
test -z "$CFLAGS"    && DEFAULT_CFLAGS=yes
test -z "$CXXFLAGS"  && DEFAULT_CXXFLAGS=yes
test -z "$LDFLAGS"   && DEFAULT_LDFLAGS=yes

dnl Checks for programs.
AC_PROG_CC
gl_EARLY
AC_PROG_CXX

AC_PROG_YACC
if test x$ac_cv_lib_fl_yywrap = xno; then
   AC_DEFINE(NEED_YYWRAP, 1, [need yywrap]) dnl ?
fi

dnl POSIXCHECK is worthwhile for maintainers, but adds several seconds
dnl (more than 10% execution time) to ./configure, with no benefit for
dnl most users.  Using it to look for bugs requires:
dnl   GNULIB_POSIXCHECK=1 autoreconf -f
dnl   ./configure
dnl   make
dnl   make -C src clean
dnl   make CFLAGS=-DGNULIB_POSIXCHECK=1
m4_syscmd([test "${GNULIB_POSIXCHECK+set}" = set])
m4_if(m4_sysval, [0], [], [dnl
gl_ASSERT_NO_GNULIB_POSIXCHECK])

AC_SYS_LARGEFILE

AC_ARG_WITH(debug,
   AS_HELP_STRING([--with-debug], [enable debug info]),
[   with_debug=$withval;   ],
[   with_debug=no;	   ])

AC_ARG_WITH(profiling,
   AS_HELP_STRING([--with-profiling], [enable profiling]),
[   with_profiling=$withval;   ],
[   with_profiling=no;	   ])

AC_ARG_ENABLE([packager-mode],
              AS_HELP_STRING([--enable-packager-mode],
                             [Change configuration behavior
                              to ease packaging]),
              [if test x"${enableval}" = xyes
               then
				   DEFAULT_CXX=no
				   DEFAULT_CFLAGS=no
				   DEFAULT_CXXFLAGS=no
				   DEFAULT_LDFLAGS=no
               fi])

if test x$with_debug = xno; then
   if test x$DEFAULT_CFLAGS = xyes; then
      CFLAGS="`echo $CFLAGS | sed 's/-g//'`"
      if test -z "$CFLAGS"; then
	 CFLAGS=-O
      fi
   fi
   if test x$DEFAULT_CXXFLAGS = xyes; then
      CXXFLAGS="`echo $CXXFLAGS | sed 's/-g//'`"
      if test -z "$CXXFLAGS"; then
	 CXXFLAGS=-O
      fi
   fi
   # don't strip when profiling
   if test x$DEFAULT_LDFLAGS = xyes -a x$with_profiling != xyes; then
      case "`uname -s`" in
      Darwin)  ;;
      *)       LDFLAGS="$LDFLAGS -s";;
      esac
   fi
fi

if test x$with_debug = xyes; then
   dnl m4 will mangle brackets
   if test x$DEFAULT_CFLAGS = xyes; then
      CFLAGS="`echo $CFLAGS | sed 's/-O@<:@0-9@:>@\?//'`"
   fi
   if test x$DEFAULT_CXXFLAGS = xyes; then
      CXXFLAGS="`echo $CXXFLAGS | sed 's/-O@<:@0-9@:>@\?//'`"
   fi
fi

if test x$with_profiling = xyes; then
   CFLAGS="$CFLAGS -pg"
   CXXFLAGS="$CXXFLAGS -pg"
   # profiling requires debugging, too, but don't nuke -O
#   if test x$with_debug != xyes; then
#      CFLAGS="$CFLAGS -g"
#      CXXFLAGS="$CXXFLAGS -g"
#   fi
fi

if test x$GCC = xyes; then
   CFLAGS="$CFLAGS -Wall"
fi
if test x$GXX = xyes; then
   CXXFLAGS="$CXXFLAGS -Wall -Wwrite-strings -Woverloaded-virtual"
   # save some bytes
   LFTP_CHECK_CXX_FLAGS([-fno-exceptions -fno-rtti])
   # check for -fno-implement-inline (doesn't work without -O in gcc 2.95.4; functions are never inlined)
   if test x$with_debug = xno; then
      LFTP_NOIMPLEMENTINLINE
   fi
fi

dnl Make sure C++ build environment is sane.
LFTP_CXX_TEST
CXX_DYNAMIC_INITIALIZERS
LFTP_CXX_BOOL
LFTP_CXX__BOOL
LFTP_CXX_ANSI_SCOPE
LFTP_CXX_STDC_LIMIT_MACROS

AC_SEARCH_LIBS([strerror],[cposix])

case "`uname -sr`" in
# Linux-2.2.x and older don't support fcntl64.
Linux\ 2.[012].*) enable_largefile=no;;
esac

dnl ** LFTP needs iconv, call the test explicitly (even if --disable-nls)
AM_ICONV

ALL_LINGUAS="de es fr it ja ko pl pt_BR ru uk zh_CN zh_TW zh_HK cs"
AM_GNU_GETTEXT([external])
AM_GNU_GETTEXT_VERSION([0.15])
test "$MSGFMT"  = "no" && MSGFMT  ="$missing_dir/missing msgfmt"
test "$GMSGFMT" = "no" && GMSGFMT ="$missing_dir/missing msgfmt"
test "$XGETTEXT" = ":" && XGETTEXT="$missing_dir/missing xgettext"

if test x$gt_cv_func_gettext_libintl = xyes; then
   case "$LIBS" in
   *-lintl*)   ;;
   *)	       LIBS="$LIBS -lintl";;
   esac
fi

AC_ARG_WITH(pager,
   AS_HELP_STRING([--with-pager=/path], [use specified pager by default]),
[  with_pager=$withval;	   ],
[  with_pager="exec more"; ])
AC_DEFINE_UNQUOTED([DEFAULT_PAGER], "$with_pager", [Default pager command])

AC_ARG_WITH(socks,
   AS_HELP_STRING([--with-socks@<:@=/path@:>@], [build with SOCKSv4 library]),
[  with_socks=$withval;	],
[  with_socks=no;	])
AC_ARG_WITH(socks5,
   AS_HELP_STRING([--with-socks5@<:@=/path@:>@], [build with SOCKSv5 library]),
[  with_socks5=$withval;],
[  with_socks5=no;	])
AC_ARG_WITH(socksdante,
   AS_HELP_STRING([--with-socksdante@<:@=/path@:>@], [build with SOCKS-Dante library]),
[  with_socksdante=$withval;],
[  with_socksdante=no;	])

case "$with_socks" in
yes|no|"") ;;
*)	 socks_loc=$with_socks
	 with_socks=yes;;
esac
case "$with_socks5" in
yes|no|"") ;;
*)	 socks_loc=$with_socks5
	 with_socks5=yes;;
esac
case "$with_socksdante" in
yes|no|"") ;;
*)	 socks_loc=$with_socksdante
	 with_socksdante=yes;;
esac

if test x$socks_loc != x; then
   LDFLAGS="$LDFLAGS -L$socks_loc/lib"
   CPPFLAGS="$CPPFLAGS -I$socks_loc/include"
fi

if test x$with_socks = xyes; then
   AC_DEFINE(SOCKS4, 1, [define if you are building with SOCKS support])
   AC_CHECK_LIB(socks, main, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR([cannot find -lsocks library])])
fi
if test x$with_socks5 = xyes; then
   AC_DEFINE(SOCKS5, 1, [define if you are building with SOCKSv5 support])
   AC_CHECK_LIB(socks5, main, [SOCKSLIBS=-lsocks5],
      [AC_MSG_ERROR([cannot find -lsocks5 library])])
fi
if test x$with_socksdante = xyes; then
   AC_DEFINE(SOCKS_DANTE, 1, [define if you are building with SOCKS-Dante support])
   AC_CHECK_LIB(socks, main, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR([cannot find -lsocks library])])
fi

AC_SUBST(SOCKSLIBS)
if test -n "$SOCKSLIBS"; then
   old_LIBS="$LIBS"
   LIBS="$LIBS $SOCKSLIBS"
   AC_CHECK_FUNCS([Rpoll])
   if test "$ac_cv_func_Rpoll" != yes; then
      gl_cv_func_poll=no
   fi
   LIBS="$old_LIBS"
fi

AC_ARG_WITH(modules,
   AS_HELP_STRING([--with-modules], [build modular lftp (protocols become dll's)]),
[  with_modules=$withval;  ],
[  with_modules=no;	   ])

AM_CONDITIONAL([WITH_MODULES], [test "$with_modules" = yes])
if test "$with_modules" = yes; then
   enable_static=no
   enable_shared=yes
   AC_DEFINE(WITH_MODULES, 1, [build modular lftp])
else
   enable_static=yes
   enable_shared=no
fi

#test -z "$enable_static" && enable_static=no
LT_INIT([dlopen])
LIBTOOL="$LIBTOOL --silent"

gl_INIT

if test x$ac_cv_func_wcwidth != xyes || test x$REPLACE_WCWIDTH = x1; then
   gl_LIBOBJ([wcwidth])
   AC_DEFINE([REPLACE_WCWIDTH], 1, [Define when using wcwidth replacement])
fi

dnl Checks for libraries.
dnl LFTP_CHECK_LIBM
AC_SEARCH_LIBS([socket],[socket])
AC_SEARCH_LIBS([gethostbyname],[nsl])
AC_SEARCH_LIBS([dlopen],[dl],[AC_DEFINE(HAVE_DLOPEN, 1, [have dlopen])])
AC_SEARCH_LIBS([res_9_search],[resolv],[AC_DEFINE(HAVE_RES_9_SEARCH, 1, [have res_9_search])])
AC_SEARCH_LIBS([res_search],[resolv bind],[AC_DEFINE(HAVE_RES_SEARCH, 1, [have res_search])])
AC_CHECK_DECLS([res_search],,, [
     #include <stdio.h>
     #include <sys/types.h>
     #include <netinet/in.h>
     #include <arpa/nameser.h>
     #include <resolv.h>
])

AC_ARG_ENABLE([ipv6],
	AS_HELP_STRING([--disable-ipv6],[disable IPv6 code]),
        [
		if test x$enableval = xno; then
			AC_DEFINE([DISABLE_IPV6],[1],[Disable IPv6 code])
		fi
	]
)

AC_ARG_WITH(libresolv, AS_HELP_STRING([--without-libresolv], [don't use libresolv]),
      [with_libresolv=$withval], [with_libresolv=yes])
if test x$with_libresolv = xyes; then
   AC_SEARCH_LIBS(hstrerror, resolv)
fi

lftp_TERMINFO
lftp_LIB_READLINE([5.0])
if test "$HAVE_READLINE" != "yes"; then
   AC_MSG_ERROR([cannot find readline library, install readline-devel package])
fi

AC_ARG_WITH(gnutls, AS_HELP_STRING([--without-gnutls], [don't use GNUTLS library]),
      [with_gnutls=$withval], [with_gnutls=yes])
AC_ARG_WITH(openssl,
AS_HELP_STRING([--with-openssl@<:@=/path@:>@], [use OpenSSL @<:@at /path@:>@])
AS_HELP_STRING([--without-openssl], [don't use OpenSSL (default)]),
      [with_openssl=$withval], [with_openssl=no])
case "$with_openssl" in
	yes)		with_gnutls=no;;
	""|no)		: ;;
	*)		openssl_loc=$with_openssl
			with_openssl=yes
			with_gnutls=no;;
esac
if test x$with_gnutls = xyes; then
   PKG_PROG_PKG_CONFIG
   PKG_CHECK_MODULES([LIBGNUTLS], [gnutls >= 1.0.0], [
      AC_DEFINE([USE_GNUTLS], 1, [Define to 1 when using GNU TLS library])
      gnutls_version_code=`$PKG_CONFIG --modversion gnutls | $AWK -F. '{ printf "0x%02X%02X%02X\n",$1,$2,$3 }'`
      AC_DEFINE_UNQUOTED([LFTP_LIBGNUTLS_VERSION_CODE], $gnutls_version_code, [Define to libgnutls version, e.g. 0x010203 for 1.2.3])
   ])
fi
if test x$with_openssl = xyes -a x"$LIBGNUTLS_LIBS" = x; then
   LFTP_OPENSSL_CHECK
fi

AX_CHECK_ZLIB([
   AC_SUBST([ZLIB],[-lz])
   r=""; test "$enable_rpath" = yes -a "$ZLIB_HOME" != /usr && r=" -R${ZLIB_HOME}/lib"
   AC_SUBST([ZLIB_LDFLAGS],["-L${ZLIB_HOME}/lib$r"])
   AC_SUBST([ZLIB_CPPFLAGS],["-I${ZLIB_HOME}/include"])
],[
   AC_MSG_ERROR([cannot find -lz library, install zlib-devel package])
])

AX_LIB_EXPAT
if test "x$HAVE_EXPAT" = xyes; then
   AC_DEFINE(HAVE_LIBEXPAT, 1, [Define if you have expat library])
fi

# Check whether user wants DNSSEC local validation support
AC_ARG_WITH(dnssec-local-validation,
        [  --with-dnssec-local-validation Enable local DNSSEC validation using libval (default=no)], want_dnssec=$withval, want_dnssec=no)
if test "x$want_dnssec" = "xyes"; then
    AC_CHECK_HEADERS(validator/validator.h)
    if test "$ac_cv_header_validator_validator_h" != yes; then
        AC_MSG_ERROR([Can't find validator.h (from dnssec-tools)])
    fi
    AC_SEARCH_LIBS([SHA1_Init], [crypto ssl])
    AC_CHECK_LIB(sres, query_send)
    if test "$ac_cv_lib_sres_query_send" != yes; then
        AC_MSG_ERROR([Can't find libsres (from dnssec-tools)])
    fi
    AC_CHECK_LIB(val, p_val_status,[LIBS="$LIBS -lval"])
    if test "x$ac_cv_lib_val_p_val_status" = "xno"; then
        AC_CHECK_LIB(pthread, pthread_rwlock_init)
	AC_CHECK_LIB(val-threads, p_val_status,
                [LIBS="$LIBS -lval-threads -lpthread" LIBVAL_SUFFIX="-threads"],
                AC_MSG_ERROR([Can't find libval or libval-threads (from dnssec-tools)]))
    fi
    if test "x$ac_cv_lib_val_p_val_status" = "xyes" -o "x$ac_cv_lib_val_threads_p_val_status" = "xyes"; then
        AC_DEFINE(DNSSEC_LOCAL_VALIDATION, 1,
              [Define if you want local DNSSEC validation support])
    fi
fi

LFTP_PTY_CHECK
LFTP_POSIX_FALLOCATE_CHECK
LFTP_POSIX_FADVISE_CHECK
LFTP_LIBIDN2_CHECK

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS(libintl.h fcntl.h sys/time.h errno.h stdlib.h varargs.h dirent.h\
 termios.h termio.h sys/select.h sys/stropts.h string.h memory.h\
 strings.h sys/ioctl.h dlfcn.h arpa/inet.h arpa/nameser.h netinet/in.h netinet/tcp.h\
 netinet/in_systm.h netinet/ip.h termcap.h sys/statfs.h ifaddrs.h\
 resolv.h langinfo.h endian.h locale.h expat.h linux/magic.h socks.h,,,[
#include <sys/types.h>
#ifdef HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_NETINET_IN_SYSTM_H
# include <netinet/in_systm.h>
#endif
])

LFTP_NEED_TRIO

# See if we have h_errno (the test is here so we can use -lresolv if necessary).
AC_CACHE_CHECK([for h_errno], inetutils_cv_var_h_errno,
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include <netdb.h>]], [[ #ifndef h_errno
      extern int h_errno;
      #endif
      int iu_x = h_errno; ]])],[inetutils_cv_var_h_errno=yes],[inetutils_cv_var_h_errno=no]))
if test "$inetutils_cv_var_h_errno" = yes; then
  AC_DEFINE([HAVE_H_ERRNO], 1, [system has h_errno])
  AC_CHECK_DECLS([h_errno],,, [#include <netdb.h>])
fi

# See if the system has hstrerror, and replace it if not
AC_CHECK_FUNC(hstrerror, , [gl_LIBOBJ(hstrerror)])
if test "$ac_cv_func_hstrerror" = yes; then
  AC_CHECK_DECLS([hstrerror],,, [#include <netdb.h>])
else
  # No hstrerror, so see if the H_ERRLIST variable can be used by ours
  AC_CHECK_FUNC([h_errlist],
    AC_CHECK_DECLS([h_errlist],,, [#include <netdb.h>]))
fi
if test "$ac_cv_func_hstrerror" = yes -o "$ac_cv_func_h_errlist" = yes; then
  # If there's a system hstrerror, or we can reasonably replace it, say so.
  # We still provide some definition, regardless, but this allows people to use
  # a reasonable alternative if the situation allows, rather than using a
  # degenerate version that only says `Host lookup error N'.
  AC_DEFINE(HAVE_HSTRERROR, 1, [System has usable hstrerror])
fi

dnl Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_PID_T
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
if test x$ac_cv_header_endian_h = xno; then
   AC_C_BIGENDIAN
fi
AC_CHECK_MEMBERS([struct sockaddr.sa_len],,,
                 [#include <sys/types.h>
                  #include <sys/socket.h>])
AC_CHECK_MEMBERS([struct sockaddr_in6.sin6_scope_id],,,
                 [#include <sys/types.h>
                  #include <sys/socket.h>
		  #include <netinet/in.h>])
AC_STRUCT_TIMEZONE
AC_STRUCT_TM

dnl Checks for library functions.
AC_FUNC_UTIME_NULL
AC_FUNC_GETPGRP
AC_FUNC_VPRINTF
AC_HEADER_STAT
LFTP_FUNC_SSCANF_CONST
AC_CHECK_FUNCS([statfs\
 killpg setpgid tcgetattr vsnprintf snprintf sscanf \
 gethostbyname2 getipnodebyname getaddrinfo getnameinfo setsid random\
 inet_aton setlocale dn_expand socketpair fallocate])
lftp_VA_COPY
LFTP_ENVIRON_CHECK
AC_CHECK_DECLS([vsnprintf,snprintf,unsetenv,random,inet_aton,strptime,strtok_r,dn_expand,memmem],,,[
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#if STDC_HEADERS || HAVE_STRING_H
# include <string.h>
# if !STDC_HEADERS && HAVE_MEMORY_H
#  include <memory.h>
# endif
#else
# include <strings.h>
#endif
#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif
#ifdef HAVE_RESOLV_H
# include <resolv.h>
#endif
])

if test "$prefix" = /usr -a "$sysconfdir" = '${prefix}/etc'; then
   sysconfdir=/etc
fi

AH_TOP([
#ifndef CONFIG_H
#define CONFIG_H
])

AH_BOTTOM([
#if defined(__cplusplus) && defined(inline)
# undef /**/ inline
#endif

#if !defined(HAVE_SETPGID) && !defined(SETPGRP_VOID)
# define setpgid(pid, pgrp)  setpgrp((pid),(pgrp))
#endif

#ifdef __cplusplus
# define CDECL extern "C"
# define CDECL_BEGIN CDECL {
# define CDECL_END   }
template<typename T>
static inline T replace_value(T &var,T new_value)
{
   T tmp=var;
   var=new_value;
   return tmp;
}
#else
# define CDECL
# define CDECL_BEGIN
# define CDECL_END
#endif

/* should be included before any real code. */
#include <alloca.h>

#include "gettext.h"
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

#ifndef PARAMS
# if defined __STDC__ || defined __cplusplus
#  define PARAMS(x) x
# else
#  define PARAMS(x) ()
# endif
#endif

#if !defined(DISABLE_IPV6) && defined(HAVE_IPV6) \
    && defined(HAVE_GETNAMEINFO) && defined(HAVE_GETADDRINFO)
# define INET6 1
#endif

#if (defined(HAVE_EXPAT_H) && defined(HAVE_LIBEXPAT))
# define USE_EXPAT 1
#endif

#if defined(SOCKS4) || defined(SOCKS5) || defined(SOCKS_DANTE)
# define SOCKS 1
#endif
#ifdef SOCKS
# if defined(HAVE_SOCKS_H)
CDECL_BEGIN
#  include <socks.h>
CDECL_END
#  undef /**/ getc /* fix a problem with redefined getc */
# else // !HAVE_SOCKS_H
#  if defined(SOCKS4) || defined(SOCKS_DANTE)
#   define connect     Rconnect
#   define getsockname Rgetsockname
#   define bind	     Rbind
#   define accept	     Raccept
#   define listen	     Rlisten
#   define select	     Rselect
#   ifdef HAVE_RPOLL
#    define poll	     Rpoll
#   endif
CDECL int SOCKSinit(const char *);
#  endif
#  ifdef SOCKS_DANTE
#   define rresvport Rrresvport
#   define bindresvport Rbindresvport
#   define gethostbyname Rgethostbyname
#   define gethostbyname2 Rgethostbyname2
#   define sendto Rsendto
#   define recvfrom Rrecvfrom
#   define recvfrom Rrecvfrom
#   define write Rwrite
#   define writev Rwritev
#   define send Rsend
#   define sendmsg Rsendmsg
#   define read Rread
#   define readv Rreadv
#   define recv Rrecv
#   define recvmsg Rrecvmsg
#  endif
# endif // !HAVE_SOCKS_H
#endif // SOCKS

#if defined __MSDOS__ || defined __CYGWIN32__
# define NATIVE_CRLF 1
#endif

#define E_RETRY(e) ((e)==EAGAIN || (e)==EWOULDBLOCK || (e)==EINTR)
#define E_LOCK_IGNORE(e) ((e)==EINVAL || (e)==ENOLCK)

#ifndef HAVE_RANDOM
#define srandom(x) srand((x))
#define random() ((long)(rand()/(RAND_MAX+1.0)*2147483648.0))
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
# define __attribute__(x)
#endif
/* Attribute `sentinel' was valid as of gcc 3.5.  */
#if defined(__GNUC__) && \
    (__GNUC__ > 3 || (__GNUC__ == 3 && __GNUC_MINOR__ >= 5))
# define ATTRIBUTE_SENTINEL __attribute__ ((__sentinel__))
#else
# define ATTRIBUTE_SENTINEL
#endif

#ifdef TRIO_REPLACE_STDIO
/* replace all the functions */
# undef /**/ HAVE_PRINTF
# undef /**/ HAVE_VPRINTF
# undef /**/ HAVE_FPRINTF
# undef /**/ HAVE_VFPRINTF
# undef /**/ HAVE_SPRINTF
# undef /**/ HAVE_VSPRINTF
# undef /**/ HAVE_SNPRINTF
# undef /**/ HAVE_VSNPRINTF
# undef /**/ HAVE_SCANF
# undef /**/ HAVE_VSCANF
# undef /**/ HAVE_FSCANF
# undef /**/ HAVE_VFSCANF
# undef /**/ HAVE_SSCANF
# undef /**/ HAVE_VSSCANF
# include "trio.h"
#endif

#if defined(USE_GNUTLS) || defined(USE_OPENSSL)
# define USE_SSL 1
#endif

#endif /* CONFIG_H */
])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([lib/Makefile])
AC_CONFIG_FILES([doc/Makefile])
AC_CONFIG_FILES([po/Makefile.in])
AC_CONFIG_FILES([m4/Makefile])
AC_CONFIG_FILES([trio/Makefile])
AC_CONFIG_FILES([contrib/Makefile])
AC_CONFIG_FILES([tests/Makefile])
AC_CONFIG_FILES([lftp.spec])

AC_OUTPUT
