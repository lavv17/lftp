EXTRA_DIST = lftp.conf lftp.lsm iumacros.m4 FAQ acconfig.h README.*\
 MIRRORS BUGS lftp.spec FEATURES
SUBDIRS = include readline-4.0 lib doc intl src po
AUTOMAKE_OPTIONS = foreign

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
