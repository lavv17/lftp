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
		  return(!(poll(0,0,0)==0 && poll(&pfd,1,0)==1));
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
      AC_TRY_LINK([],[char *a=new char[10];delete[] a;],
	 [],[CXX="$old_CXX";])
      AC_LANG_RESTORE
   fi
   AC_MSG_RESULT(using $CXX)
])
dnl try to build and run a dummy program
AC_DEFUN(LFTP_CXX_TEST,
[
   AC_LANG_SAVE
   AC_LANG_CPLUSPLUS
   AC_MSG_CHECKING(if c++ compiler works)
   AC_TRY_RUN([int main() { return(0); } ],
           [AC_MSG_RESULT(yes)], [
	   AC_MSG_RESULT(no)
	   AC_MSG_ERROR(C++ test compile failed; check your C++ compiler)], [AC_MSG_RESULT(cross-compiling)])
   AC_LANG_RESTORE
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
AC_DEFUN(LFTP_CHECK_LIBM,
[
   AC_MSG_CHECKING(if math library is needed)
   AC_CACHE_VAL(lftp_cv_libm_needed,
   [
      AC_LANG_SAVE
      AC_LANG_CPLUSPLUS
      AC_TRY_LINK([
	    #include <math.h>
	    double a,b;
	 ],[
	    return int(exp(a)+log(b)+pow(a,b));
	 ],
	 [lftp_cv_libm_needed=no],
	 [lftp_cv_libm_needed=yes])
      AC_LANG_RESTORE
   ])
   AC_MSG_RESULT($lftp_cv_libm_needed)
   if test x$lftp_cv_libm_needed = xyes; then
      AC_SEARCH_LIBS(exp,m)
   fi
])
