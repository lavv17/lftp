%define name lftp
%define version 2.1.9

Summary: The lftp command line ftp/http client
Name: %{name} 
Version: %{version}
Release: 1
Copyright: GPL
Url: http://ftp.yars.free.net/projects/lftp/
BuildRoot: /var/tmp/%{name}-%{version}-root
Source: ftp.yars.free.net:/pub/software/unix/net/ftp/client/lftp/%{name}-%{version}.tar.gz
Group: Applications/Networking


%description
LFTP is a shell-like command line ftp client. It is
reliable: can retry operations and does reget automatically.
It can do several transfers simultaneously in background.
You can start a transfer in background and continue browsing
the ftp site or another one. This all is done in one process.
Background jobs will be completed in nohup mode if you exit
or close modem connection. Lftp has reput, mirror, reverse
mirror among its features. Since version 2.0 it also supports
http protocol.



%prep
rm -rf $RPM_BUILD_ROOT

%setup
CFLAGS="$RPM_OPT_FLAGS" ./configure --prefix=/usr
make

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT/etc
make prefix=$RPM_BUILD_ROOT/usr install
install -c -m 644 lftp.conf $RPM_BUILD_ROOT/etc/lftp.conf

%clean
rm -rf $RPM_BUILD_ROOT

%changelog
* Sat Oct 02 1999 Alexander Lukyanov <lav@yars.free.net>

- 2.1.1 release
- removed ChangeLog from doc.

* Mon Sep 27 1999 Alexander Lukyanov <lav@yars.free.net>

- 2.1.0 release

* Tue Sep 14 1999 Alexander Lukyanov <lav@yars.free.net>

- add lftpget

* Tue Jul 27 1999 Adrian Likins <alikins@redhat.com>

-initial release


%files
%defattr(644,root,root,755)
%doc README README.modules FAQ THANKS COPYING TODO lftp.lsm NEWS INSTALL
%doc /usr/man/man1/ftpget.1
%doc /usr/man/man1/lftp.1
%config /etc/lftp.conf
%dir /usr/share/lftp
%attr(755,root,root) /usr/share/lftp/import-*
/usr/share/locale/*/*/*
%attr(755,root,root) /usr/bin/lftp
%attr(755,root,root) /usr/bin/ftpget
%attr(755,root,root) /usr/bin/lftpget
