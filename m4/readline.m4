dnl Check for libreadline of proper version
AC_DEFUN(READLINE_CHECK,
[AC_MSG_CHECKING(for readline)
AC_CACHE_VAL(lftp_cv_precompiled_readline,
[
   old_LIBS="$LIBS"
   LIBS="-lreadline $READLINE_SUPPLIB $LIBS"
   AC_TRY_LINK([extern int (*rl_getc_function)();],
      [rl_getc_function=0;
       rl_completion_matches(0,0);],
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
   READLINE_DIR=readline-4.2
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
