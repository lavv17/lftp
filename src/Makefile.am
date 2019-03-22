pkgverlibdir = $(pkglibdir)/$(VERSION)

bin_PROGRAMS = lftp
bin_SCRIPTS = lftpget
pkgdata_SCRIPTS = import-ncftp import-netscape verify-file convert-mozilla-cookies xdg-move
noinst_SCRIPTS = ftpget

EXTRA_DIST = $(pkgdata_SCRIPTS) $(bin_SCRIPTS) $(noinst_SCRIPTS)

lftp_SOURCES = lftp.cc complete.h complete.cc lftp_rl.c lftp_rl.h attach.cc attach.h

TASK_MODULES = liblftp-pty.la liblftp-network.la proto-ftp.la proto-http.la proto-file.la proto-fish.la proto-sftp.la
JOB_MODULES = cmd-mirror.la cmd-sleep.la cmd-torrent.la
if WITH_MODULES
  pkgverlib_LTLIBRARIES = $(TASK_MODULES) $(JOB_MODULES)
else
  TASK_MODULES_STATIC = $(TASK_MODULES)
  JOB_MODULES_STATIC = $(JOB_MODULES)
endif
lib_LTLIBRARIES = liblftp-tasks.la liblftp-jobs.la

proto_ftp_la_SOURCES  = ftpclass.cc ftpclass.h FtpListInfo.cc FtpListInfo.h\
 FtpDirList.cc FtpDirList.h ftp-opie.c netkey.c FileCopyFtp.cc FileCopyFtp.h
proto_http_la_SOURCES = Http.cc Http.h HttpHeader.cc HttpHeader.h\
 HttpAuth.cc HttpAuth.h HttpDir.cc HttpDir.h HttpDirXML.cc
proto_file_la_SOURCES = LocalAccess.cc LocalAccess.h
proto_fish_la_SOURCES = Fish.cc Fish.h
proto_sftp_la_SOURCES = SFtp.cc SFtp.h
cmd_mirror_la_SOURCES = MirrorJob.cc MirrorJob.h
cmd_sleep_la_SOURCES  = SleepJob.cc SleepJob.h
cmd_torrent_la_SOURCES= Torrent.cc Torrent.h TorrentTracker.cc TorrentTracker.h\
 DHT.cc DHT.h Bencode.cc Bencode.h
liblftp_pty_la_SOURCES     = PtyShell.cc PtyShell.h lftp_pty.c lftp_pty.h SSH_Access.cc SSH_Access.h
liblftp_network_la_SOURCES = NetAccess.cc NetAccess.h Resolver.cc Resolver.h\
 lftp_ssl.cc lftp_ssl.h buffer_ssl.cc buffer_ssl.h RateLimit.cc RateLimit.h\
 network.cc network.h buffer_zlib.cc buffer_zlib.h

if NEED_TRIO
   TRIO = $(top_builddir)/trio/libtrio.la
endif
GNULIB = $(top_builddir)/lib/libgnu.la

proto_ftp_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
proto_http_la_CPPFLAGS = $(AM_CPPFLAGS) $(EXPAT_CFLAGS)
proto_http_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_file_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_fish_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_sftp_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_mirror_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_sleep_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
cmd_torrent_la_LDFLAGS= -module -avoid-version -rpath $(pkgverlibdir)
liblftp_pty_la_LDFLAGS     = -avoid-version -rpath $(pkgverlibdir)
liblftp_network_la_CPPFLAGS = $(AM_CPPFLAGS) $(OPENSSL_CPPFLAGS) $(ZLIB_CPPFLAGS) $(LIBGNUTLS_CFLAGS)
liblftp_network_la_LDFLAGS = -avoid-version -rpath $(pkgverlibdir)
liblftp_network_la_LIBADD  = $(SOCKSLIBS) $(OPENSSL_LDFLAGS) $(OPENSSL_LIBS) $(LIBGNUTLS_LIBS) $(LIBSOCKET) $(GNULIB) $(ZLIB_LDFLAGS) $(ZLIB)

proto_ftp_la_LIBADD  = liblftp-network.la
proto_http_la_LIBADD = liblftp-network.la $(EXPAT_LDFLAGS) $(EXPAT_LIBS)
proto_fish_la_LIBADD = liblftp-network.la liblftp-pty.la
proto_sftp_la_LIBADD = liblftp-network.la liblftp-pty.la
cmd_torrent_la_LIBADD  = liblftp-network.la

liblftp_tasks_la_SOURCES = PollVec.cc PollVec.h SMTask.cc SMTask.h ProcWait.cc\
 ProcWait.h GetPass.cc GetPass.h ConnectionSlot.cc ConnectionSlot.h\
 CharReader.cc CharReader.h Cache.cc Cache.h LsCache.cc LsCache.h\
 FileAccess.h FileAccess.cc ResMgr.h ResMgr.cc Ref.h ProtoLog.cc ProtoLog.h\
 Filter.cc Filter.h SignalHook.cc SignalHook.h FileCopy.cc FileCopy.h\
 xmalloc.cc xmalloc.h xstring.cc xstring.h FileSet.cc FileSet.h\
 log.h log.cc StringSet.cc StringSet.h xarray.cc xarray.h xmap.cc xmap.h\
 buffer.cc buffer.h url.cc url.h StatusLine.cc StatusLine.h plural.c plural.h\
 misc.h misc.cc fg.cc fg.h module.cc module.h modconfig.h\
 resource.cc DummyProto.cc DummyProto.h Error.cc Error.h\
 ArgV.cc ArgV.h ascii_ctype.h keyvalue.cc keyvalue.h bookmark.cc bookmark.h\
 Speedometer.cc FileGlob.cc FileGlob.h xlist.h xheap.h\
 Speedometer.h netrc.cc netrc.h lftp_tinfo.cc lftp_tinfo.h\
 TimeDate.cc TimeDate.h Timer.cc Timer.h GetFileInfo.cc GetFileInfo.h\
 StringPool.cc StringPool.h DirColors.cc DirColors.h IdNameCache.cc\
 IdNameCache.h PatternSet.cc PatternSet.h LocalDir.cc LocalDir.h
liblftp_tasks_la_LIBADD = $(TASK_MODULES_STATIC) $(TRIO) $(GNULIB)\
 $(LIB_CRYPTO) $(INET_PTON_LIB) $(LIB_CLOCK_GETTIME) $(SOCKSLIBS)\
 $(LIB_POLL) $(LIB_SELECT) $(LTLIBINTL) $(LTLIBICONV)

liblftp_jobs_la_SOURCES = Job.cc Job.h CmdExec.cc CmdExec.h\
 commands.cc mgetJob.h mgetJob.cc SysCmdJob.cc SysCmdJob.h rmJob.cc rmJob.h\
 parsecmd.cc mvJob.cc mvJob.h mmvJob.cc mmvJob.h alias.cc alias.h\
 CatJob.cc CatJob.h EditJob.cc EditJob.h GetJob.cc GetJob.h\
 ColumnOutput.h ColumnOutput.cc FileSetOutput.h FileSetOutput.cc\
 mkdirJob.cc mkdirJob.h pgetJob.cc pgetJob.h FileFeeder.cc FileFeeder.h\
 QueueFeeder.cc QueueFeeder.h History.cc History.h\
 FindJob.cc FindJob.h FindJobDu.cc FindJobDu.h ChmodJob.cc ChmodJob.h\
 TreatFileJob.cc TreatFileJob.h CopyJob.cc CopyJob.h echoJob.cc echoJob.h\
 OutputJob.cc OutputJob.h FileCopyOutputJob.cc FileCopyOutputJob.h\
 buffer_std.cc buffer_std.h
liblftp_jobs_la_LIBADD = $(JOB_MODULES_STATIC) liblftp-tasks.la

lftp_CPPFLAGS = $(AM_CPPFLAGS) $(READLINE_CFLAGS)
lftp_LDFLAGS = -export-dynamic
lftp_LDADD = liblftp-jobs.la liblftp-tasks.la $(READLINE_LDFLAGS) $(READLINE_LIBS)
lftp_DEPENDENCIES = liblftp-jobs.la

CLEANFILES = *.la

AM_CPPFLAGS = -I$(top_srcdir)/lib -I$(top_srcdir)/trio

# libtool does not strip modules, do it here.
install-data-hook:
if WITH_MODULES
	rm -f $(DESTDIR)$(pkgverlibdir)/*.la; \
	case " $(LDFLAGS) " in *" -s "*) \
		$(STRIP) $(DESTDIR)$(pkgverlibdir)/*.so;; \
	esac
else
	-rmdir $(DESTDIR)$(pkgverlibdir) 2>/dev/null || :
endif

# without *.la files libtool does not remove the *.so files.
uninstall-hook:
	-rmdir "$(DESTDIR)$(pkgdatadir)" 2>/dev/null || :
if WITH_MODULES
	for m in $(pkgverlib_LTLIBRARIES); do rm -f "$(DESTDIR)$(pkgverlibdir)/$${m%.la}.so"; done
	-rmdir "$(DESTDIR)$(pkgverlibdir)" 2>/dev/null || :
	-rmdir "$(DESTDIR)$(pkglibdir)" 2>/dev/null || :
endif

commands.lo lftp.o module.lo resource.lo: $(top_builddir)/lib/configmake.h
$(top_builddir)/lib/configmake.h:
	$(MAKE) -C $(top_builddir)/lib configmake.h
