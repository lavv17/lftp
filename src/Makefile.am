localedir = @LOCALE_DIR@
pkgverlibdir = $(pkglibdir)/$(VERSION)

bin_PROGRAMS = lftp
bin_SCRIPTS = lftpget
pkgdata_SCRIPTS = import-ncftp import-netscape
noinst_SCRIPTS = ftpget

EXTRA_DIST = $(pkgdata_SCRIPTS) $(bin_SCRIPTS) $(noinst_SCRIPTS) \
	example1.cc example2.cc Fish.cc Fish.h

lftp_SOURCES = lftp.cc complete.h complete.cc lftp_rl.c lftp_rl.h
#ftpget_SOURCES = ftpget.cc

noinst_LIBRARIES = libtasks.a libjobs.a

EXTRA_LTLIBRARIES = libnetwork.la proto-ftp.la proto-http.la proto-file.la cmd-mirror.la cmd-sleep.la proto-fish.la
pkgverlib_LTLIBRARIES = $(MODULES_LA)

proto_ftp_la_SOURCES  = ftpclass.cc FtpListInfo.cc FtpGlob.cc FtpDirList.cc\
 md5.c ftp-opie.c FtpSplitList.cc FileCopyFtp.cc
proto_http_la_SOURCES = Http.cc HttpDir.cc
proto_file_la_SOURCES = LocalAccess.cc
proto_fish_la_SOURCES = Fish.cc
cmd_mirror_la_SOURCES = MirrorJob.cc
cmd_sleep_la_SOURCES  = SleepJob.cc getdate.c
libnetwork_la_SOURCES = NetAccess.cc Resolver.cc lftp_ssl.cc buffer_ssl.cc

proto_ftp_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
proto_http_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_file_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_fish_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_mirror_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_sleep_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
libnetwork_la_LDFLAGS = -avoid-version -rpath $(pkgverlibdir) $(SSL_LDFLAGS)
libnetwork_la_LIBADD  = $(SOCKSLIBS) $(SSL_LIBS)

proto_ftp_la_LIBADD  = -L$(DESTDIR)$(pkgverlibdir) libnetwork.la
proto_http_la_LIBADD = -L$(DESTDIR)$(pkgverlibdir) libnetwork.la
proto_fish_la_LIBADD = -L$(DESTDIR)$(pkgverlibdir) libnetwork.la

libtasks_a_SOURCES = PollVec.cc PollVec.h SMTask.cc SMTask.h ProcWait.cc\
 ProcWait.h ftpclass.cc ftpclass.h GetPass.cc GetPass.h\
 CharReader.cc CharReader.h LsCache.cc LsCache.h Resolver.cc Resolver.h\
 FileAccess.h FileAccess.cc ResMgr.h ResMgr.cc LocalAccess.h LocalAccess.cc\
 Filter.cc Filter.h SignalHook.cc SignalHook.h FileCopy.cc FileCopy.h\
 xmalloc.cc xmalloc.h FileSet.cc FileSet.h FileCopyFtp.cc FileCopyFtp.h\
 FtpListInfo.cc FtpListInfo.h ftp-opie.c md5.c md5.h log.h log.cc\
 buffer.cc buffer.h url.cc url.h StatusLine.cc StatusLine.h plural.c plural.h\
 misc.h misc.cc Http.cc Http.h fg.cc fg.h module.cc module.h modconfig.h\
 resource.cc DummyProto.cc DummyProto.h FtpGlob.cc FtpGlob.h FtpDirList.cc\
 FtpDirList.h ArgV.cc ArgV.h HttpDir.cc HttpDir.h ascii_ctype.h\
 NetAccess.cc NetAccess.h FtpSplitList.h FtpSplitList.cc Speedometer.cc\
 Speedometer.h lftp_ssl.cc lftp_ssl.h netrc.cc netrc.h buffer_ssl.cc\
 buffer_ssl.h Fish.cc Fish.h

libjobs_a_SOURCES = Job.cc Job.h CmdExec.cc CmdExec.h\
 commands.cc mgetJob.h mgetJob.cc SysCmdJob.cc SysCmdJob.h rmJob.cc rmJob.h\
 parsecmd.cc MirrorJob.cc MirrorJob.h\
 mvJob.cc mvJob.h alias.cc alias.h\
 bookmark.cc bookmark.h CatJob.cc CatJob.h GetJob.cc GetJob.h\
 mkdirJob.cc mkdirJob.h pgetJob.cc pgetJob.h FileFeeder.cc FileFeeder.h\
 keyvalue.cc keyvalue.h history.cc history.h\
 SleepJob.cc SleepJob.h getdate.y getdate.h FindJob.cc FindJob.h\
 ChmodJob.cc ChmodJob.h TreatFileJob.cc TreatFileJob.h CopyJob.cc CopyJob.h

lftp_LDFLAGS = -export-dynamic $(LFTP_SSL_LDFLAGS)
lftp_LDADD = libjobs.a libtasks.a $(READLINE) $(READLINE_SUPPLIB) \
	$(top_builddir)/lib/liblib.a $(INTLLIBS) $(LFTP_SSL_LIBS)
lftp_DEPENDENCIES = libjobs.a libtasks.a $(READLINE_DEPEND) \
	$(top_builddir)/lib/liblib.a

confpaths.h: Makefile
	echo "#define SYSCONFDIR \"$(sysconfdir)\""  > $@
	echo "#define LOCALEDIR  \"$(localedir)\""  >> $@
	echo "#define PKGDATADIR \"$(pkgdatadir)\"" >> $@
	echo "#define PKGLIBDIR  \"$(pkglibdir)\""  >> $@

DISTCLEANFILES = confpaths.h

INCLUDES = -I$(top_srcdir)/include $(SSL_CPPFLAGS)

# These files can be linked to ../include by configure, but not always.
OMIT_DEPENDENCIES = readline.h keymaps.h chardefs.h tilde.h rltypedefs.h\
	history.h libintl.hregex.h glob.h fnmatch.h poll.h rlstdc.h

lftp.o commands.o module.o: confpaths.h

# libtool does not strip modules, do it here.
install-data-local:
	@-if test "$(MODULES_LA)"; then \
	    case " $(LDFLAGS) " in *" -s "*) \
	       echo $(STRIP) $(DESTDIR)$(pkgverlibdir)/*.so; \
	       $(STRIP) $(DESTDIR)$(pkgverlibdir)/*.so;; \
	    esac; fi
