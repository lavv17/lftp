EXTRA_DIST = lftp.conf lftp.lsm FAQ acconfig.h README README.debug-levels\
 README.modules MIRRORS BUGS FEATURES ltcf-c.sh ltcf-cxx.sh
SUBDIRS = include readline-4.1 lib doc intl src po m4
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

install-data-local:
	if [ ! -f $(DESTDIR)$(sysconfdir)/lftp.conf ]; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir); \
		$(INSTALL_DATA) $(srcdir)/lftp.conf $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

uninstall-local:
	# remove conf file if not changed.
	if cmp -s $(DESTDIR)$(sysconfdir)/lftp.conf lftp.conf; then \
		rm -f $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

dist-hook:
	@echo lftp.spec updating...
	@rm -f $(distdir)/lftp.spec
	@sed "s/%define version .*/%define version $(VERSION)/" \
	    $(srcdir)/lftp.spec > $(distdir)/lftp.spec && \
	    cp $(distdir)/lftp.spec $(srcdir)/lftp.spec
	@echo lftp.lsm updating...
	@rm -f $(distdir)/lftp.lsm
	@SIZE=`GZIP=$(GZIP_ENV) $(TAR) chozf - $(distdir) | wc -c`; SIZE=`expr \( $$SIZE + 512 \) / 1024`; echo Size: $$SIZE kB; \
	 DATE=`date +%Y-%m-%d`; echo Date: $$DATE; \
	 sed -e "s/Version:	.*/Version:	$(VERSION)/" \
	     -e "s/[0-9]* kB lftp-[0-9.]*\.tar\.gz/$$SIZE kB lftp-$(VERSION).tar.gz/" \
	     -e "s/Entered-date:	.*/Entered-date:	$$DATE/" \
	    $(srcdir)/lftp.lsm > $(distdir)/lftp.lsm && \
	    cp $(distdir)/lftp.lsm $(srcdir)/lftp.lsm

release:
	cvs commit -m $(VERSION)
	make distcheck
	cvs commit -m $(VERSION)
	make dist
	mv lftp-$(VERSION).tar.gz /ftp/pub/software/unix/net/ftp/client/lftp/
	cp -p lftp.lsm /ftp/pub/software/unix/net/ftp/client/lftp/lftp-$(VERSION).lsm
	cd /ftp/pub/software/unix/net/ftp/client/lftp && \
	    (addbz lftp-$(VERSION).tar.gz; \
	    rm -f lftp.tar.gz && ln -s lftp-$(VERSION).tar.gz lftp.tar.gz)
	mailx -s add lsm@execpc.com < lftp.lsm
	cvs tag lftp-`echo $(VERSION) | sed 's/\./-/g'` .
