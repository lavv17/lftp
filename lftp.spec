%define version 2.3.6
%define release 1
%define use_modules 1

Summary: sophisticated command line file transfer program
Name: lftp
Version: %{version}
Release: %{release}
URL: http://ftp.yars.free.net/projects/lftp/
Source: ftp://ftp.yars.free.net/pub/software/unix/net/ftp/client/lftp/lftp-%{version}.tar.gz
Group: Applications/Internet
BuildRoot: %{_tmppath}/%{name}-buildroot
Copyright: GPL
#Packager: Manoj Kasichainula <manojk+rpm@io.com>

%description
lftp is command line file transfer program. It supports FTP and HTTP
protocols. GNU Readline library is used for input.

%prep
%setup
#%setup -n %{name}

%build

# Make sure that all message catalogs are built
if [ $LINGUAS ]; then
    unset LINGUAS
fi

# The lftp maintainer seems to use a newer version of libtool than Red
# Hat (even 7.0) ships with. So make sure that we don't muck with
# ltconfig
%define __libtoolize true
%if %use_modules
    %configure --with-modules --mandir=%{_mandir}
%else
    %configure --mandir=%{_mandir}
%endif
make

%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}

%clean
rm -rf %{buildroot}

%files
%defattr(644 root root 755)
%doc ABOUT-NLS BUGS COPYING FAQ FEATURES NEWS README* THANKS TODO lftp.lsm
%config /etc/lftp.conf
%attr(755 root root) %{_bindir}/*
%if %use_modules
%{_libdir}/*
%endif
%{_mandir}/man*/*
%attr(- root root) %{_datadir}/lftp
%{_datadir}/locale/*/*/*
