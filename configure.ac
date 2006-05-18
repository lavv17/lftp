dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.53)
AC_INIT([lftp], [3.4.7], [lftp-bugs@lftp.yar.ru])
AM_INIT_AUTOMAKE
AC_CONFIG_LIBOBJ_DIR(lib)
AC_CONFIG_SRCDIR([src/ftpclass.cc])
AM_CONFIG_HEADER(include/config.h)

# This doesn't *require* GNU extensions; it merely enables them if
# they're there.
AC_GNU_SOURCE

test -z "$CXX"	     && DEFAULT_CXX=yes
test -z "$CFLAGS"    && DEFAULT_CFLAGS=yes
test -z "$CXXFLAGS"  && DEFAULT_CXXFLAGS=yes
test -z "$LDFLAGS"   && DEFAULT_LDFLAGS=yes

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX

AC_SYS_LARGEFILE

AC_ARG_WITH(debug,
[  --with-debug            enable debug info],
[   with_debug=$withval;   ],
[   with_debug=no;	   ])

AC_ARG_WITH(profiling,
[  --with-profiling        enable profiling],
[   with_profiling=$withval;   ],
[   with_profiling=no;	   ])

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
   # m4 will mangle brackets
   if test x$DEFAULT_CFLAGS = xyes; then
      CFLAGS="`echo $CFLAGS | sed 's/-O[[0-9]]\?//'`"
   fi
   if test x$DEFAULT_CXXFLAGS = xyes; then
      CXXFLAGS="`echo $CXXFLAGS | sed 's/-O[[0-9]]\?//'`"
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
   if test x$DEFAULT_CXX = xyes; then
      LFTP_PROG_CXXLINK
   fi
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
LFTP_CXX_ANSI_SCOPE

AC_ISC_POSIX

# for lib/mbswidth.{c,h}
jm_PREREQ_MBSWIDTH

# for lib/human.{c,h}
jm_PREREQ_HUMAN

jm_FUNC_MBRTOWC
AC_MBSTATE_T

case "`uname -sr`" in
# Linux-2.2.x and older don't support fcntl64.
"Linux 2.2."*|"Linux 2.0."*)  enable_largefile=no;;
# MacOS X Tiger's poll does not work correctly.
"Darwin 8."*)  lftp_cv_func_poll_works=no;;
esac

AC_PROG_YACC

if test x$ac_cv_lib_fl_yywrap = xno; then
   AC_DEFINE(NEED_YYWRAP, 1, [need yywrap]) dnl ?
fi

dnl ** LFTP needs iconv, call the test explicitly (even if --disable-nls)
AM_ICONV
ALL_LINGUAS="de es fr it ja ko pl pt_BR ru zh_CN zh_TW"
AM_GNU_GETTEXT([external])
if test x$USE_INCLUDED_LIBINTL = xyes; then
   LINK_SRC="$LINK_SRC intl/libgnuintl.h"
   LINK_DST="$LINK_DST include/libintl.h"
fi
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
[  --with-pager=/path      use specified pager by default],
[  with_pager=$withval;	   ],
[  with_pager="exec more"; ])
AC_DEFINE_UNQUOTED([DEFAULT_PAGER], "$with_pager", [Default pager command])

AC_ARG_WITH(socks,
[  --with-socks[=/path]    build with SOCKSv4 library],
[  with_socks=$withval;	],
[  with_socks=no;	])
AC_ARG_WITH(socks5,
[  --with-socks5[=/path]   build with SOCKSv5 library],
[  with_socks5=$withval;],
[  with_socks5=no;	])
AC_ARG_WITH(socksdante,
[  --with-socksdante[=/path]  build with SOCKS-Dante library],
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
   CPPFLAGS="$CPPFLAGS -L$socks_loc/include"
fi

if test x$with_socks = xyes; then
   AC_DEFINE(SOCKS4, 1, [define if you are building with SOCKS support])
   AC_CHECK_LIB(socks, main, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR(cannot find -lsocks library)])
fi
if test x$with_socks5 = xyes; then
   AC_DEFINE(SOCKS5, 1, [define if you are building with SOCKSv5 support])
   AC_CHECK_LIB(socks5, main, [SOCKSLIBS=-lsocks5],
      [AC_MSG_ERROR(cannot find -lsocks5 library)])
fi
if test x$with_socksdante = xyes; then
   AC_DEFINE(SOCKS_DANTE, 1, [define if you are building with SOCKS-Dante support])
   AC_CHECK_LIB(socks, main, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR(cannot find -lsocks library)])
fi

AC_SUBST(SOCKSLIBS)
if test -n "$SOCKSLIBS"; then
   old_LIBS="$LIBS"
   LIBS="$LIBS $SOCKSLIBS"
   AC_CHECK_FUNCS([Rpoll])
   if test "$ac_cv_func_Rpoll" != yes; then
      lftp_cv_func_poll_works=no
   fi
   LIBS="$old_LIBS"
fi

AC_ARG_WITH(modules,
[  --with-modules          build modular lftp (protocols become dll's)],
[  with_modules=$withval;  ],
[  with_modules=no;	   ])

AM_CONDITIONAL([WITH_MODULES], [test "$with_modules" = yes])
if test "$with_modules" = yes; then
   enable_static=no
   enable_shared=yes
   AC_DEFINE(WITH_MODULES, 1, [build modular lftp])
   MODULES_LA="liblftp-pty.la liblftp-network.la proto-ftp.la proto-http.la proto-file.la proto-fish.la proto-sftp.la cmd-mirror.la cmd-sleep.la"
   LFTP_SSL_LDFLAGS=
   LFTP_SSL_LIBS=
   LFTP_EXPAT_LIBS=
else
   enable_static=yes
   enable_shared=no
   MODULES_LA_STATIC="liblftp-pty.la liblftp-network.la proto-ftp.la proto-http.la proto-file.la proto-fish.la proto-sftp.la cmd-mirror.la cmd-sleep.la"
   LFTP_OPENSSL_LDFLAGS='$(OPENSSL_LDFLAGS)'
   LFTP_OPENSSL_LIBS='$(OPENSSL_LIBS)'
   LFTP_LIBGNUTLS_LIBS='$(LIBGNUTLS_LIBS)'
   LFTP_EXPAT_LIBS='$(EXPAT_LIBS)'
fi
AC_SUBST(MODULES_LA)
AC_SUBST(MODULES_LA_STATIC)
AC_SUBST(LFTP_OPENSSL_LDFLAGS)
AC_SUBST(LFTP_OPENSSL_LIBS)
AC_SUBST(LFTP_LIBGNUTLS_LDFLAGS)
AC_SUBST(LFTP_LIBGNUTLS_LIBS)
AC_SUBST(LFTP_EXPAT_LIBS)

#test -z "$enable_static" && enable_static=no
AM_PROG_LIBTOOL
LIBTOOL="$LIBTOOL --silent"

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

AC_ARG_WITH(libresolv, [  --without-libresolv     don't use libresolv],
      [with_libresolv=$withval], [with_libresolv=yes])
if test x$with_libresolv = xyes; then
   AC_SEARCH_LIBS(hstrerror, resolv)
fi

lftp_TERMINFO

READLINE_CHECK

AC_ARG_WITH(gnutls, [  --without-gnutls              don't use GNUTLS library],
      [with_gnutls=$withval], [with_gnutls=yes])
AC_ARG_WITH(openssl, [  --with-openssl[=/path]          use OpenSSL [at /path]
  --without-openssl             don't use OpenSSL (default)],
      [with_openssl=$withval], [with_openssl=no])
case "$with_openssl" in
	yes)		with_gnutls=no;;
	""|no)		: ;;
	*)		openssl_loc=$with_openssl
			with_openssl=yes
			with_gnutls=no;;
esac
if test x$with_gnutls = xyes; then
   AM_PATH_LIBGNUTLS([1.0.0], [
      AC_DEFINE([USE_GNUTLS], 1, [Define to 1 when using GNU TLS library])
      gnutls_version_code=`echo $libgnutls_config_version | $AWK -F. '{ printf "0x%02X%02X%02X\n",$1,$2,$3 }'`
      AC_DEFINE_UNQUOTED([LFTP_LIBGNUTLS_VERSION_CODE], $gnutls_version_code, [Define to libgnutls version, e.g. 0x010203 for 1.2.3])
   ])
fi
if test x$with_openssl = xyes -a x"$LIBGNUTLS_LIBS" = x; then
   LFTP_OPENSSL_CHECK
fi

AC_CHECK_LIB(expat, XML_ParserCreateNS, [
   EXPAT_LIBS=-lexpat
   AC_SUBST(EXPAT_LIBS)
   AC_DEFINE(HAVE_LIBEXPAT, 1, [Define if you have expat library])
])

LFTP_PTY_CHECK

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS(fcntl.h sys/time.h errno.h stdlib.h varargs.h dirent.h\
 termios.h termio.h sys/select.h sys/poll.h sys/stropts.h string.h memory.h\
 strings.h sys/ioctl.h dlfcn.h arpa/inet.h arpa/nameser.h netinet/in.h netinet/tcp.h\
 netinet/in_systm.h netinet/ip.h termcap.h\
 resolv.h langinfo.h endian.h regex.h locale.h expat.h,,,[
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

# See if the system has strerror and replace if not
AC_CHECK_FUNC([strerror], AC_DEFINE(HAVE_STRERROR, 1, [System has usable strerror]),[
  AC_LIBOBJ([strerror])
  # No strerror, so see if the SYS_ERRLIST variable can be used by ours
  AC_CHECK_FUNC([sys_errlist],
    AC_CHECK_DECLS([sys_errlist],,, [
      #include <stdio.h>
      #ifdef HAVE_ERRNO_H
      #include <errno.h>
      #endif]))])

LFTP_NEED_TRIO

# See if we have h_errno (the test is here so we can use -lresolv if necessary).
AC_CACHE_CHECK([for h_errno], inetutils_cv_var_h_errno,
  AC_TRY_COMPILE([#include <netdb.h>],
    [ #ifndef h_errno
      extern int h_errno;
      #endif
      int iu_x = h_errno; ],
    inetutils_cv_var_h_errno=yes, inetutils_cv_var_h_errno=no))
if test "$inetutils_cv_var_h_errno" = yes; then
  AC_DEFINE([HAVE_H_ERRNO], 1, [system has h_errno])
  AC_CHECK_DECLS([h_errno],,, [#include <netdb.h>])
fi

# See if the system has hstrerror, and replace it if not
AC_CHECK_FUNC(hstrerror, , [AC_LIBOBJ(hstrerror)])
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
TYPE_SOCKLEN_T
AC_CHECK_MEMBERS([struct sockaddr.sa_len],,,
                 [#include <sys/types.h>
                  #include <sys/socket.h>])
AC_STRUCT_TIMEZONE
AC_STRUCT_TM

dnl Checks for library functions.
AC_TYPE_SIGNAL
AC_FUNC_UTIME_NULL
AC_FUNC_GETPGRP
AC_FUNC_VPRINTF
AC_FUNC_FNMATCH_GNU
AC_HEADER_STAT
LFTP_FUNC_POLL
LFTP_FUNC_SSCANF_CONST
AC_CHECK_FUNCS([strdup strstr lstat lchown select regexec glob\
 killpg setpgid tcgetattr ftruncate vsnprintf snprintf sscanf gettimeofday\
 gethostbyname2 getipnodebyname getaddrinfo getnameinfo setsid random\
 inet_aton unsetenv setlocale nl_langinfo dn_expand])
AC_FUNC_ALLOCA
lftp_VA_COPY
LFTP_ENVIRON_CHECK
AC_REPLACE_FUNCS([mktime memmove strcasecmp strncasecmp strtol strtoul\
 strptime strtok_r memmem])
AC_CHECK_DECLS([vsnprintf,snprintf,unsetenv,random,inet_aton,strcasecmp,strptime,strtok_r,dn_expand,memmem],,,[
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

# always want GNU getopt
AC_LIBOBJ(getopt)
AC_LIBOBJ(getopt1)

if test x$lftp_cv_func_poll_works = xno; then
   AC_LIBOBJ(poll)
   if test x$ac_cv_header_sys_poll_h = xno; then
      AC_CONFIG_LINKS([include/poll.h:lib/poll.h])
   fi
fi
if test x$ac_cv_func_regexec = xno -o x$ac_cv_header_regex_h = xno; then
   AC_LIBOBJ(regex)
   AC_CONFIG_LINKS([include/regex.h:lib/regex.h])
fi
if test x$ac_cv_func_glob = xno; then
   AC_LIBOBJ(glob)
   AC_CONFIG_LINKS([include/glob.h:lib/glob.h])
fi
if test x$ac_cv_func_fnmatch_gnu = xno; then
   AC_LIBOBJ(fnmatch)
   AC_CONFIG_LINKS([include/fnmatch.h:lib/fnmatch_.h])
   AC_DEFINE_UNQUOTED(fnmatch, rpl_fnmatch, [Define to rpl_fnmatch if system fnmatch does not work])
fi

if test "$prefix" = /usr -a "$sysconfdir" = '${prefix}/etc'; then
   sysconfdir=/etc
fi

AH_TOP([
#ifndef CONFIG_H
#define CONFIG_H
])

AH_BOTTOM([
#include <post-config.h>
#endif /* CONFIG_H */
])

AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([src/Makefile])
AC_CONFIG_FILES([lib/Makefile])
AC_CONFIG_FILES([include/Makefile])
AC_CONFIG_FILES([doc/Makefile])
AC_CONFIG_FILES([readline-4.3/Makefile])
AC_CONFIG_FILES([po/Makefile.in])
AC_CONFIG_FILES([m4/Makefile])
AC_CONFIG_FILES([contrib/Makefile])

AC_OUTPUT
