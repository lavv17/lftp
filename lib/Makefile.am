noinst_LIBRARIES = liblib.a
liblib_a_SOURCES = poll.h glob.h fnmatch.h regex.h filemode.c mbswidth.c \
		   human.c xstrtol.c xstrtoul.c
liblib_a_LIBADD = @LIBOBJS@ @ALLOCA@
INCLUDES = -I$(top_srcdir)/include
EXTRA_DIST = fnmatch_loop.c
