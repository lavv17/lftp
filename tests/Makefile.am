AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_srcdir)/trio -I$(top_srcdir)/src

if WITH_MODULES
  PROTO_FTP =
  PROTO_HTTP =
  AM_TESTS_ENVIRONMENT = LFTP_MODULE_PATH=../src/.libs:./.libs
else
  PROTO_FTP  = ../src/proto-ftp.la
  PROTO_HTTP = ../src/proto-http.la
endif

check_PROGRAMS = ftp-mlsd ftp-list http-get ftp-cls-l
check_SCRIPTS = module1

ftp_mlsd_SOURCES = ftp-mlsd.cc
ftp_list_SOURCES = ftp-list.cc
ftp_cls_l_SOURCES = ftp-cls-l.cc
http_get_SOURCES = http-get.cc

ftp_mlsd_LDADD = $(PROTO_FTP) ../src/liblftp-tasks.la
ftp_list_LDADD = $(PROTO_FTP) ../src/liblftp-tasks.la
ftp_cls_l_LDADD = $(PROTO_FTP) ../src/liblftp-tasks.la ../src/liblftp-jobs.la
http_get_LDADD = $(PROTO_HTTP) ../src/liblftp-tasks.la

check_LTLIBRARIES = module1.la
module1_la_SOURCES = module1.cc
module1_la_LDFLAGS  = -module -avoid-version -rpath `pwd`/.libs

TESTS = $(check_PROGRAMS) $(check_SCRIPTS)
EXTRA_DIST = $(check_SCRIPTS)
