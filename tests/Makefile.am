check_PROGRAMS = ftp-mlsd ftp-list http-get ftp-cls-l
check_SCRIPTS = module1 lftp-https-get lftp-queue-kill

ftp_mlsd_SOURCES = ftp-mlsd.cc
ftp_list_SOURCES = ftp-list.cc
ftp_cls_l_SOURCES = ftp-cls-l.cc
http_get_SOURCES = http-get.cc

AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_srcdir)/trio -I$(top_srcdir)/src

if WITH_MODULES
  PROTO_FTP =
  PROTO_HTTP =
  TESTS_ENVIRONMENT = LFTP_MODULE_PATH=$(top_builddir)/src/.libs:$(builddir)/.libs
else
  PROTO_FTP  = $(top_builddir)/src/proto-ftp.la
  PROTO_HTTP = $(top_builddir)/src/proto-http.la
endif

LIBTASKS = $(top_builddir)/src/liblftp-tasks.la
LIBJOBS  = $(top_builddir)/src/liblftp-jobs.la

ftp_mlsd_LDADD = $(PROTO_FTP) $(LIBTASKS)
ftp_list_LDADD = $(PROTO_FTP) $(LIBTASKS)
ftp_cls_l_LDADD = $(PROTO_FTP) $(LIBJOBS) $(LIBTASKS)
http_get_LDADD = $(PROTO_HTTP) $(LIBTASKS)

check_LTLIBRARIES = module1.la
module1_la_SOURCES = module1.cc
module1_la_LDFLAGS  = -module -avoid-version -rpath $(abs_builddir)/.libs

TESTS = $(check_PROGRAMS) $(check_SCRIPTS)
EXTRA_DIST = $(check_SCRIPTS)
