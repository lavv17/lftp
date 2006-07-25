noinst_LTLIBRARIES = liblib.la
liblib_la_SOURCES = poll.h glob.h fnmatch_.h regex.h argmatch.c argmatch.h verify.h exit.h xalloc-die.c xstrtoumax.c
liblib_la_LIBADD = @LTLIBOBJS@ @ALLOCA@
INCLUDES = -I$(top_srcdir)/include
EXTRA_DIST = fnmatch_loop.c intprops.h stdbool_.h
BUILT_SOURCES = $(STDBOOL_H)

# We need the following in order to create <stdbool.h> when the system
# doesn't have one that works.
stdbool.h: stdbool_.h
	sed -e 's/@''HAVE__BOOL''@/$(HAVE__BOOL)/g' < $(srcdir)/stdbool_.h > $@-t
	mv $@-t $@
MOSTLYCLEANFILES = stdbool.h stdbool.h-t

