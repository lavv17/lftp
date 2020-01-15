EXTRA_DIST = lftp.conf FAQ MIRRORS BUGS FEATURES\
 README README.debug-levels README.modules README.dnssec\
 lib/hstrerror.c lftp.spec.in lftp.spec
if NEED_TRIO
   TRIO = trio
endif
SUBDIRS = m4 doc lib $(TRIO) src po contrib tests
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


# release rules
REL_DIR=$(HOME)/www-lftp/ftp

release-check:
	for opt in "--with-modules" "--without-gnutls" "--without-openssl" \
			"--without-gnutls --without-openssl" \
			"--with-modules --without-gnutls --without-openssl" \
			"--disable-ipv6" ""; \
	 do \
		echo "Testing $$opt ..."; \
		$(MAKE) distclean >/dev/null && $(srcdir)/configure -q $$opt || exit 1; \
		$(MAKE) V=0 check || exit 1; \
	 done
	$(MAKE) V=0 DISTCHECK_CONFIGURE_FLAGS=-q distcheck

release:
	$(MAKE) release-check
	$(MAKE) release-ftp

release-ftp:
	mv $(PACKAGE)-$(VERSION).tar.gz $(REL_DIR)/$(PACKAGE)-$(VERSION).tar.gz
	cd $(REL_DIR) && \
	    (addbz $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.gz; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.bz2; \
	    gpg -ba $(PACKAGE)-$(VERSION).tar.xz; \
	    md5sum $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE)-$(VERSION).tar.bz2 $(PACKAGE)-$(VERSION).tar.xz > $(PACKAGE)-$(VERSION).md5sum)


# AppImage building rules
linuxdeployqt=linuxdeployqt-continuous-$(build_cpu).AppImage
lftp_desktop=$(prefix)/share/applications/lftp.desktop
lftp_appimage=$(PACKAGE)-$(VERSION)-$(host_cpu).AppImage
CLEANFILES=$(lftp_appimage)
DISTCLEANFILES=$(linuxdeployqt)

$(linuxdeployqt):
	test -x src/lftp || $(MAKE) $(AM_MAKEFLAGS) all
	LFTP_MODULE_PATH=$(top_builddir)/src/.libs $(top_builddir)/src/lftp -c \
	    "get https://github.com/probonopd/linuxdeployqt/releases/download/continuous/$(linuxdeployqt)"
	chmod a+x $(linuxdeployqt)

$(lftp_appimage): $(linuxdeployqt)
	$(MAKE) $(AM_MAKEFLAGS) install DESTDIR="`readlink -f appdir`"
	unset QTDIR QT_PLUGIN_PATH LD_LIBRARY_PATH \
	&& ./$(linuxdeployqt) ./appdir$(lftp_desktop) -bundle-non-qt-libs \
	&& ./$(linuxdeployqt) ./appdir$(lftp_desktop) -appimage
	mv -f lftp-$(host_cpu).AppImage $(lftp_appimage)
	-rm -rf appdir

appimage: $(lftp_appimage)
.PHONY: appimage

clean-local:
	-rm -rf appdir
