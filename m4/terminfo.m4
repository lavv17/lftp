AC_DEFUN([lftp_TERMINFO],
[
   AC_CHECK_HEADERS(curses.h term.h ncurses/curses.h ncurses/term.h termcap.h)

   # Get a library with terminal caps if needed; prefer one with tigetstr.
   AC_SEARCH_LIBS(tigetstr, [tinfo curses ncurses], [
	 AC_DEFINE(HAVE_TIGETSTR, 1, [Define to 1 if you have the `tigetstr' function.])
	 AC_CHECK_FUNCS(tgetstr)
      ], [AC_SEARCH_LIBS(tgetstr, termcap,
	    [AC_DEFINE(HAVE_TGETSTR, 1)],
	    [AC_MSG_ERROR([No terminfo, termcap or curses library found. Install ncurses-devel.])])])

   if test "x$ac_cs_ac_cv_header_curses_h" = xno -a "x$ac_cv_header_ncurses_curses_h" = xno \
      -a "x$ac_cs_ac_cv_header_term_h" = xno -a "x$ac_cv_header_ncurses_term_h" = xno \
      -a "x$ac_cs_ac_cv_header_termcap_h" = xno; then
         AC_MSG_ERROR([No terminfo, termcap or curses headers found. Install ncurses-devel]);
   fi
])
