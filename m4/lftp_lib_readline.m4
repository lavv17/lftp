# SYNOPSIS
#
#   lftp_LIB_READLINE([MINIMUM-VERSION])
#
# DESCRIPTION
#
#   This macro provides tests of availability of Readline of
#   particular version or newer. This macro checks for Readline
#   headers and libraries and defines compilation flags
#
#   Macro supports following options and their values:
#
#   1) Single-option usage:
#
#     --with-readline      -- yes, no, or path to Readline
#                          installation prefix
#
#   2) Three-options usage (all options are required):
#
#     --with-readline=yes
#     --with-readline-inc  -- path to base directory with Readline headers
#     --with-readline-lib  -- linker flags for Readline
#
#   This macro calls:
#
#     AC_SUBST(READLINE_CFLAGS)
#     AC_SUBST(READLINE_LIBS)
#     AC_SUBST(READLINE_LDFLAGS)
#     AC_SUBST(READLINE_VERSION)  -- only if version requirement is used
#
#   And sets:
#
#     HAVE_READLINE
#
# LICENSE
#
#   Copyright (c) 2008 Mateusz Loskot <mateusz@loskot.net>
#   Copyright (c) 2015 Alexander V. Lukyanov <lavv17f@gmail.com>
#
#   Copying and distribution of this file, with or without modification, are
#   permitted in any medium without royalty provided the copyright notice
#   and this notice are preserved. This file is offered as-is, without any
#   warranty.

AC_DEFUN([lftp_LIB_READLINE],
[
    AC_ARG_WITH([readline],
        AS_HELP_STRING([--with-readline=@<:@ARG@:>@],
            [use Readline from given prefix (ARG=path); check standard prefixes (ARG=yes); disable (ARG=no)]
        ),
        [
        if test "$withval" = "yes"; then
            if test -f /usr/local/include/readline/readline.h; then
                readline_prefix=/usr/local
            elif test -f /usr/include/readline/readline.h; then
                readline_prefix=/usr
            else
                readline_prefix=""
            fi
            readline_requested="yes"
        elif test -d "$withval"; then
            readline_prefix="$withval"
            readline_requested="yes"
        else
            readline_prefix=""
            readline_requested="no"
        fi
        ],
        [
        dnl Default behavior is implicit yes
        if test -f /usr/local/include/readline/readline.h; then
            readline_prefix=/usr/local
        elif test -f /usr/include/readline/readline.h; then
            readline_prefix=/usr
        else
            readline_prefix=""
        fi
        ]
    )

    AC_ARG_WITH([readline-inc],
        AS_HELP_STRING([--with-readline-inc=@<:@DIR@:>@],
            [path to Readline headers]
        ),
        [readline_include_dir="$withval"],
        [readline_include_dir=""]
    )
    AC_ARG_WITH([readline-lib],
        AS_HELP_STRING([--with-readline-lib=@<:@ARG@:>@],
            [link options for Readline libraries]
        ),
        [readline_lib_flags="$withval"],
        [readline_lib_flags=""]
    )

    READLINE_CFLAGS=""
    READLINE_LIBS=""
    READLINE_VERSION=""

    dnl
    dnl Collect include/lib paths and flags
    dnl
    run_readline_test="no"

    if test -n "$readline_prefix"; then
        readline_include_dir="$readline_prefix/include"
	if test -f "$readline_include_dir/readline/readline.h"; then
	    readline_include_dir="$readline_include_dir/readline"
	fi
        readline_ld_flags="-L$readline_prefix/lib"
        if test -z "$readline_lib_flags"; then
            readline_lib_flags="-lreadline"
        fi
        run_readline_test="yes"
    elif test "$readline_requested" = "yes"; then
        if test -n "$readline_include_dir" -a -n "$readline_lib_flags"; then
	    if test -f "$readline_include_dir/readline/readline.h"; then
		readline_include_dir="$readline_include_dir/readline"
	    fi
            run_readline_test="yes"
        fi
    else
        run_readline_test="no"
    fi

    dnl
    dnl Check Readline files
    dnl
    if test "$run_readline_test" = "yes"; then

        saved_CPPFLAGS="$CPPFLAGS"
        CPPFLAGS="$CPPFLAGS -I$readline_include_dir"

        saved_LIBS="$LIBS"
        LIBS="$LIBS $readline_lib_flags"

        saved_LDFLAGS="$LDFLAGS"
        LDFLAGS="$LDFLAGS $readline_ld_flags"

        dnl
        dnl Check Readline headers
        dnl
        AC_MSG_CHECKING([for Readline headers in $readline_include_dir])

        AC_LANG_PUSH([C++])
        AC_COMPILE_IFELSE([
            AC_LANG_PROGRAM(
                [[
@%:@include <stdio.h>
@%:@include <readline/readline.h>
                ]],
                [[]]
            )],
            [
            READLINE_CFLAGS="-I$readline_include_dir"
            readline_header_found="yes"
            AC_MSG_RESULT([found])
            ],
            [
            readline_header_found="no"
            AC_MSG_RESULT([not found])
            ]
        )
        AC_LANG_POP([C++])

        dnl
        dnl Check Readline libraries
        dnl
        if test "$readline_header_found" = "yes"; then

            AC_MSG_CHECKING([for Readline libraries])

            AC_LANG_PUSH([C++])
            AC_LINK_IFELSE([
                AC_LANG_PROGRAM(
                    [[
@%:@include <stdio.h>
@%:@include <readline/readline.h>
                    ]],
                    [[
rl_getc_function=0;
rl_completion_matches(0,0);
		    ]]
                )],
                [
                READLINE_LIBS="$readline_lib_flags"
		READLINE_LDFLAGS="$readline_ld_flags"
		test "$enable_rpath" = yes -a "$readline_prefix" != /usr && \
		    READLINE_LDFLAGS="$READLINE_LDFLAGS -R$readline_prefix/lib"
                readline_lib_found="yes"
                AC_MSG_RESULT([found])
                ],
                [
                readline_lib_found="no"
                AC_MSG_RESULT([not found])
                ]
            )
            AC_LANG_POP([C++])
        fi

        CPPFLAGS="$saved_CPPFLAGS"
        LDFLAGS="$saved_LDFLAGS"
        LIBS="$saved_LIBS"
    fi

    AC_MSG_CHECKING([for Readline])

    if test "$run_readline_test" = "yes"; then
        if test "$readline_header_found" = "yes" -a "$readline_lib_found" = "yes"; then

            AC_SUBST([READLINE_CFLAGS])
            AC_SUBST([READLINE_LDFLAGS])
            AC_SUBST([READLINE_LIBS])

            HAVE_READLINE="yes"
        else
            HAVE_READLINE="no"
        fi

        AC_MSG_RESULT([$HAVE_READLINE])

        dnl
        dnl Check Readline version
        dnl
        if test "$HAVE_READLINE" = "yes"; then

            readline_version_req=ifelse([$1], [], [], [$1])

            if test  -n "$readline_version_req"; then

                AC_MSG_CHECKING([if Readline version is >= $readline_version_req])

                if test -f "$readline_include_dir/readline.h"; then

                    readline_major=`cat $readline_include_dir/readline.h | \
                                    grep '^#define.*RL_VERSION_MAJOR.*[0-9]$' | \
                                    sed -e 's/#define RL_VERSION_MAJOR.//'`

                    readline_minor=`cat $readline_include_dir/readline.h | \
                                    grep '^#define.*RL_VERSION_MINOR.*[0-9]$' | \
                                    sed -e 's/#define RL_VERSION_MINOR.//'`

                    readline_revision=0

                    READLINE_VERSION="$readline_major.$readline_minor"
                    AC_SUBST([READLINE_VERSION])

                    dnl Decompose required version string and calculate numerical representation
                    readline_version_req_major=`expr $readline_version_req : '\([[0-9]]*\)'`
                    readline_version_req_minor=`expr $readline_version_req : '[[0-9]]*\.\([[0-9]]*\)'`
                    readline_version_req_revision=`expr $readline_version_req : '[[0-9]]*\.[[0-9]]*\.\([[0-9]]*\)'`
                    if test "x$readline_version_req_revision" = "x"; then
                        readline_version_req_revision="0"
                    fi

                    readline_version_req_number=`expr $readline_version_req_major \* 10000 \
                                               \+ $readline_version_req_minor \* 100 \
                                               \+ $readline_version_req_revision`

                    dnl Calculate numerical representation of detected version
                    readline_version_number=`expr $readline_major \* 10000 \
                                          \+ $readline_minor \* 100 \
                                           \+ $readline_revision`

                    readline_version_check=`expr $readline_version_number \>\= $readline_version_req_number`
                    if test "$readline_version_check" = "1"; then
                        AC_MSG_RESULT([yes])
                    else
                        AC_MSG_RESULT([no])
                        AC_MSG_WARN([Found Readline $READLINE_VERSION, which is older than required. Possible compilation failure.])
                    fi
                else
                    AC_MSG_RESULT([no])
                    AC_MSG_WARN([Missing readline.h header. Unable to determine Readline version.])
                fi
            fi
        fi

    else
        HAVE_READLINE="no"
        AC_MSG_RESULT([$HAVE_READLINE])

        if test "$readline_requested" = "yes"; then
            AC_MSG_WARN([Readline support requested but headers or library not found. Specify valid prefix of Readline using --with-readline=@<:@DIR@:>@ or provide include directory and linker flags using --with-readline-inc and --with-readline-lib])
        fi
    fi
])
