AC_DEFUN([lftp_TERMINFO],
[
   AC_CHECK_HEADERS(curses.h term.h ncurses/curses.h ncurses/term.h)

   # Get a library with terminal caps if needed; prefer one with tigetstr.
   AC_SEARCH_LIBS(tigetstr, [curses ncurses], [
	 AC_DEFINE(HAVE_TIGETSTR, 1, [Define to 1 if you have the `tigetstr' function.])
	 AC_CHECK_FUNCS(tgetstr)
      ], [AC_SEARCH_LIBS(tgetstr, termcap,
	    [AC_DEFINE(HAVE_TGETSTR, 1)],
	    [AC_MSG_WARN(No terminfo, termcap or curses library found)])])
])
