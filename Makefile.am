EXTRA_DIST = lftp.conf lftp.lsm FAQ acconfig.h README README.debug-levels\
 README.modules MIRRORS BUGS FEATURES ltcf-c.sh ltcf-cxx.sh
SUBDIRS = include readline-4.1 lib doc intl src po m4
AUTOMAKE_OPTIONS = foreign
M4DIR = $(srcdir)/m4
ACLOCAL_AMFLAGS = -I $(M4DIR)
ACINCLUDE_INPUTS = $(M4DIR)/gettext.m4 $(M4DIR)/lcmessage.m4 $(M4DIR)/progtest.m4

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
	@echo $(PACKAGE).spec updating...
	@rm -f $(distdir)/$(PACKAGE).spec
	@sed "s/%define version .*/%define version $(VERSION)/" \
	    $(srcdir)/$(PACKAGE).spec > $(distdir)/$(PACKAGE).spec && \
	    cp $(distdir)/$(PACKAGE).spec $(srcdir)/$(PACKAGE).spec
	@echo $(PACKAGE).lsm updating...
	@rm -f $(distdir)/$(PACKAGE).lsm
	@SIZE=`$(TAR) chof - $(distdir) | gzip $(GZIP_ENV) | wc -c`; SIZE=`expr \( $$SIZE + 512 \) / 1024`; echo Size: $$SIZE kB; \
	 DATE=`date +%Y-%m-%d`; echo Date: $$DATE; \
	 sed -e "s/Version:	.*/Version:	$(VERSION)/" \
	     -e "s/[0-9]* kB $(PACKAGE)-[0-9.]*\.tar\.gz/$$SIZE kB $(PACKAGE)-$(VERSION).tar.gz/" \
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
	    rm -f $(PACKAGE).tar.gz && ln -s $(PACKAGE)-$(VERSION).tar.gz $(PACKAGE).tar.gz)
	mutt -s add lsm@execpc.com < $(PACKAGE).lsm
	cvs tag $(PACKAGE)-`echo $(VERSION) | sed 's/\./-/g'` .

$(srcdir)/acinclude.m4: $(ACINCLUDE_INPUTS)
	cat $(ACINCLUDE_INPUTS) > $@
