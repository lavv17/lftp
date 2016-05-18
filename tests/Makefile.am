AM_CXXFLAGS = -I../lib -I../src -I../trio

check_PROGRAMS = ftp-mlsd
ftp_mlsd_SOURCES = ftp-mlsd.cc
ftp_mlsd_LDADD = ../src/proto-ftp.la ../src/liblftp-tasks.la

TESTS = $(check_PROGRAMS)
