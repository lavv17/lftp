localedir = $(datadir)/locale

bin_PROGRAMS = lftp
bin_SCRIPTS = lftpget
pkgdata_SCRIPTS = import-ncftp import-netscape
noinst_SCRIPTS = ftpget

EXTRA_DIST = $(pkgdata_SCRIPTS) $(bin_SCRIPTS) $(noinst_SCRIPTS) example1.cc example2.cc

lftp_SOURCES = lftp.cc complete.h complete.cc lftp_rl.c lftp_rl.h
#ftpget_SOURCES = ftpget.cc

noinst_LIBRARIES = libtasks.a libjobs.a

## this does not work too well as I need to compile modules with -DMODULE
# MODULES_LA = proto-ftp.la
# lib_LTLIBRARIES = $(MODULES_LA)
# proto_ftp_la_SOURCES = ftpclass.cc FtpListInfo.cc FtpGlob.cc FtpDirList.cc md5.c ftp-opie.c NetAccess.cc FtpSplitList.cc FileCopyFtp.cc
# proto_ftp_la_LDFLAGS = -module -avoid-version
# proto_ftp_la_CXXFLAGS = -DMODULE

libtasks_a_SOURCES = PollVec.cc PollVec.h SMTask.cc SMTask.h ProcWait.cc\
 ProcWait.h ftpclass.cc ftpclass.h GetPass.cc GetPass.h\
 CharReader.cc CharReader.h LsCache.cc LsCache.h Resolver.cc Resolver.h\
 FileAccess.h FileAccess.cc ResMgr.h ResMgr.cc LocalAccess.h LocalAccess.cc\
 Filter.cc Filter.h SignalHook.cc SignalHook.h FileCopy.cc FileCopy.h\
 xmalloc.cc xmalloc.h FileSet.cc FileSet.h FileCopyFtp.cc FileCopyFtp.h\
 FtpListInfo.cc FtpListInfo.h ftp-opie.c md5.c md5.h log.h log.cc\
 buffer.cc buffer.h url.cc url.h StatusLine.cc StatusLine.h plural.c plural.h\
 misc.h misc.cc Http.cc Http.h fg.cc fg.h module.c module.h modconfig.h\
 resource.cc DummyProto.cc DummyProto.h FtpGlob.cc FtpGlob.h FtpDirList.cc\
 FtpDirList.h ArgV.cc ArgV.h HttpDir.cc HttpDir.h ascii_ctype.h\
 NetAccess.cc NetAccess.h FtpSplitList.h FtpSplitList.cc Speedometer.cc\
 Speedometer.h

libjobs_a_SOURCES = Job.cc Job.h CmdExec.cc CmdExec.h\
 commands.cc mgetJob.h mgetJob.cc SysCmdJob.cc SysCmdJob.h rmJob.cc rmJob.h\
 parsecmd.cc MirrorJob.cc MirrorJob.h\
 mvJob.cc mvJob.h alias.cc alias.h netrc.cc netrc.h\
 bookmark.cc bookmark.h CatJob.cc CatJob.h GetJob.cc GetJob.h\
 mkdirJob.cc mkdirJob.h pgetJob.cc pgetJob.h FileFeeder.cc FileFeeder.h\
 keyvalue.cc keyvalue.h history.cc history.h\
 SleepJob.cc SleepJob.h getdate.y getdate.h FindJob.cc FindJob.h\
 ChmodJob.cc ChmodJob.h TreatFileJob.cc TreatFileJob.h CopyJob.cc CopyJob.h

lftp_LDFLAGS = $(EXPORT_DYNAMIC)
lftp_LDADD = libjobs.a libtasks.a $(READLINE) $(READLINE_SUPPLIB) $(top_builddir)/lib/liblib.a $(INTLLIBS) $(SOCKSLIBS)
lftp_DEPENDENCIES = libjobs.a libtasks.a $(READLINE_DEPEND) $(top_builddir)/lib/liblib.a
#ftpget_LDFLAGS = $(EXPORT_DYNAMIC)
#ftpget_LDADD = libjobs.a libtasks.a $(top_builddir)/lib/liblib.a $(INTLLIBS) $(SOCKSLIBS)

confpaths.h: Makefile
	echo "#define SYSCONFDIR \"$(sysconfdir)\""  > $@
	echo "#define LOCALEDIR  \"$(localedir)\""  >> $@
	echo "#define PKGDATADIR \"$(pkgdatadir)\"" >> $@
	echo "#define PKGLIBDIR  \"$(pkglibdir)\""  >> $@

DISTCLEANFILES = confpaths.h

INCLUDES = -I$(top_srcdir)/include

# These files can be linked to ../include by configure, but not always.
OMIT_DEPENDENCIES = readline.h keymaps.h chardefs.h tilde.h history.h libintl.h\
 regex.h glob.h fnmatch.h poll.h rlstdc.h

lftp.o commands.o module.o: confpaths.h


all-local: $(MODULES)
clean-local:
	-rm -f $(MODULES)

install-exec-local:
	@$(NORMAL_INSTALL)
	@if [ '$(MODULES)' ]; then $(mkinstalldirs) $(DESTDIR)$(pkglibdir); \
	list='$(MODULES)'; for p in $$list; do \
	  if test -f $$p; then \
	    echo "  $(INSTALL_DATA) $$p $(DESTDIR)$(pkglibdir)/$$p"; \
	     $(INSTALL_DATA) $$p $(DESTDIR)$(pkglibdir)/$$p; \
	  else :; fi; \
	done; fi
uninstall-local:
	@$(NORMAL_UNINSTALL)
	@list='$(MODULES)'; for p in $$list; do \
	    rm -f $(DESTDIR)$(pkglibdir)/$$p; \
	done

MODULEFLAGS=$(PIC_FLAG) -shared -nostdlib -DMODULE
CXXMODLINK=$(CXXCOMPILE) $(LDFLAGS) $(MODULEFLAGS)
proto-http.so: Http.cc HttpDir.cc NetAccess.cc
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
proto-file.so: LocalAccess.cc
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
proto-ftp.so: ftpclass.cc FtpListInfo.cc FtpGlob.cc FtpDirList.cc\
 md5.c ftp-opie.c NetAccess.cc FtpSplitList.cc FileCopyFtp.cc
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
proto-fish.so: Fish.cc NetAccess.cc
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
cmd-mirror.so: MirrorJob.cc
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
cmd-sleep.so: SleepJob.cc getdate.c
	@-rm -f $@
	$(CXXMODLINK) $^ -o $@
