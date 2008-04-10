# include_next.m4 serial 1
dnl Copyright (C) 2006 Free Software Foundation, Inc.
dnl This file is free software; the Free Software Foundation
dnl gives unlimited permission to copy and/or distribute it,
dnl with or without modifications, as long as this notice is preserved.

dnl From Derek Price.

AC_DEFUN([gl_INCLUDE_NEXT],
[AC_CACHE_CHECK([whether the preprocessor supports include_next],
[gl_cv_have_include_next],
[AC_PREPROC_IFELSE([#include_next <stddef.h>],
                   [gl_cv_have_include_next=yes],
                   [gl_cv_have_include_next=no])])
if test $gl_cv_have_include_next = yes; then
  AC_DEFINE([HAVE_INCLUDE_NEXT], 1,
            [Define if your compiler supports the #include_next directive.])
fi
])
