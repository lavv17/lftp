man_MANS = ftpget.1 lftp.1
EXTRA_DIST = $(man_MANS)

lftp.ps: lftp.1
	groff -man -Tps $< > $@
