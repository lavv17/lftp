EXTRA_DIST = lftp.conf lftp.lsm iumacros.m4 FAQ acconfig.h README.*\
 MIRRORS BUGS lftp.spec
SUBDIRS = include readline-4.0 lib doc intl src po
AUTOMAKE_OPTIONS = foreign

install-data-local:
	if [ ! -f $(sysconfdir)/lftp.conf ]; then \
		$(mkinstalldirs) $(sysconfdir); \
		$(INSTALL_DATA) lftp.conf $(sysconfdir)/lftp.conf; \
	fi

uninstall-local:
	# remove conf file if not changed.
	if cmp -s $(sysconfdir)/lftp.conf lftp.conf; then \
		rm -f $(sysconfdir)/lftp.conf; \
	fi
