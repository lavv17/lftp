dnl Check for libreadline of proper version
AC_DEFUN(READLINE_CHECK,
[AC_MSG_CHECKING(for readline)
AC_CACHE_VAL(lftp_cv_precompiled_readline,
[
   old_LIBS="$LIBS"
   LIBS="-lreadline $READLINE_SUPPLIB $LIBS"
   AC_TRY_LINK([extern int (*rl_getc_function)();],
      [rl_getc_function=0;],
      [AC_TRY_CPP([#include <readline/readline.h>],
		[lftp_cv_precompiled_readline=yes],
		[lftp_cv_precompiled_readline=no])
      ],lftp_cv_precompiled_readline=no)
   LIBS="$old_LIBS"
])
if test $lftp_cv_precompiled_readline = yes; then
   READLINE_DIR=''
   AC_MSG_RESULT(yes)
   READLINE='-lreadline'
   READLINE_DEPEND=''
   COMPILE_READLINE=no
else
   READLINE_DIR=readline-4.1
   AC_MSG_RESULT(no - will compile)
   LINK_SRC="$LINK_SRC $READLINE_DIR"
   LINK_DST="$LINK_DST include/readline"
   READLINE='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   READLINE_DEPEND='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   COMPILE_READLINE=yes
fi
AC_CONFIG_SUBDIRS($READLINE_DIR)
AC_SUBST(COMPILE_READLINE)
AC_SUBST(READLINE_DIR)
AC_SUBST(READLINE_DEPEND)
AC_SUBST(READLINE)
])

dnl check if c++ compiler can use dynamic initializers for static variables
AC_DEFUN(CXX_DYNAMIC_INITIALIZERS,
[
   AC_LANG_SAVE
   AC_LANG_CPLUSPLUS
   AC_MSG_CHECKING(if c++ compiler can handle dynamic initializers)
   AC_TRY_RUN(
   [
      int f() { return 1; }
      int a=f();
      int main()
      {
	 exit(1-a);
      }
   ],
   [cxx_dynamic_init=yes],
   [cxx_dynamic_init=no],
   [cxx_dynamic_init=yes])
   AC_MSG_RESULT($cxx_dynamic_init)
   if test x$cxx_dynamic_init = xno; then
      AC_MSG_ERROR(C++ compiler cannot handle dynamic initializers of static objects)
   fi
   AC_LANG_RESTORE
])

dnl Check for size of addr length argument
AC_DEFUN(TYPE_SOCKLEN_T,
[
   AC_MSG_CHECKING(for socklen_t)
   AC_CACHE_VAL(lftp_cv_socklen_t,
   [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      lftp_cv_socklen_t=no
      AC_TRY_COMPILE([
	 #include <sys/types.h>
	 #include <sys/socket.h>
      ],
      [
	 socklen_t len;
	 getpeername(0,0,&len);
      ],
      [
	 lftp_cv_socklen_t=yes
      ])
      AC_LANG_RESTORE
   ])
   AC_MSG_RESULT($lftp_cv_socklen_t)
   if test $lftp_cv_socklen_t = no; then
      AC_MSG_CHECKING(for socklen_t equivalent)
      AC_CACHE_VAL(lftp_cv_socklen_t_equiv,
      [
	 lftp_cv_socklen_t_equiv=int
	 AC_LANG_SAVE
	 AC_LANG_CPLUSPLUS
	 for t in int size_t unsigned long "unsigned long"; do
	    AC_TRY_COMPILE([
	       #include <sys/types.h>
	       #include <sys/socket.h>
	    ],
	    [
	       $t len;
	       getpeername(0,0,&len);
	    ],
	    [
	       lftp_cv_socklen_t_equiv="$t"
	       break
	    ])
	 done
	 AC_LANG_RESTORE
      ])
      AC_MSG_RESULT($lftp_cv_socklen_t_equiv)
      AC_DEFINE_UNQUOTED(socklen_t, $lftp_cv_socklen_t_equiv)
   fi
])

AC_DEFUN(LFTP_FUNC_POLL,
[
   AC_MSG_CHECKING(for working poll)
   AC_CACHE_VAL(lftp_cv_func_poll_works,
   [
      AC_TRY_RUN([
	       #include <sys/types.h>
	       #ifdef HAVE_SYS_POLL_H
	       #include <sys/poll.h>
	       #else
	       #include <poll.h>
	       #endif

	       int main()
	       {
		  struct pollfd pfd={5,POLLOUT}; /* fd 5 is config.log */
		  exit(!(poll(0,0,0)==0 && poll(&pfd,1,0)==1));
	       }
	    ],
	    [lftp_cv_func_poll_works=yes;],
	    [lftp_cv_func_poll_works=no;],
	    [lftp_cv_func_poll_works=no;])
   ])
   AC_MSG_RESULT($lftp_cv_func_poll_works)
   if test $lftp_cv_func_poll_works = yes; then
      AC_DEFINE(HAVE_POLL)
   fi
])

AC_DEFUN(LFTP_PROG_CXXLINK,
[
   AC_MSG_CHECKING(how to link simple c++ programs)
   if test "$GCC" = yes -a "$GXX" = yes; then
      old_CXX="$CXX"
      CXX="$CC"
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_COMPILE([],[char *a=new char[10]; exit(0);],
	 [],[CXX="$old_CXX";])
      AC_LANG_RESTORE
   fi
   AC_MSG_RESULT(using $CXX)
])

AC_DEFUN(LFTP_FUNC_SSCANF_CONST,
[
   AC_MSG_CHECKING(whether sscanf works on const strings)
   AC_CACHE_VAL(lftp_cv_func_sscanf_const_works,
   [
      AC_TRY_RUN([   #include <stdio.h>
		     int main() {
			int a,b;
			return !(sscanf("123x","%d%n",&a,&b)>0);
		  }],
	    [lftp_cv_func_sscanf_const_works=yes;],
	    [lftp_cv_func_sscanf_const_works=no;],
	    [lftp_cv_func_sscanf_const_works=yes;])
   ])
   AC_MSG_RESULT($lftp_cv_func_sscanf_const_works)
   if test $lftp_cv_func_sscanf_const_works = no; then
      if test "$GCC" = yes; then
	 CFLAGS="$CFLAGS -fwritable-strings"
      else
	 AC_MSG_WARN(sscanf does not work on const strings and not using gcc)
      fi
      if test "$GXX" = yes; then
	 CXXFLAGS="$CXXFLAGS -fwritable-strings"
      else
	 AC_MSG_WARN(sscanf does not work on const strings and not using g++)
      fi
   fi
])

dnl Do nothing if the compiler accepts the inline keyword.
dnl Otherwise define c_inline to __inline__ or __inline if one of those work,
dnl otherwise define c_inline to be empty.
AC_DEFUN(LFTP_C_INLINE,
[AC_CACHE_CHECK([for inline], ac_cv_c_inline,
[ac_cv_c_inline=no
for ac_kw in inline __inline__ __inline; do
  AC_TRY_COMPILE(, [} $ac_kw foo() {], [ac_cv_c_inline=$ac_kw; break])
done
])
case "$ac_cv_c_inline" in
  inline | yes) ;;
  no) AC_DEFINE(c_inline, ) ;;
  *)  AC_DEFINE_UNQUOTED(c_inline, $ac_cv_c_inline) ;;
esac
])

AC_DEFUN(LFTP_NOIMPLEMENTINLINE,
[
   AC_MSG_CHECKING(if -fno-implement-inlines implements virtual functions)
   flags="-fno-implement-inlines -Winline"
   AC_CACHE_VAL(lftp_cv_noimplementinline,
   [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      old_CXXFLAGS="$CXXFLAGS"
      CXXFLAGS="$CXXFLAGS $flags"
      AC_TRY_LINK([
	 class aaa
	 {
	    int var;
	 public:
	    virtual void func() { var=1; }
	    aaa();
	    virtual ~aaa();
	 };
	 aaa::aaa() { var=0; }
	 aaa::~aaa() {}
	 ],[],
	 [lftp_cv_noimplementinline=yes],
	 [lftp_cv_noimplementinline=no])
      CXXFLAGS="$old_CXXFLAGS"
      AC_LANG_RESTORE
   ])
   AC_MSG_RESULT($lftp_cv_noimplementinline)
   if test x$lftp_cv_noimplementinline = xyes; then
      CXXFLAGS="$CXXFLAGS $flags"
   fi
])
AC_DEFUN(LFTP_CHECK_CXX_FLAGS,
[
   flags="$1"
   AC_MSG_CHECKING(if $CXX supports $flags)

      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      old_CXXFLAGS="$CXXFLAGS"
      CXXFLAGS="$CXXFLAGS $flags"
      AC_TRY_COMPILE([
	 class aaa
	 {
	    int var;
	 public:
	    virtual void func() { var=1; }
	    aaa();
	    virtual ~aaa();
	 };
	 aaa::aaa() { var=0; }
	 aaa::~aaa() {}
	 ],[],
	 [support=yes],[support=no])
      CXXFLAGS="$old_CXXFLAGS"
      AC_LANG_RESTORE

   AC_MSG_RESULT($support)
   if test x$support = xyes; then
      CXXFLAGS="$CXXFLAGS $flags"
   fi
])

dnl LAV: AC_SYS_LARGEFILE is taken from sh-utils-2.0.

dnl By default, many hosts won't let programs access large files;
dnl one must use special compiler options to get large-file access to work.
dnl For more details about this brain damage please see:
dnl http://www.sas.com/standards/large.file/x_open.20Mar96.html

dnl Written by Paul Eggert <eggert@twinsun.com>.

dnl Internal subroutine of AC_SYS_LARGEFILE.
dnl AC_SYS_LARGEFILE_FLAGS(FLAGSNAME)
AC_DEFUN(AC_SYS_LARGEFILE_FLAGS,
  [AC_CACHE_CHECK([for $1 value to request large file support],
     ac_cv_sys_largefile_$1,
     [ac_cv_sys_largefile_$1=`($GETCONF LFS_$1) 2>/dev/null` || {
	ac_cv_sys_largefile_$1=no
	ifelse($1, CFLAGS,
	  [case "$host_os" in
	   # IRIX 6.2 and later require cc -n32.
changequote(, )dnl
	   irix6.[2-9]* | irix6.1[0-9]* | irix[7-9].* | irix[1-9][0-9]*)
changequote([, ])dnl
	     if test "$GCC" != yes; then
	       ac_cv_sys_largefile_CFLAGS=-n32
	     fi
	     ac_save_CC="$CC"
	     CC="$CC $ac_cv_sys_largefile_CFLAGS"
	     AC_TRY_LINK(, , , ac_cv_sys_largefile_CFLAGS=no)
	     CC="$ac_save_CC"
	   esac])
      }])])

dnl Internal subroutine of AC_SYS_LARGEFILE.
dnl AC_SYS_LARGEFILE_SPACE_APPEND(VAR, VAL)
AC_DEFUN(AC_SYS_LARGEFILE_SPACE_APPEND,
  [case $2 in
   no) ;;
   ?*)
     case "[$]$1" in
     '') $1=$2 ;;
     *) $1=[$]$1' '$2 ;;
     esac ;;
   esac])

dnl Internal subroutine of AC_SYS_LARGEFILE.
dnl AC_SYS_LARGEFILE_MACRO_VALUE(C-MACRO, CACHE-VAR, COMMENT, CODE-TO-SET-DEFAULT)
AC_DEFUN(AC_SYS_LARGEFILE_MACRO_VALUE,
  [AC_CACHE_CHECK([for $1], $2,
     [$2=no
changequote(, )dnl
      $4
      for ac_flag in $ac_cv_sys_largefile_CFLAGS no; do
	case "$ac_flag" in
	-D$1)
	  $2=1 ;;
	-D$1=*)
	  $2=`expr " $ac_flag" : '[^=]*=\(.*\)'` ;;
	esac
      done
changequote([, ])dnl
      ])
   if test "[$]$2" != no; then
     AC_DEFINE_UNQUOTED([$1], [$]$2, [$3])
   fi])

AC_DEFUN(AC_SYS_LARGEFILE,
  [AC_REQUIRE([AC_CANONICAL_HOST])
   AC_ARG_ENABLE(largefile,
     [  --disable-largefile     omit support for large files])
   if test "$enable_largefile" != no; then
     AC_CHECK_TOOL(GETCONF, getconf)
     AC_SYS_LARGEFILE_FLAGS(CFLAGS)
     AC_SYS_LARGEFILE_FLAGS(LDFLAGS)
     AC_SYS_LARGEFILE_FLAGS(LIBS)

     for ac_flag in $ac_cv_sys_largefile_CFLAGS no; do
       case "$ac_flag" in
       no) ;;
       -D_FILE_OFFSET_BITS=*) ;;
       -D_LARGEFILE_SOURCE | -D_LARGEFILE_SOURCE=*) ;;
       -D_LARGE_FILES | -D_LARGE_FILES=*) ;;
       -D?* | -I?*)
	 AC_SYS_LARGEFILE_SPACE_APPEND(CPPFLAGS, "$ac_flag") ;;
       *)
	 AC_SYS_LARGEFILE_SPACE_APPEND(CFLAGS, "$ac_flag")
	 AC_SYS_LARGEFILE_SPACE_APPEND(CXXFLAGS, "$ac_flag") ;;
       esac
     done
     AC_SYS_LARGEFILE_SPACE_APPEND(LDFLAGS, "$ac_cv_sys_largefile_LDFLAGS")
     AC_SYS_LARGEFILE_SPACE_APPEND(LIBS, "$ac_cv_sys_largefile_LIBS")
     AC_SYS_LARGEFILE_MACRO_VALUE(_FILE_OFFSET_BITS,
       ac_cv_sys_file_offset_bits,
       [Number of bits in a file offset, on hosts where this is settable.]
       [case "$host_os" in
	# HP-UX 10.20 and later
	hpux10.[2-9][0-9]* | hpux1[1-9]* | hpux[2-9][0-9]*)
	  ac_cv_sys_file_offset_bits=64 ;;
	esac])
     AC_SYS_LARGEFILE_MACRO_VALUE(_LARGEFILE_SOURCE,
       ac_cv_sys_largefile_source,
       [Define to make fseeko etc. visible, on some hosts.],
       [case "$host_os" in
	# HP-UX 10.20 and later
	hpux10.[2-9][0-9]* | hpux1[1-9]* | hpux[2-9][0-9]*)
	  ac_cv_sys_largefile_source=1 ;;
	esac])
     AC_SYS_LARGEFILE_MACRO_VALUE(_LARGE_FILES,
       ac_cv_sys_large_files,
       [Define for large files, on AIX-style hosts.],
       [case "$host_os" in
	# AIX 4.2 and later
	aix4.[2-9]* | aix4.1[0-9]* | aix[5-9].* | aix[1-9][0-9]*)
	  ac_cv_sys_large_files=1 ;;
	esac])
   fi
  ])
