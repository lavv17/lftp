man_MANS = lftp.1 lftpget.1 lftp.conf.5
EXTRA_DIST = $(man_MANS)

lftp.ps: lftp.1
	groff -t -man -Tps $< > $@
