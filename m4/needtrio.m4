AC_DEFUN([LFTP_NEED_TRIO],[
   AC_CHECK_FUNCS(vsnprintf snprintf)
   AC_CACHE_CHECK([if trio library is needed], ac_cv_need_trio,
   [
      ac_cv_need_trio=no;

      if test x$ac_cv_func_vsnprintf != xyes -o x$ac_cv_func_snprintf != xyes; then
	 ac_cv_need_trio="yes (because there is no system snprintf/vsnprintf functions)"
      else

      AC_RUN_IFELSE([AC_LANG_SOURCE([[
	 int main()
	 {
	    unsigned long long x=0,x1;
	    long long y=0,y1;
	    char buf[128];

	    x=~x;
	    sscanf("0","%llu",&x);
	    if(x!=0) return 1;

	    y=~y;
	    sscanf("0","%lld",&y);
	    if(y!=0) return 1;

	    x=~x;
	    sprintf(buf,"%lld %llu",y,x);
	    sscanf (buf,"%lld %llu",&y1,&x1);
	    if(x!=x1 || y!=y1)
	       return 1;

	    return 0;
	 }]])],[],[ac_cv_need_trio="yes (because %lld fails)"],[ac_cv_need_trio="no (assumed)"])

      fi
   ])
   case $ac_cv_need_trio in
   yes*)
      ac_cv_need_trio=yes
      AC_DEFINE(TRIO_REPLACE_STDIO, 1, [replace system's printf functions])
      LFTP_CHECK_LIBM
      ;;
   *)
      ac_cv_need_trio=no
      ;;
   esac
   AM_CONDITIONAL(NEED_TRIO, [test "$ac_cv_need_trio" = yes])
])
