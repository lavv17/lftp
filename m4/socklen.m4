dnl Check for size of addr length argument
AC_DEFUN(TYPE_SOCKLEN_T,
[
   AC_MSG_CHECKING(for socklen_t)
   AC_CACHE_VAL(lftp_cv_socklen_t,
   [
      AC_LANG_PUSH(C++)
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
      AC_LANG_POP(C++)
   ])
   AC_MSG_RESULT($lftp_cv_socklen_t)
   if test $lftp_cv_socklen_t = no; then
      AC_MSG_CHECKING(for socklen_t equivalent)
      AC_CACHE_VAL(lftp_cv_socklen_t_equiv,
      [
	 lftp_cv_socklen_t_equiv=int
	 AC_LANG_PUSH(C++)
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
	 AC_LANG_POP(C++)
      ])
      AC_MSG_RESULT($lftp_cv_socklen_t_equiv)
      AC_DEFINE_UNQUOTED(socklen_t, $lftp_cv_socklen_t_equiv,
                        [type to use in place of socklen_t if not defined])
   fi
])
