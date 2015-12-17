EXTRA_DIST = lftp.conf lftp.lsm FAQ MIRRORS BUGS FEATURES\
 README README.debug-levels README.modules README.dnssec\
 lib/hstrerror.c
EXTRA_DIST += $(top_srcdir)/.version
BUILT_SOURCES = $(top_srcdir)/.version
if NEED_TRIO
   TRIO = trio
endif
SUBDIRS = m4 doc lib $(TRIO) src po
ACLOCAL_AMFLAGS = -I m4

install-data-local:
	if [ ! -f $(DESTDIR)$(sysconfdir)/lftp.conf ]; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir); \
		$(INSTALL_DATA) $(srcdir)/lftp.conf $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

uninstall-local:
	if cmp -s $(DESTDIR)$(sysconfdir)/lftp.conf $(srcdir)/lftp.conf; then \
		echo "$(DESTDIR)$(sysconfdir)/lftp.conf is not changed and will be removed."; \
		rm -f $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@

dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version
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

REL_DIR=/home/lav/www-lftp/ftp

release-check:
	for opt in "" "--with-modules" "--without-gnutls" "--without-openssl" \
			"--without-gnutls --without-openssl" \
			"--with-modules --without-gnutls --without-openssl"; \
	 do \
	 	echo "Testing $$opt ..."; \
		make DISTCHECK_CONFIGURE_FLAGS="$$opt" distcheck >/dev/null || exit 1; \
	 done

release:
	make release-check
	make release-ftp

release-ftp:
	mv $(PACKAGE)-$(VERSION).tar.gz $(REL_DIR)/$(PACKAGE)-$(VERSION).tar.gz
	cp -p $(PACKAGE).lsm $(REL_DIR)/$(PACKAGE)-$(VERSION).lsm
	cd $(REL_DIR) && \
	    (addbz $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.bz2; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.xz; \
	    md5sum $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION).tar.bz2 $(PACKAGE)-$(VERSION).tar.xz > $(PACKAGE)-$(VERSION).md5sum)
	mutt -s add lsm@execpc.com < $(PACKAGE).lsm
