dnl Check for libreadline of proper version
AC_DEFUN([READLINE_CHECK],
[AC_MSG_CHECKING(for readline)
dnl AC_ARG_WITH(included-readline,
dnl    [  --with-included-readline  use supplied readline instead of system one],
dnl    [with_included_readline=$withval],[with_included_readline=auto])
case "$with_included_readline" in
yes) lftp_cv_precompiled_readline=no;;
no)  lftp_cv_precompiled_readline=yes;;
*)
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
   ;;
esac
if test $lftp_cv_precompiled_readline = yes; then
   READLINE_DIR=''
   AC_MSG_RESULT(yes)
   READLINE='-lreadline'
   READLINE_DEPEND=''
   COMPILE_READLINE=no
else
   READLINE_DIR=readline-4.3
   if ! test -d $READLINE_DIR; then
      AC_MSG_ERROR(need installed readline-devel package)
   fi
dnl   AC_CONFIG_SUBDIRS(readline-4.3)
   AC_MSG_RESULT(no - will compile)
   AC_CONFIG_LINKS([include/readline:$READLINE_DIR])
   READLINE='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   READLINE_DEPEND='$(top_builddir)/$(READLINE_DIR)/libreadline.a'
   COMPILE_READLINE=yes
fi
AC_SUBST(COMPILE_READLINE)
AC_SUBST(READLINE_DIR)
AC_SUBST(READLINE_DEPEND)
AC_SUBST(READLINE)
])
