dnl -*-autoconf-*-

AC_DEFUN([LFTP_PROG_CXXLINK],
[
   AC_MSG_CHECKING(how to link simple c++ programs)
   if test "$GCC" = yes -a "$GXX" = yes; then
      old_CXX="$CXX"
      CXX="$CC"
      AC_LANG_PUSH(C++)
      AC_TRY_LINK([],[char *a=new char[10];delete[] a;],
	 [],[
	 old_LIBS="$LIBS"
	 LIBS="-lsupc++ $LIBS"
	 AC_TRY_LINK([],[char *a=new char[10];delete[] a;],[],[LIBS="$old_LIBS"; CXX="$old_CXX";])
	 ])
      AC_LANG_POP(C++)
   fi
   AC_MSG_RESULT(using $CXX)
])
dnl try to build and run a dummy program
AC_DEFUN([LFTP_CXX_TEST],
[
   AC_LANG_PUSH(C++)
   AC_MSG_CHECKING(if c++ compiler works)
   AC_TRY_RUN([int main() { return(0); } ],
           [AC_MSG_RESULT(yes)], [
	   AC_MSG_RESULT(no)
	   AC_MSG_ERROR(C++ test compile failed; check your C++ compiler)], [AC_MSG_RESULT(cross-compiling)])
   AC_LANG_POP(C++)
])

AC_DEFUN([LFTP_FUNC_SSCANF_CONST],
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
      AC_MSG_WARN(sscanf does not work on const strings)
   fi
])

dnl Do nothing if the compiler accepts the inline keyword.
dnl Otherwise define c_inline to __inline__ or __inline if one of those work,
dnl otherwise define c_inline to be empty.
AC_DEFUN([LFTP_C_INLINE],
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

AC_DEFUN([LFTP_NOIMPLEMENTINLINE],
[
   AC_MSG_CHECKING(if -fno-implement-inlines implements virtual functions)
   flags="-fno-implement-inlines"
   AC_CACHE_VAL(lftp_cv_noimplementinline,
   [
      AC_LANG_PUSH(C++)
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
      AC_LANG_POP(C++)
   ])
   AC_MSG_RESULT($lftp_cv_noimplementinline)
   if test x$lftp_cv_noimplementinline = xyes; then
      CXXFLAGS="$CXXFLAGS $flags"
   fi
])
AC_DEFUN([LFTP_CHECK_CXX_FLAGS],
[
   flags="$1"
   AC_MSG_CHECKING(if $CXX supports $flags)

      AC_LANG_PUSH(C++)
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
      AC_LANG_POP(C++)

   AC_MSG_RESULT($support)
   if test x$support = xyes; then
      CXXFLAGS="$CXXFLAGS $flags"
   fi
])
AC_DEFUN([LFTP_CHECK_LIBM],
[
   AC_MSG_CHECKING(if math library is needed)
   AC_CACHE_VAL(lftp_cv_libm_needed,
   [
      AC_LANG_PUSH(C++)
      AC_TRY_LINK([
	    #include <math.h>
	    double a,b;
	 ],[
	    return int(exp(a)+log(b)+pow(a,b));
	 ],
	 [lftp_cv_libm_needed=no],
	 [lftp_cv_libm_needed=yes])
      AC_LANG_POP(C++)
   ])
   AC_MSG_RESULT($lftp_cv_libm_needed)
   if test x$lftp_cv_libm_needed = xyes; then
      AC_SEARCH_LIBS(exp,m)
   fi
])
dnl try to build and run a dummy program
AC_DEFUN([LFTP_CXX_BOOL],
[
   AC_MSG_CHECKING(whether $CXX supports bool type)
   AC_CACHE_VAL(lftp_cv_cxx_bool,
   [
      AC_LANG_PUSH(C++)
      AC_TRY_COMPILE([bool t=true;bool f=false;],[],
	 [lftp_cv_cxx_bool=yes],[lftp_cv_cxx_bool=no])
      AC_LANG_POP(C++)
   ])
   AC_MSG_RESULT($lftp_cv_cxx_bool)
   if test x$lftp_cv_cxx_bool = xyes; then
      AC_DEFINE(HAVE_CXX_BOOL, 1, [define if c++ compiler supports bool])
   fi
   AH_VERBATIM([OPT_CPP_BOOL], [
#if defined(__cplusplus) && !defined(HAVE_CXX_BOOL)
   typedef unsigned _bool;
#define bool   _bool
#define false  0U
#define true   1U
#endif ])
])

dnl try to build and run a dummy program
AC_DEFUN([LFTP_CXX__BOOL],
[
   AC_MSG_CHECKING(whether $CXX supports _Bool type)
   AC_CACHE_VAL(lftp_cv_cxx__Bool,
   [
      AC_LANG_PUSH(C++)
      AC_TRY_COMPILE([_Bool t=true;bool f=false;],[],
	 [lftp_cv_cxx__Bool=yes],[lftp_cv_cxx__Bool=no])
      AC_LANG_POP(C++)
   ])
   AC_MSG_RESULT($lftp_cv_cxx__Bool)
   if test x$lftp_cv_cxx__Bool = xyes; then
      AC_DEFINE(HAVE_CXX__BOOL, 1, [define if c++ compiler supports _Bool])
   fi
   AH_VERBATIM([OPT_CPP__BOOL], [
#if defined(__cplusplus) && !defined(HAVE_CXX__BOOL)
   typedef bool _Bool;
#endif ])
])

dnl check if C++ compiler needs extra arguments to grok ANSI scoping rules
AC_DEFUN([LFTP_CXX_ANSI_SCOPE],
[
   AC_MSG_CHECKING([whether $CXX understands ANSI scoping rules])
   AC_CACHE_VAL(lftp_cv_cxx_ansi_scope,
   [
      AC_LANG_PUSH(C++)
      AC_TRY_COMPILE(,[
  for (int i = 0; i < 4; i++) ;
  for (int i = 0; i < 4; i++) ;],
      [lftp_cv_cxx_ansi_scope=yes],
      [
         # IRIX C++ needs -LANG:ansi-for-init-scope=ON if
         # -LANG:std not used
         _cxxflags=$CXXFLAGS
         CXXFLAGS="$CXXFLAGS -LANG:ansi-for-init-scope=ON"
         AC_TRY_COMPILE(,[
  for (int i = 0; i < 4; i++) ;
  for (int i = 0; i < 4; i++) ;],
         [lftp_cv_cxx_ansi_scope="-LANG:ansi-for-init-scope=ON"],
         [lftp_cv_cxx_ansi_scope=no])
         CXXFLAGS=$_cxxflags
      ])
      AC_LANG_POP(C++)
   ])
   if test x$lftp_cv_cxx_ansi_scope = xno; then
      AC_MSG_RESULT([$lftp_cv_cxx_ansi_scope])
      AC_MSG_ERROR([C++ compiler does not understand ANSI scoping rules])
   elif test x$lftp_cv_cxx_ansi_scope != xyes; then
      AC_MSG_RESULT([$lftp_cv_cxx_ansi_scope added to \$CXXFLAGS])
      CXXFLAGS="$CXXFLAGS $lftp_cv_cxx_ansi_scope"
   else
     AC_MSG_RESULT($lftp_cv_cxx_ansi_scope)
   fi
])

AC_DEFUN([LFTP_ENVIRON_CHECK],[
   AC_CACHE_CHECK([for environ variable],[lftp_cv_environ],[
      AC_TRY_LINK([
	 #include <unistd.h>
	 extern char **environ;
      ],[
	 return environ==(char**)0;
      ],[lftp_cv_environ=yes],[lftp_cv_environ=no])
   ])
   if test x$lftp_cv_environ = xyes; then
      AC_DEFINE(HAVE_ENVIRON, 1, [define if you have global environ variable])
   fi
])

dnl Taken from dovecot
AC_DEFUN([LFTP_POSIX_FALLOCATE_CHECK],[
   dnl * Old glibcs have broken posix_fallocate(). Make sure not to use it.
   dnl * It may also be broken in AIX.
   AC_CACHE_CHECK([whether posix_fallocate() works],[i_cv_posix_fallocate_works],[
     AC_TRY_RUN([
       #define _XOPEN_SOURCE 600
       #include <stdio.h>
       #include <stdlib.h>
       #include <fcntl.h>
       #include <unistd.h>
       #if defined(__GLIBC__) && (__GLIBC__ < 2 || __GLIBC_MINOR__ < 7)
	 possibly broken posix_fallocate
       #endif
       int main() {
	 int fd = creat("conftest.temp", 0600);
	 int ret;
	 if (fd == -1) {
      perror("creat()");
      return 2;
	 }
	 ret = posix_fallocate(fd, 1024, 1024) < 0 ? 1 : 0;
	 unlink("conftest.temp");
	 return ret;
       }
     ], [
       i_cv_posix_fallocate_works=yes
     ], [
       i_cv_posix_fallocate_works=no
     ])
   ])
   if test x$i_cv_posix_fallocate_works = xyes; then
     AC_DEFINE(HAVE_POSIX_FALLOCATE, 1, [Define if you have a working posix_fallocate()])
   fi
])

AC_DEFUN([LFTP_POSIX_FADVISE_CHECK],[
   AC_CACHE_CHECK([for posix_fadvise], [ac_cv_posix_fadvise], [AC_TRY_LINK([
   #define _XOPEN_SOURCE 600
   #include <stdio.h>
   #include <stdlib.h>
   #include <fcntl.h>
   #include <unistd.h>
   ],[
      int res = posix_fadvise ((int)0, (off_t)0, (off_t)0, POSIX_FADV_NORMAL);
      int a = POSIX_FADV_SEQUENTIAL;
      int b = POSIX_FADV_NOREUSE;
      int c = POSIX_FADV_RANDOM;
      int d = POSIX_FADV_WILLNEED;
      int e = POSIX_FADV_DONTNEED;
   ],[ac_cv_posix_fadvise=yes],[ac_cv_posix_fadvise=no])])
   if test x$ac_cv_posix_fadvise = xyes; then
      AC_DEFINE(HAVE_POSIX_FADVISE, 1, [Define if posix_fadvise() is available])
   fi
])
