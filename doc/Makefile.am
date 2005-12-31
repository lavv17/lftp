man_MANS = lftp.1 lftpget.1
EXTRA_DIST = $(man_MANS)

lftp.ps: lftp.1
	groff -man -Tps $< > $@
