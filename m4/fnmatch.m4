AC_DEFUN([LFTP_GNU_FNMATCH],[
   AC_MSG_CHECKING(for gnu fnmatch)
   AC_CACHE_VAL(lftp_cv_gnu_fnmatch,
   [
      lftp_cv_gnu_fnmatch=no
      AC_TRY_COMPILE([
	 #include <fnmatch.h>
      ],
      [
	 fnmatch("x","y",FNM_CASEFOLD);
      ],
      [
	 lftp_cv_gnu_fnmatch=yes
      ])
   ])
   AC_MSG_RESULT($lftp_cv_gnu_fnmatch)
])
