EXTRA_DIST = lftp.conf lftp.lsm iumacros.m4 FAQ acconfig.h README.*\
 MIRRORS BUGS FEATURES
SUBDIRS = include readline-4.1 lib doc intl src po m4
AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4

install-data-local:
	if [ ! -f $(DESTDIR)$(sysconfdir)/lftp.conf ]; then \
		$(mkinstalldirs) $(DESTDIR)$(sysconfdir); \
		$(INSTALL_DATA) lftp.conf $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

uninstall-local:
	# remove conf file if not changed.
	if cmp -s $(DESTDIR)$(sysconfdir)/lftp.conf lftp.conf; then \
		rm -f $(DESTDIR)$(sysconfdir)/lftp.conf; \
	fi

dist-hook:
	@echo lftp.spec updating...
	@sed "s/%define version .*/%define version $(VERSION)/" \
	    $(srcdir)/lftp.spec > $(distdir)/lftp.spec && \
	    cp $(distdir)/lftp.spec $(srcdir)/lftp.spec
