EXTRA_DIST = lftp.conf FAQ MIRRORS BUGS FEATURES\
 README README.debug-levels README.modules README.dnssec\
 lib/hstrerror.c lftp.spec.in lftp.spec
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

EXTRA_DIST += $(top_srcdir)/.version
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version


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
	cd $(REL_DIR) && \
	    (addbz $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.bz2; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.xz; \
	    md5sum $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION).tar.bz2 $(PACKAGE)-$(VERSION).tar.xz > $(PACKAGE)-$(VERSION).md5sum)
