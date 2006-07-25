localedir = $(datadir)/locale
pkgverlibdir = $(pkglibdir)/$(VERSION)

bin_PROGRAMS = lftp
bin_SCRIPTS = lftpget
pkgdata_SCRIPTS = import-ncftp import-netscape verify-file convert-netscape-cookies
noinst_SCRIPTS = ftpget

EXTRA_DIST = $(pkgdata_SCRIPTS) $(bin_SCRIPTS) $(noinst_SCRIPTS)

lftp_SOURCES = lftp.cc complete.h complete.cc lftp_rl.c lftp_rl.h

noinst_PROGRAMS = example1 example2 example1-cmd
noinst_LTLIBRARIES = example-module1.la
example1_SOURCES = example1.cc
example1_cmd_SOURCES = example1-cmd.cc
example2_SOURCES = example2.cc
example_module1_la_SOURCES = example-module1.cc
example_module1_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)

if WITH_MODULES
pkgverlib_LTLIBRARIES = liblftp-pty.la liblftp-network.la proto-ftp.la proto-http.la proto-file.la cmd-mirror.la cmd-sleep.la proto-fish.la proto-sftp.la
endif
lib_LTLIBRARIES = liblftp-tasks.la liblftp-jobs.la

proto_ftp_la_SOURCES  = ftpclass.cc ftpclass.h FtpListInfo.cc FtpListInfo.h\
 FtpDirList.cc FtpDirList.h md5.c md5.h ftp-opie.c FileCopyFtp.cc FileCopyFtp.h
proto_http_la_SOURCES = Http.cc Http.h HttpDir.cc HttpDir.h HttpDirXML.cc
proto_file_la_SOURCES = LocalAccess.cc LocalAccess.h
proto_fish_la_SOURCES = Fish.cc Fish.h
proto_sftp_la_SOURCES = SFtp.cc SFtp.h
cmd_mirror_la_SOURCES = MirrorJob.cc MirrorJob.h
cmd_sleep_la_SOURCES  = SleepJob.cc SleepJob.h
liblftp_pty_la_SOURCES     = PtyShell.cc PtyShell.h lftp_pty.c lftp_pty.h
liblftp_network_la_SOURCES = NetAccess.cc NetAccess.h Resolver.cc Resolver.h\
 lftp_ssl.cc lftp_ssl.h buffer_ssl.cc buffer_ssl.h

if !WITH_MODULES
add_tasks = $(proto_ftp_la_SOURCES) $(proto_http_la_SOURCES)\
 $(proto_file_la_SOURCES) $(proto_fish_la_SOURCES) $(proto_sftp_la_SOURCES)\
 $(liblftp_pty_la_SOURCES) $(liblftp_network_la_SOURCES)
add_tasks_libadd = $(EXPAT_LIBS) $(SOCKSLIBS) $(OPENSSL_LIBS) $(LIBGNUTLS_LIBS)
add_jobs = $(cmd_mirror_la_SOURCES) $(cmd_sleep_la_SOURCES)
endif

proto_ftp_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
proto_http_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_file_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_fish_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
proto_sftp_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_mirror_la_LDFLAGS = -module -avoid-version -rpath $(pkgverlibdir)
cmd_sleep_la_LDFLAGS  = -module -avoid-version -rpath $(pkgverlibdir)
liblftp_pty_la_LDFLAGS     = -avoid-version -rpath $(pkgverlibdir)
liblftp_network_la_LDFLAGS = -avoid-version -rpath $(pkgverlibdir) $(OPENSSL_LDFLAGS)
liblftp_network_la_LIBADD  = $(SOCKSLIBS) $(OPENSSL_LIBS) $(LIBGNUTLS_LIBS)

proto_ftp_la_LIBADD  = -L$(DESTDIR)$(pkgverlibdir) liblftp-network.la
proto_http_la_LIBADD = -L$(DESTDIR)$(pkgverlibdir) liblftp-network.la $(EXPAT_LIBS)
proto_fish_la_LIBADD = -L$(DESTDIR)$(pkgverlibdir) liblftp-network.la liblftp-pty.la
proto_sftp_la_LIBADD = -L$(DESTDIR)$(pkgverlibdir) liblftp-network.la liblftp-pty.la

liblftp_tasks_la_SOURCES = PollVec.cc PollVec.h SMTask.cc SMTask.h ProcWait.cc\
 ProcWait.h GetPass.cc GetPass.h ConnectionSlot.cc ConnectionSlot.h\
 CharReader.cc CharReader.h Cache.cc Cache.h LsCache.cc LsCache.h\
 FileAccess.h FileAccess.cc ResMgr.h ResMgr.cc\
 Filter.cc Filter.h SignalHook.cc SignalHook.h FileCopy.cc FileCopy.h\
 xmalloc.cc xmalloc.h FileSet.cc FileSet.h\
 log.h log.cc StringSet.cc StringSet.h\
 buffer.cc buffer.h url.cc url.h StatusLine.cc StatusLine.h plural.c plural.h\
 misc.h misc.cc fg.cc fg.h module.cc module.h modconfig.h\
 resource.cc DummyProto.cc DummyProto.h\
 ArgV.cc ArgV.h ascii_ctype.h keyvalue.cc keyvalue.h bookmark.cc bookmark.h\
 Speedometer.cc FileGlob.cc FileGlob.h\
 Speedometer.h netrc.cc netrc.h lftp_tinfo.cc lftp_tinfo.h\
 TimeDate.cc TimeDate.h Timer.cc Timer.h GetFileInfo.cc GetFileInfo.h\
 StringPool.cc StringPool.h DirColors.cc DirColors.h IdNameCache.cc\
 IdNameCache.h PatternSet.cc PatternSet.h LocalDir.cc LocalDir.h\
 mbswidth.c mbswidth.h wcwidth.h\
 $(add_tasks)
liblftp_tasks_la_LDFLAGS = $(OPENSSL_LDFLAGS)
liblftp_tasks_la_LIBADD = $(top_builddir)/lib/liblib.la $(LFTP_LIBGNUTLS_LIBS) $(LTLIBINTL) $(LTLIBICONV) $(add_tasks_libadd)

liblftp_jobs_la_SOURCES = Job.cc Job.h CmdExec.cc CmdExec.h\
 commands.cc mgetJob.h mgetJob.cc SysCmdJob.cc SysCmdJob.h rmJob.cc rmJob.h\
 parsecmd.cc mvJob.cc mvJob.h alias.cc alias.h\
 CatJob.cc CatJob.h GetJob.cc GetJob.h\
 ColumnOutput.h ColumnOutput.cc FileSetOutput.h FileSetOutput.cc\
 mkdirJob.cc mkdirJob.h pgetJob.cc pgetJob.h FileFeeder.cc FileFeeder.h\
 QueueFeeder.cc QueueFeeder.h history.cc history.h\
 FindJob.cc FindJob.h FindJobDu.cc FindJobDu.h ChmodJob.cc ChmodJob.h\
 TreatFileJob.cc TreatFileJob.h CopyJob.cc CopyJob.h echoJob.cc echoJob.h\
 OutputJob.cc OutputJob.h FileCopyOutputJob.cc buffer_std.cc buffer_std.h\
 getdate.y getdate.h\
 $(add_jobs)
liblftp_jobs_la_LIBADD = liblftp-tasks.la

lftp_LDADD = liblftp-jobs.la liblftp-tasks.la $(MODULES_LA_STATIC) $(READLINE)
lftp_DEPENDENCIES = liblftp-jobs.la liblftp-tasks.la $(MODULES_LA_STATIC) $(READLINE_DEPEND)

example1_LDADD = liblftp-tasks.la $(MODULES_LA_STATIC)
example1_DEPENDENCIES = liblftp-tasks.la $(MODULES_LA_STATIC)
example1_cmd_LDADD = liblftp-jobs.la liblftp-tasks.la $(MODULES_LA_STATIC)
example1_cmd_DEPENDENCIES = liblftp-jobs.la liblftp-tasks.la $(MODULES_LA_STATIC)
example2_LDADD = liblftp-tasks.la $(MODULES_LA_STATIC)
example2_DEPENDENCIES = liblftp-tasks.la $(MODULES_LA_STATIC)

confpaths.h: Makefile
	echo "#define SYSCONFDIR \"$(sysconfdir)\""  > $@
	echo "#define LOCALEDIR  \"$(localedir)\""  >> $@
	echo "#define PKGDATADIR \"$(pkgdatadir)\"" >> $@
	echo "#define PKGLIBDIR  \"$(pkglibdir)\""  >> $@

DISTCLEANFILES = confpaths.h *.la

INCLUDES = -I$(top_srcdir)/include $(OPENSSL_CPPFLAGS) $(LIBGNUTLS_CFLAGS)

# These files can be linked to ../include by configure, but not always.
#OMIT_DEPENDENCIES = readline.h keymaps.h chardefs.h tilde.h rltypedefs.h\
#	history.h libintl.hregex.h glob.h fnmatch.h poll.h rlstdc.h

lftp.o commands.lo module.lo: confpaths.h

# libtool does not strip modules, do it here.
install-data-hook:
	@-if test "$(MODULES_LA)"; then \
	    rm -f $(DESTDIR)$(pkgverlibdir)/*.la; \
	    case " $(LDFLAGS) " in *" -s "*) \
	       echo $(STRIP) $(DESTDIR)$(pkgverlibdir)/*.so; \
	       $(STRIP) $(DESTDIR)$(pkgverlibdir)/*.so;; \
	    esac; else rmdir $(DESTDIR)$(pkgverlibdir) 2>/dev/null || true; fi
