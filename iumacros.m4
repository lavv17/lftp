dnl Autoconf macros used by inetutils
dnl
dnl Copyright (C) 1996, 1997 Free Software Foundation, Inc.
dnl
dnl Written by Miles Bader <miles@gnu.ai.mit.edu>
dnl
dnl This program is free software; you can redistribute it and/or modify
dnl it under the terms of the GNU General Public License as published by
dnl the Free Software Foundation; either version 2, or (at your option)
dnl any later version.
dnl
dnl This program is distributed in the hope that it will be useful,
dnl but WITHOUT ANY WARRANTY; without even the implied warranty of
dnl MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
dnl GNU General Public License for more details.
dnl
dnl You should have received a copy of the GNU General Public License
dnl along with this program; if not, write to the Free Software
dnl Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
dnl

# For case-conversion with sed
IU_UCASE=ABCDEFGHIJKLMNOPQRSTUVWXYZ
iu_lcase=abcdefghijklmnopqrstuvwxyz

dnl IU_FLUSHLEFT -- remove all whitespace at the beginning of lines
dnl This is useful for c-code which may include cpp statements
dnl
define([IU_FLUSHLEFT],
 [changequote(`,')dnl
patsubst(`$1', `^[ 	]+')
changequote([,])])dnl

dnl IU_RESULT_ACTIONS -- generate shell code for the result of a test
dnl   $1 -- CVAR  -- cache variable to check
dnl   $2 -- NAME  -- if not empty, used to generate a default value TRUE:
dnl                  `AC_DEFINE(HAVE_NAME)'
dnl   $2 -- TRUE  -- what to do if the CVAR is `yes'
dnl   $3 -- FALSE -- what to do otherwise; defaults to `:'
dnl
AC_DEFUN([IU_RESULT_ACTIONS], [
[if test "$$1" = yes; then
  ]ifelse([$3], ,
          [AC_DEFINE(HAVE_]translit($2, [a-z ./<>], [A-Z___])[)],
          [$3])[
else
  ]ifelse([$4], , [:], [$4])[
fi]])dnl

dnl IU_CHECK_DEFINE -- Check for cpp defines
dnl   $1 - NAME   -- printed in message
dnl   $2 - INCLS  -- C program text to inculde necessary files for testing
dnl   $3 - MACROS -- a space-separated list of macros that all must be defined
dnl		     defaults to NAME
dnl   $4 - TRUE	  -- what to do if all macros are defined; defaults to 
dnl		     AC_DEFINE(upcase(HAVE_$1))
dnl   $5 - FALSE  -- what to do if some macros aren't defined
dnl
AC_DEFUN([IU_CHECK_MACRO], [
  define([IU_CVAR], [inetutils_cv_macro_]translit($1, [A-Z ./<>], [a-z___]))dnl
  define([IU_TAG], [IU_CHECK_MACRO_]translit($1, [a-z ./<>], [A-Z___]))dnl
  AC_CACHE_CHECK([for $1], IU_CVAR,
    AC_EGREP_CPP(IU_TAG,
      IU_FLUSHLEFT(
[$2
#if ]dnl
changequote(<<,>>)dnl
patsubst(patsubst(ifelse(<<$3>>, , <<$1>>, <<$3>>),
	          <<\>[ ,]+\<>>, << && >>),
         <<\w+>>, <<defined(\&)>>) dnl
changequote([,])dnl
[
]IU_TAG[
#endif]),
      IU_CVAR[=yes],
      IU_CVAR[=no])) dnl
  IU_RESULT_ACTIONS(IU_CVAR, [$1], [$4], [$5]) dnl
  undefine([IU_CVAR]) undefine([IU_TAG])])dnl

dnl IU_CHECK_DECL -- See if a function/variable is declared
dnl   $1 - NAME  -- name of c symbol to check
dnl   $2 - INCLS -- C program text to inculde necessary files for testing
dnl   $3 - TRUE  -- what to do if NAME is declared; defaults to 
dnl		    `AC_DEFINE(HAVE_NAME_DECL)'
dnl   $4 - FALSE -- what to do if NAME isn't declared; defaults to `:'
dnl
AC_DEFUN([IU_CHECK_DECL], [
  define([IU_CVAR], [inetutils_cv_decl_]translit($1, [A-Z], [a-z]))dnl
  AC_CACHE_CHECK([whether $1 is declared], IU_CVAR,
    AC_TRY_COMPILE(IU_FLUSHLEFT([$2]), [char *iu_x = (char *)$1],
      IU_CVAR[=yes], IU_CVAR[=no]))
  IU_RESULT_ACTIONS(IU_CVAR, [$1_DECL], [$3], [$4])dnl
  undefine([IU_CVAR])])

dnl IU_CHECK_TYPE -- See if a typedef is defined
dnl   $1 - TYPE  -- name of c type to check
dnl   $2 - INCLS -- C program text to inculde necessary files for testing
dnl   $3 - TRUE  -- what to do if TYPE is defined; defaults to 
dnl		    `AC_DEFINE(HAVE_TYPE)'
dnl   $4 - FALSE -- what to do if TYPE isn't defined; defaults to `:'
dnl
AC_DEFUN([IU_CHECK_TYPE], [
  define([IU_CVAR], [inetutils_cv_type_]translit($1, [A-Z ], [a-z_]))dnl
  AC_CACHE_CHECK([whether $1 is defined], IU_CVAR,
    AC_TRY_COMPILE(IU_FLUSHLEFT([$2]), [$1 iu_x;],
      IU_CVAR[=yes], IU_CVAR[=no]))
  IU_RESULT_ACTIONS(IU_CVAR, [$1], [$3], [$4])dnl
  undefine([IU_CVAR])])

dnl IU_CHECK_STRUCT_FIELD -- See if a structure has a particular field
dnl   $1 - NAME  -- name of structure
dnl   $2 - FIELD -- name of field to test
dnl   $3 - INCLS -- C program text to inculde necessary files for testing
dnl   $4 - TRUE  -- what to do if struct NAME has FIELD; defaults to 
dnl		    `AC_DEFINE(HAVE_NAME_FIELD)'
dnl   $5 - FALSE -- what to do if not; defaults to `:'
dnl
AC_DEFUN([IU_CHECK_STRUCT_FIELD], [
  define([IU_CVAR], [inetutils_cv_struct_]translit($1_$2, [A-Z], [a-z]))dnl
  AC_CACHE_CHECK([whether struct $1 has $2 field], IU_CVAR,
    AC_TRY_COMPILE(IU_FLUSHLEFT([$3]),
      [struct $1 iu_x; int iu_y = sizeof iu_x.$2;],
      IU_CVAR[=yes], IU_CVAR[=no]))
  IU_RESULT_ACTIONS(IU_CVAR, [$1_$2], [$4], [$5])dnl
  undefine([IU_CVAR])])dnl

dnl 
dnl Following are some more specific tests
dnl

dnl IU_CHECK_WEAK_REFS -- See if any of a variety of `weak reference'
dnl mechanisms works.  If so, this defines HAVE_WEAK_REFS, and one of
dnl HAVE_ATTR_WEAK_REFS, HAVE_PRAGMA_WEAK_REFS, or HAVE_ASM_WEAK_REFS to
dnl indicate which sort.
dnl
dnl This can't just be a compile-check, as gcc somtimes accepts the syntax even
dnl feature isn't actually supported.
dnl
AC_DEFUN([IU_CHECK_WEAK_REFS], [
  AC_CACHE_CHECK(whether gcc weak references work,
		 inetutils_cv_attr_weak_refs,
    AC_TRY_LINK([],
      [extern char *not_defined (char *, char *) __attribute__ ((weak));
	if (not_defined) puts ("yes"); ],
      [inetutils_cv_attr_weak_refs=yes],
      [inetutils_cv_attr_weak_refs=no]))
  if test "$inetutils_cv_weak_refs" = yes; then
    AC_DEFINE(HAVE_WEAK_REFS) AC_DEFINE(HAVE_ATTR_WEAK_REFS)
  else
    AC_CACHE_CHECK(whether pragma weak references work,
		   inetutils_cv_pragma_weak_refs,
      AC_TRY_LINK([],
	[extern char *not_defined (char *, char *);
#pragma weak not_defined
	 if (not_defined) puts ("yes"); ],
	[inetutils_cv_pragma_weak_refs=yes],
	[inetutils_cv_pragma_weak_refs=no]))
    if test "$inetutils_cv_pragma_weak_refs" = yes; then
      AC_DEFINE(HAVE_WEAK_REFS) AC_DEFINE(HAVE_PRAGMA_WEAK_REFS)
    else
      AC_CACHE_CHECK(whether asm weak references work,
		     inetutils_cv_asm_weak_refs,
	AC_TRY_LINK([],
	  [extern char *not_defined (char *, char *);
	   asm (".weak not_defined");
	   if (not_defined) puts ("yes"); ],
	  [inetutils_cv_asm_weak_refs=yes],
	  [inetutils_cv_asm_weak_refs=no]))
      if test "$inetutils_cv_asm_weak_refs" = yes; then
	AC_DEFINE(HAVE_WEAK_REFS) AC_DEFINE(HAVE_ASM_WEAK_REFS)
      fi
    fi
  fi])dnl

dnl IU_LIB_NCURSES -- check for, and configure, ncurses
dnl
AC_DEFUN([IU_LIB_NCURSES], [
  AC_ARG_ENABLE(ncurses,    [  --disable-ncurses       don't prefer -lncurses over -lcurses],
              , enable_ncurses=yes)
  if test "$enable_ncurses" = yes; then
    AC_CHECK_LIB(ncurses, initscr, LIBNCURSES="-lncurses")
    if test "$LIBNCURSES"; then
      # Use ncurses header files instead of the ordinary ones, if possible;
      # is there a better way of doing this, that avoids looking in specific
      # directories?
      AC_ARG_WITH(ncurses-include-dir,
[  --with-ncurses-include-dir=DIR
                          Set directory containing the include files for
                          use with -lncurses, when it isn't installed as
                          the default curses library.  If DIR is "none",
                          then no special ncurses include files are used.
  --without-ncurses-include-dir
                          Equivalent to --with-ncurses-include-dir=none])dnl
      if test "${with_ncurses_include_dir+set}" = set; then
        AC_MSG_CHECKING(for ncurses include dir)
	case "$with_ncurses_include_dir" in
	  no|none)
	    inetutils_cv_includedir_ncurses=none;;
	  *)
	    inetutils_cv_includedir_ncurses="$with_ncurses_include_dir";;
	esac
        AC_MSG_RESULT($inetutils_cv_includedir_ncurses)
      else
	AC_CACHE_CHECK(for ncurses include dir,
		       inetutils_cv_includedir_ncurses,
	  for D in $includedir $prefix/include /local/include /usr/local/include /include /usr/include; do
	    if test -d $D/ncurses; then
	      inetutils_cv_includedir_ncurses="$D/ncurses"
	      break
	    fi
	    test "$inetutils_cv_includedir_ncurses" \
	      || inetutils_cv_includedir_ncurses=none
	  done)
      fi
      if test "$inetutils_cv_includedir_ncurses" = none; then
        NCURSES_INCLUDE=""
      else
        NCURSES_INCLUDE="-I$inetutils_cv_includedir_ncurses"
      fi
    fi
  fi
  AC_SUBST(NCURSES_INCLUDE)
  AC_SUBST(LIBNCURSES)])dnl

dnl IU_LIB_TERMCAP -- check for various termcap libraries
dnl
AC_DEFUN([IU_LIB_TERMCAP], [
  AC_REQUIRE([IU_LIB_NCURSES])
  if test "$LIBNCURSES"; then
    LIBTERMCAP="$LIBNCURSES"
  else
    AC_CHECK_LIB(curses, tgetent, LIBTERMCAP=-lcurses)
    if test "$ac_cv_lib_curses_tgetent" = no; then
      AC_CHECK_LIB(termcap, tgetent, LIBTERMCAP=-ltermcap)
    fi
    if test "$ac_cv_lib_termcap_tgetent" = no; then
      AC_CHECK_LIB(termlib, tgetent, LIBTERMCAP=-ltermlib)
    fi
  fi
  AC_SUBST(LIBTERMCAP)])dnl

dnl IU_LIB_CURSES -- checke for curses, and associated libraries
dnl
AC_DEFUN([IU_LIB_CURSES], [
  AC_REQUIRE([IU_LIB_TERMCAP])
  AC_REQUIRE([IU_LIB_NCURSES])
  if test "$LIBNCURSES"; then
    LIBCURSES="$LIBNCURSES"	# ncurses doesn't require termcap
  else
    _IU_SAVE_LIBS="$LIBS"
    LIBS="$LIBTERMCAP"
    AC_CHECK_LIB(curses, initscr, LIBCURSES="-lcurses")
    if test "$LIBCURSES" -a "$LIBTERMCAP" -a "$LIBCURSES" != "$LIBTERMCAP"; then
      AC_CACHE_CHECK(whether curses needs $LIBTERMCAP,
		     inetutils_cv_curses_needs_termcap,
	LIBS="$LIBCURSES"
	AC_TRY_LINK([#include <curses.h>], [initscr ();],
		    [inetutils_cv_curses_needs_termcap=no],
		    [inetutils_cv_curses_needs_termcap=yes]))
      if test $inetutils_cv_curses_needs_termcap = yes; then
	  LIBCURSES="$LIBCURSES $LIBTERMCAP"
      fi
    fi
    LIBS="$_IU_SAVE_LIBS"
  fi
  AC_SUBST(LIBCURSES)])dnl

dnl IU_CONFIG_PATHS -- Configure system paths for use by programs
dnl   $1 - PATHS    -- The file to read containing the paths
dnl   $2 - MAKEDEFS -- The file to generate containing make `PATHDEF_' vars
dnl   $3 - HDRDEFS  -- The file to generate containing c header stuff
dnl
dnl From the paths listed in the file PATHS, generate a file of make input
dnl (MAKEDEFS) containing a make variable for each PATH_FOO, called
dnl PATHDEF_FOO, which is set to a cpp option to define that path, unless it
dnl is to be defined using a system define, in which case the
dnl corresponding make variable is empty.  A file called HDRDEFS will also be
dnl generated containing cpp statements.  For each PATH_FOO which is found
dnl to be available as a system define, a statement will be generated which
dnl defines it to be that system define, unless it is already defined (which
dnl will be case if overridden by make).
dnl
AC_DEFUN([IU_CONFIG_PATHS], [
  dnl
  dnl We need to know if we're cross compiling.
  dnl
  AC_REQUIRE([AC_PROG_CC])
  dnl
  AC_CHECK_HEADER(paths.h, AC_DEFINE(HAVE_PATHS_H) iu_paths_h="<paths.h>")
  dnl 
  dnl A slightly bogus use of AC_ARG_WITH; we never actually use
  dnl $with_PATHVAR, we just want to get this entry put into the help list.
  dnl We actually look for `with_' variables corresponding to each path
  dnl configured.
  dnl 
  AC_ARG_WITH(PATHVAR,
[  --with-PATHVAR=PATH     Set the value of PATHVAR to PATH
                          PATHVAR is the name of a \`PATH_FOO' variable,
                          downcased, with \`_' changed to \`-'
  --without-PATHVAR       Never define PATHVAR by any method])dnl

  iu_cache_file="/tmp/,iu-path-cache.$$"
  iu_tmp_file="/tmp/,iu-tmp.$$"
  ac_clean_files="$ac_clean_files $iu_cache_file $iu_tmp_file"
  while read iu_path iu_search; do
    test "$iu_path" = "#" -o -z "$iu_path" && continue

    iu_pathvar="`echo $iu_path  | sed y/${IU_UCASE}/${iu_lcase}/`"
    AC_MSG_CHECKING(for value of $iu_path)

    iu_val='' iu_hdr='' iu_sym=''
    iu_cached='' iu_defaulted=''
    iu_cross_conflict=''
    if test "`eval echo '$'{with_$iu_pathvar+set}`" = set; then
      # User-supplied value
      eval iu_val=\"'$'with_$iu_pathvar\"
    elif test "`eval echo '$'{inetutils_cv_$iu_pathvar+set}`" = set; then
      # Cached value
      eval iu_val=\"'$'inetutils_cv_$iu_pathvar\"
      # invert escaped $(...) notation used in autoconf cache
      eval iu_val=\"\`echo \'"$iu_val"\' \| sed \''s/@(/$\(/g'\'\`\"
      iu_cached="(cached) "
    elif test "`eval echo '$'{inetutils_cv_hdr_$iu_pathvar+set}`" = set; then
      # Cached non-value
      eval iu_hdr=\"'$'inetutils_cv_hdr_$iu_pathvar\"
      eval iu_sym=\"'$'inetutils_cv_hdr_sym_$iu_pathvar\"
      iu_cached="(cached) "
    else
      # search for a reasonable value

      iu_test_type=r		# `exists'
      iu_default='' iu_prev_cross_test=''
      for iu_try in $iu_paths_h $iu_search; do
	iu_cross_test=''
	case "$iu_try" in
	  "<"*">"*)
	    # <HEADER.h> and <HEADER.h>:SYMBOL -- look for SYMBOL in <HEADER.h>
	    # SYMBOL defaults to _$iu_path (e.g., _PATH_FOO)
	    changequote(,)	dnl Avoid problems with [ ] in regexps
	    eval iu_hdr=\'`echo "$iu_try" |sed 's/:.*$//'`\'
	    eval iu_sym=\'`echo "$iu_try" |sed -n 's/^<[^>]*>:\(.*\)$/\1/p'`\'
	    changequote([,])
	    test "$iu_sym" || iu_sym="_$iu_path"
	    AC_EGREP_CPP(HAVE_$iu_sym,
[#include ]$iu_hdr[
#ifdef $iu_sym
HAVE_$iu_sym
#endif],
	      :, iu_hdr='' iu_sym='')
	    ;;

	  search:*)
	    # Do a path search.  The syntax here is: search:NAME[:PATH]...

	    # Path searches always generate potential conflicts
	    test "$cross_compiling" = yes && { iu_cross_conflict=yes; continue; }

	    changequote(,)	dnl Avoid problems with [ ] in regexps
	    iu_name="`echo $iu_try | sed 's/^search:\([^:]*\).*$/\1/'`"
	    iu_spath="`echo $iu_try | sed 's/^search:\([^:]*\)//'`"
	    changequote([,])

	    test "$iu_spath" || iu_spath="$PATH"

	    for iu_dir in `echo "$iu_spath" | sed 'y/:/ /'`; do
	      test -z "$iu_dir" && iu_dir=.
	      if test -$iu_test_type "$iu_dir/$iu_name"; then
		iu_val="$iu_dir/$iu_name"
		break
	      fi
	    done
	    ;;

	  no) iu_default=no;;
	  x|d|f|c|b) iu_test_type=$iu_try;;

	  *)
	    # Just try the given name, with make-var substitution.  Besides 
	    # yielding a value if found, this also sets the default.

	    case "$iu_try" in "\""*"\"")
	      # strip off quotes
	      iu_try="`echo $iu_try | sed -e 's/^.//' -e 's/.$//'`"
	    esac

	    test -z "$iu_default" && iu_default="$iu_try"
	    test "$cross_compiling" = yes && { iu_cross_test=yes; continue; }

	    # See if the value begins with a $(FOO)/${FOO} make variable
	    # corresponding to a shell variable, and if so set try_exp to the
	    # value thereof.  Recurse.
	    iu_try_exp="$iu_try"
	    changequote(,)
	    iu_try_var="`echo "$iu_try_exp" |sed -n 's;^\$[({]\([-_a-zA-Z]*\)[)}].*;\1;p'`"
	    while eval test \"$iu_try_var\" && eval test '${'$iu_try_var'+set}'; do
	      # yes, and there's a corresponding shell variable, which substitute
	      if eval test \"'$'"$iu_try_var"\" = NONE; then
		# Not filled in by configure yet
		case "$iu_try_var" in
		  prefix | exec_prefix)
		    iu_try_exp="$ac_default_prefix`echo "$iu_try_exp" |sed 's;^\$[({][-_a-zA-Z]*[)}];;'`";;
		esac
		iu_try_var=''	# Stop expansion here
	      else
		# Use the actual value of the shell variable
		eval iu_try_exp=\"`echo "$iu_try_exp" |sed 's;^\$[({]\([-_a-zA-Z]*\)[)}];\$\1;'`\"
		iu_try_var="`echo "$iu_try_exp" |sed -n 's;^\$[({]\([-_a-zA-Z]*\)[)}].*;\1;p'`"
	      fi
	    done
	    changequote([,])

	    test -$iu_test_type "$iu_try_exp" && iu_val="$iu_try"
	    ;;

	esac

	test "$iu_val" -o "$iu_hdr" && break
	test "$iu_cross_test" -a "$iu_prev_cross_test" && iu_cross_conflict=yes
	iu_prev_cross_test=$iu_cross_test
      done

      if test -z "$iu_val" -a -z "$iu_hdr"; then
	if test -z "$iu_default"; then
	  iu_val=no
	else
	  iu_val="$iu_default"
	  iu_defaulted="(default) "
	fi
      fi
    fi

    if test "$iu_val"; then
      AC_MSG_RESULT(${iu_cached}${iu_defaulted}$iu_val)
      test "$iu_cross_conflict" -a "$iu_defaulted" \
	&& AC_MSG_WARN(may be incorrect because of cross-compilation)
      # Put the value in the autoconf cache.  We replace $( with @( to avoid
      # variable evaluation problems when autoconf reads the cache later.
      echo inetutils_cv_$iu_pathvar=\'"`echo "$iu_val" | sed 's/\$(/@(/g'`"\'
    elif test "$iu_hdr"; then
      AC_MSG_RESULT(${iu_cached}from $iu_sym in $iu_hdr)
      echo inetutils_cv_hdr_$iu_pathvar=\'"$iu_hdr"\'
      echo inetutils_cv_hdr_sym_$iu_pathvar=\'"$iu_sym"\'
    fi
  done <[$1] >$iu_cache_file

  # Read the cache values constructed by the previous loop, 
  . $iu_cache_file

  # Construct the pathdefs file -- a file of make variable definitions, of
  # the form PATHDEF_FOO, that contain cc -D switches to define the cpp macro
  # PATH_FOO.
  grep -v '^inetutils_cv_hdr_' < $iu_cache_file | \
  while read iu_cache_set; do
    iu_var="`echo $iu_cache_set | sed 's/=.*$//'`"
    eval iu_val=\"'$'"$iu_var"\"
    # invert escaped $(...) notation used in autoconf cache
    eval iu_val=\"\`echo \'"$iu_val"\' \| sed \''s/@(/$\(/g'\'\`\"
    if test "$iu_val" != no; then
      iu_path="`echo $iu_var | sed -e 's/^inetutils_cv_//' -e y/${iu_lcase}/${IU_UCASE}/`"
      iu_pathdef="`echo $iu_path | sed 's/^PATH_/PATHDEF_/'`"
      echo $iu_pathdef = -D$iu_path='\"'"$iu_val"'\"'
    fi
  done >$[$2]
  AC_SUBST_FILE([$2])

  # Generate a file of #ifdefs that defaults PATH_FOO macros to _PATH_FOO (or
  # some other symbol) (excluding any who's value is set to `no').
  grep '^inetutils_cv_hdr_sym_' < $iu_cache_file | \
  while read iu_cache_set; do
    iu_sym_var="`echo "$iu_cache_set" | sed 's/=.*$//'`"
    eval iu_sym=\"'$'"$iu_sym_var"\"
    iu_path="`echo $iu_sym_var | sed -e 's/^inetutils_cv_hdr_sym_//' -e y/${iu_lcase}/${IU_UCASE}/`"
    cat <<EOF
#ifndef $iu_path
#define $iu_path $iu_sym
#endif
EOF
  done >$[$3]
  AC_SUBST_FILE([$3])])
