EXTRA_DIST = config.rpath  config.rpath lftp.conf lftp.lsm FAQ acconfig.h README README.debug-levels\
 README.modules MIRRORS BUGS FEATURES
SUBDIRS = include readline-4.3 lib doc src po m4 contrib
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

install-data-local:
	if [ ! -f $(DESTDIR)$(sysconfdir)/lftp.conf ]; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir); \
		$(INSTALL_DATA) $(srcdir)/lftp.conf $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

uninstall-local:
	if cmp -s $(DESTDIR)$(sysconfdir)/lftp.conf lftp.conf; then \
		echo "$(DESTDIR)$(sysconfdir)/lftp.conf is not changed and will be removed."; \
		rm -f $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

dist-hook:
	@echo $(PACKAGE).spec updating...
	@rm -f $(distdir)/$(PACKAGE).spec
	@-sed "s/%define version .*/%define version $(VERSION)/" \
	    $(srcdir)/$(PACKAGE).spec > $(distdir)/$(PACKAGE).spec && \
	    cp $(distdir)/$(PACKAGE).spec $(srcdir)/$(PACKAGE).spec
	@echo $(PACKAGE).lsm updating...
	@rm -f $(distdir)/$(PACKAGE).lsm
	@-SIZE=`$(AMTAR) chof - $(distdir) | GZIP=$(GZIP_ENV) gzip -c | wc -c`; SIZE=`expr \( $$SIZE + 512 \) / 1024`; echo Size: $$SIZE kB; \
	 DATE=`date +%Y-%m-%d`; echo Date: $$DATE; \
	 sed -e "s/Version:	.*/Version:	$(VERSION)/" \
	     -e "s/[0-9]* kB $(PACKAGE)-.*\.tar\.gz/$$SIZE kB $(PACKAGE)-$(VERSION).tar.gz/" \
	     -e "s/Entered-date:	.*/Entered-date:	$$DATE/" \
	    $(srcdir)/$(PACKAGE).lsm > $(distdir)/$(PACKAGE).lsm && \
	    cp $(distdir)/$(PACKAGE).lsm $(srcdir)/$(PACKAGE).lsm

REL_DIR=/ftp/pub/software/unix/net/ftp/client/lftp
release:
	cvs commit -m $(VERSION)
	make dist
	cvs commit -m $(VERSION)
	make distcheck
	mv $(PACKAGE)-$(VERSION).tar.gz $(REL_DIR)
	cp -p $(PACKAGE).lsm $(REL_DIR)/$(PACKAGE)-$(VERSION).lsm
	cd $(REL_DIR) && \
	    (addbz $(PACKAGE)-$(VERSION).tar.gz; \
	    rm -f $(PACKAGE).tar.gz && ln -s $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.bz2; \
	    md5sum $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION).tar.bz2 > $(PACKAGE)-$(VERSION).md5sum)
	mutt -s add lsm@execpc.com < $(PACKAGE).lsm
	cvs tag $(PACKAGE)-`echo $(VERSION) | sed 's/\./-/g'` .
