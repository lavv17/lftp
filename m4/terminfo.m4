AC_DEFUN(lftp_TERMINFO,
[
   AC_CHECK_HEADERS(curses.h term.h ncurses/curses.h ncurses/term.h)

   AC_SUBST(READLINE_SUPPLIB)

   # Get a library with terminal caps if needed; prefer one with tigetstr.
   AC_CHECK_FUNC(tigetstr,
      [READLINE_SUPPLIB=],
      [AC_CHECK_LIB(curses, tigetstr,
	 [READLINE_SUPPLIB=-lcurses],
	 [AC_CHECK_LIB(ncurses, tigetstr,
	    [READLINE_SUPPLIB=-lncurses],
	    [AC_CHECK_FUNC(tgetent,
	       [READLINE_SUPPLIB=],
	       [AC_CHECK_LIB(curses, tgetent,
		  [READLINE_SUPPLIB=-lcurses],
		  [AC_CHECK_LIB(ncurses, tgetent,
		     [READLINE_SUPPLIB=-lncurses],
		     [AC_CHECK_LIB(termcap, tgetent,
			[READLINE_SUPPLIB=-ltermcap],
			[AC_MSG_WARN(No terminfo, termcap or curses library found)])
		     ])
		  ])
	       ])
	    ])
	 ])
      ])

   # Check for working tigetstr and tgetstr.
   OLDLIBS="$LIBS"
   LIBS="$LIBS $READLINE_SUPPLIB"

   AC_MSG_CHECKING([for tigetstr])
   AC_TRY_LINK([
      #if defined(HAVE_TERM_H)
      #include <term.h>
      #elif defined(HAVE_NCURSES_TERM_H)
      #include <ncurses/term.h>
      #endif

      #if defined(HAVE_CURSES_H)
      #include <curses.h>
      #elif defined(HAVE_NCURSES_CURSES_H)
      #include <ncurses/curses.h>
      #endif],
     [ tigetstr((char *) 0); ],
     [ AC_DEFINE(HAVE_TIGETSTR, 1, [System has usable tigetstr])
       AC_MSG_RESULT(yes) ],
     [ AC_MSG_RESULT(no) ]
   )

   AC_MSG_CHECKING([for tgetstr])
   AC_TRY_LINK([
      #if defined(HAVE_TERM_H)
      #include <term.h>
      #elif defined(HAVE_NCURSES_TERM_H)
      #include <ncurses/term.h>
      #endif

      #if defined(HAVE_CURSES_H)
      #include <curses.h>
      #elif defined(HAVE_NCURSES_CURSES_H)
      #include <ncurses/curses.h>
      #endif],
     [ tgetstr((char *) 0, (char **) 0); ],
     [ AC_DEFINE(HAVE_TGETSTR, 1, [System has usable tgetstr])
       AC_MSG_RESULT(yes) ],
     [ AC_MSG_RESULT(no) ]
   )

   LIBS="$OLDLIBS"
])
