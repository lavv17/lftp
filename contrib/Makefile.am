appdir = $(datadir)/applications
icondir = $(datadir)/icons/hicolor/48x48/apps

app_DATA = lftp.desktop
icon_DATA = lftp-icon.png
noinst_SCRIPTS = get-all-torrents

EXTRA_DIST = $(app_DATA) $(icon_DATA) $(noinst_SCRIPTS)

.PHONY: install-zsh-completion
install-zsh-completion:
	install -Dm644 zsh-completion.zsh $(datadir)/zsh/site-functions/_lftp
