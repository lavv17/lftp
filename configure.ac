dnl Process this file with autoconf to produce a configure script.
AC_PREREQ(2.53)
AC_INIT([lftp], [2.6.7], [lftp-bugs@lftp.yar.ru])
AM_INIT_AUTOMAKE
AC_CONFIG_SRCDIR([src/ftpclass.cc])
AM_CONFIG_HEADER(include/config.h)

# This doesn't *require* GNU extensions; it merely enables them if
# they're there.
AC__GNU_SOURCE

test -z "$CXX"	     && DEFAULT_CXX=yes
test -z "$CFLAGS"    && DEFAULT_CFLAGS=yes
test -z "$CXXFLAGS"  && DEFAULT_CXXFLAGS=yes
test -z "$LDFLAGS"   && DEFAULT_LDFLAGS=yes

dnl Checks for programs.
AC_PROG_CC
AC_PROG_CXX

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

if test "`uname`" = Linux; then
   case "`uname -r`" in
   # Linux-2.2.x and older don't support fcntl64.
   2.2.*|2.0.*)	  enable_largefile=no;;
   esac
fi
AC_SYS_LARGEFILE

AC_PROG_YACC

if test x$ac_cv_lib_fl_yywrap = xno; then
   AC_DEFINE(NEED_YYWRAP, 1, [need yywrap]) dnl ?
fi


AC_ARG_WITH(locale-dir,
  [  --with-locale-dir=DIR   specify locale directory],
  LOCALE_DIR=$withval)
test -z "$LOCALE_DIR" && LOCALE_DIR='$(datadir)/locale'
AC_SUBST(LOCALE_DIR)

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

AC_ARG_WITH(socks,
[  --with-socks            build with SOCKSv4 library],
[  with_socks=$withval;	],
[  with_socks=no;	])
if test x$with_socks = xyes; then
   AC_DEFINE(SOCKS4, 1, [define if you are building with SOCKS support])
   AC_CHECK_LIB(socks, Rconnect, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR(cannot find -lsocks library)])
fi
AC_ARG_WITH(socks5,
[  --with-socks5           build with SOCKSv5 library],
[  with_socks5=$withval;],
[  with_socks5=no;	])
if test x$with_socks5 = xyes; then
   AC_DEFINE(SOCKS5, 1, [define if you are building with SOCKSv5 support])
   AC_CHECK_LIB(socks5, Rconnect, [SOCKSLIBS=-lsocks5],
      [AC_MSG_ERROR(cannot find -lsocks5 library)])
fi
AC_ARG_WITH(socksdante,
[  --with-socksdante       build with SOCKS-Dante library],
[  with_socksdante=$withval;],
[  with_socksdante=no;	])
if test x$with_socksdante = xyes; then
   AC_DEFINE(SOCKS_DANTE, 1, [define if you are building with SOCKS-Dante support])
   AC_CHECK_LIB(socks, Rconnect, [SOCKSLIBS=-lsocks],
      [AC_MSG_ERROR(cannot find -lsocks library)])
fi
LIBS="$LIBS $SOCKSLIBS"
if test "$SOCKSLIBS" = "-lsocks"; then
   AC_CHECK_FUNCS([Rpoll])
   if test "$ac_cv_func_Rpoll" != yes; then
      lftp_cv_func_poll_works=no
   fi
fi

AC_ARG_WITH(modules,
[  --with-modules          build modular lftp (protocols become dll's)],
[  with_modules=$withval;  ],
[  with_modules=no;	   ])

if test "$with_modules" = yes; then
   enable_static=no
   enable_shared=yes
   AC_DEFINE(WITH_MODULES, 1, [build modular lftp])
   MODULES_LA="libnetwork.la proto-ftp.la proto-http.la proto-file.la proto-fish.la cmd-mirror.la cmd-sleep.la"
   LFTP_SSL_LDFLAGS=
   LFTP_SSL_LIBS=
else
   enable_static=yes
   enable_shared=no
   MODULES_LA_STATIC="libnetwork.la proto-ftp.la proto-http.la proto-file.la proto-fish.la cmd-mirror.la cmd-sleep.la"
   LFTP_SSL_LDFLAGS='$(SSL_LDFLAGS)'
   LFTP_SSL_LIBS='$(SSL_LIBS)'
fi
AC_SUBST(MODULES_LA)
AC_SUBST(MODULES_LA_STATIC)
AC_SUBST(LFTP_SSL_LDFLAGS)
AC_SUBST(LFTP_SSL_LIBS)

#test -z "$enable_static" && enable_static=no
AM_PROG_LIBTOOL
LIBTOOL="$LIBTOOL --silent"

dnl Checks for libraries.
LFTP_CHECK_LIBM
dnl This won't work for glibc2
dnl AC_SEARCH_LIBS(md5_process_bytes,crypt,[AC_DEFINE(HAVE_MD5, 1, [md5 support is present in libc or libcrypt])])
AC_SEARCH_LIBS(socket,socket)
AC_SEARCH_LIBS(gethostbyname,nsl)
AC_SEARCH_LIBS(dlopen,dl,[AC_DEFINE(HAVE_DLOPEN, 1, [have dlopen])])
AC_SEARCH_LIBS(res_search,resolv bind,[AC_DEFINE(HAVE_RES_SEARCH, 1, [have res_search])])
IU_CHECK_DECL(res_search, [
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

AC_ARG_WITH(ssl, [  --with-ssl[=/path]        use SSL [at /path]
  --without-ssl           don't use SSL],
      [with_ssl=$withval], [with_ssl=yes])
case "$with_ssl" in
	yes)		: ;;
	""|no)		: ;;
	*)		ssl_loc=$with_ssl
			with_ssl=yes ;;
esac
if test x$with_ssl = xyes; then
   LFTP_SSL_CHECK
fi

LFTP_PTY_CHECK

dnl Checks for header files.
AC_HEADER_DIRENT
AC_HEADER_STDC
AC_HEADER_SYS_WAIT
AC_HEADER_TIME
AC_CHECK_HEADERS(fcntl.h sys/time.h errno.h stdlib.h varargs.h dirent.h\
 termios.h termio.h sys/select.h sys/poll.h sys/stropts.h string.h memory.h\
 strings.h sys/ioctl.h dlfcn.h resolv.h arpa/nameser.h netinet/tcp.h\
 langinfo.h endian.h regex.h locale.h)

# See if the system has strerror and replace if not
AC_CHECK_FUNC(strerror, AC_DEFINE(HAVE_STRERROR, 1, [System has usable strerror]),[
  AC_LIBOBJ(strerror)
  # No strerror, so see if the SYS_ERRLIST variable can be used by ours
  AC_CHECK_FUNC(sys_errlist,
    IU_CHECK_DECL(sys_errlist, [
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
  AC_DEFINE(HAVE_H_ERRNO, 1, [system has h_errno])
  IU_CHECK_DECL(h_errno, [#include <netdb.h>])
fi

# See if the system has hstrerror, and replace it if not
AC_CHECK_FUNC(hstrerror, , AC_LIBOBJ(hstrerror))
if test "$ac_cv_func_hstrerror" = yes; then
  IU_CHECK_DECL(hstrerror, [#include <netdb.h>])
else
  # No hstrerror, so see if the H_ERRLIST variable can be used by ours
  AC_CHECK_FUNC(h_errlist,
    IU_CHECK_DECL(h_errlist, [#include <netdb.h>]))
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
AC_STRUCT_TM

AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
if test x$ac_cv_header_endian_h = xno; then
   AC_C_BIGENDIAN
fi
TYPE_SOCKLEN_T

AC_STRUCT_TIMEZONE

dnl Checks for library functions.
AC_TYPE_SIGNAL
AC_FUNC_UTIME_NULL
AC_FUNC_GETPGRP
AC_FUNC_VPRINTF
AC_FUNC_FNMATCH
LFTP_GNU_FNMATCH
AC_HEADER_STAT
LFTP_FUNC_POLL
LFTP_FUNC_SSCANF_CONST
AC_CHECK_FUNCS(strdup strstr lstat lchown select regexec glob\
 killpg setpgid tcgetattr ftruncate vsnprintf gettimeofday\
 gethostbyname2 getipnodebyname getaddrinfo getnameinfo setsid random\
 inet_aton unsetenv setlocale)
AC_FUNC_ALLOCA
IU_CHECK_DECL(vsnprintf, [#include <stdio.h>])
IU_CHECK_DECL(unsetenv, [#include <stdlib.h>])

lftp_VA_COPY

LFTP_ENVIRON_CHECK

AC_REPLACE_FUNCS(mktime memmove strcasecmp strncasecmp strtol strtoul strptime)
IU_CHECK_DECL(strcasecmp, [#include <string.h>])
IU_CHECK_DECL(strptime, [#include <time.h>])
IU_CHECK_DECL(random, [#include <stdlib.h>])
IU_CHECK_DECL(inet_aton, [
   #include <sys/types.h>
   #include <sys/socket.h>
   #include <netinet/in.h>
   #include <arpa/inet.h>
])

# always want GNU getopt
AC_LIBOBJ(getopt)
AC_LIBOBJ(getopt1)

if test x$lftp_cv_func_poll_works = xno; then
   AC_LIBOBJ(poll)
   AC_CONFIG_LINKS([include/poll.h:lib/poll.h])
fi
if test x$ac_cv_func_regexec = xno -o x$ac_cv_header_regex_h = xno; then
   AC_LIBOBJ(regex)
   AC_CONFIG_LINKS([include/regex.h:lib/regex.h])
fi
if test x$ac_cv_func_glob = xno; then
   AC_LIBOBJ(glob)
   AC_CONFIG_LINKS([include/glob.h:lib/glob.h])
fi
if test x$ac_cv_func_fnmatch_works = xno -o x$lftp_cv_gnu_fnmatch = xno; then
   AC_LIBOBJ(fnmatch)
   AC_CONFIG_LINKS([include/fnmatch.h:lib/fnmatch.h])
   AC_DEFINE_UNQUOTED(fnmatch, rpl_fnmatch, [Define to rpl_fnmatch if system fnmatch does not work])
fi

if test "$prefix" = /usr -a "$sysconfdir" = '${prefix}/etc'; then
   sysconfdir=/etc
fi

AC_CONFIG_FILES([Makefile src/Makefile lib/Makefile include/Makefile doc/Makefile readline-4.3/Makefile po/Makefile.in m4/Makefile contrib/Makefile])
AC_OUTPUT
