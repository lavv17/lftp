# If you use human.c, you need the following files:
# uintmax_t.m4 inttypes_h.m4 ulonglong.m4
AC_DEFUN(jm_PREREQ_HUMAN,
[
  AC_CHECK_HEADERS(limits.h stdlib.h string.h)
  AC_CHECK_DECLS([getenv])
  AC_REQUIRE([jm_AC_HEADER_INTTYPES_H])
  AC_REQUIRE([jm_AC_TYPE_UINTMAX_T])
])

