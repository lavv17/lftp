%define	name	lftp
%define	version	2.0.4
%define	release	1
%define	serial	1

Summary:	LFTP command line file transfer program
Name:		%{name}
Version:	%{version}
Release:	%{release}
Copyright:	GPL
Group:		Applications/Internet
Url:		http://ftp.yars.free.net/projects/lftp/
Source:		%{name}-%{version}.tar.gz
BuildRoot:	/var/tmp/%{name}-%{version}

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
%setup -q -n %{name}-%{version}

%build
./configure --prefix=/usr --sysconfdir=/etc
make all

%install
if [ -e $RPM_BUILD_ROOT ]; then rm -rf $RPM_BUILD_ROOT; fi
mkdir $RPM_BUILD_ROOT

# can't just use this, we should install things manually
make prefix=$RPM_BUILD_ROOT/usr sysconfdir=$RPM_BUILD_ROOT/etc install

mkdir -p -m 0755 $RPM_BUILD_ROOT/etc
install -m 644 lftp.conf $RPM_BUILD_ROOT/etc/lftp.conf

# no shared libraries yet
#%post -p /sbin/ldconfig
#%postun -p /sbin/ldconfig

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-, root, root)
%doc ABOUT-NLS BUGS COPYING ChangeLog FAQ INSTALL NEWS README README.modules THANKS TODO
%config /etc/lftp.conf
/usr/man/man1/ftpget.1
/usr/man/man1/lftp.1
/usr/bin/lftp
/usr/bin/ftpget
/usr/bin/lftpget
/usr/share/lftp/import-ncftp
/usr/share/lftp/import-netscape
/usr/share/locale/

%changelog
* Fri Sep 10 1999 Wang Jian <lark@linux.net.cn>
- Initial package
