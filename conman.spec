# $Id$

Name:		conman
Version:	0
Release:	0

Summary:	ConMan - The Console Manager
Group:		Applications/Communications
License:	GPL
URL:		http://www.llnl.gov/linux/conman/

BuildRoot:	%{_tmppath}/%{name}-%{version}

Source0:	%{name}-%{version}.tar

%description
ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.  It currently supports
local serial devices and remote terminal servers (via the telnet protocol).
Its features include:

  - mapping symbolic names to console devices
  - logging all output from a console device to file
  - supporting monitor (R/O), interactive (R/W), and
    broadcast (W/O) modes of console access
  - allowing clients to join or steal console "write" privileges
  - executing Expect scripts across multiple consoles in parallel

%prep
%setup

%build
%configure
make

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
DESTDIR="$RPM_BUILD_ROOT" make install

%clean
rm -rf "$RPM_BUILD_ROOT"

%post
/sbin/chkconfig --add conman

%preun
if [ "$1" = 0 ]; then
  /sbin/service conman stop >/dev/null 2>&1 || :
  /sbin/chkconfig --del conman
fi

%postun
if [ "$1" -ge 1 ]; then
  /sbin/service conman condrestart >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,0755)
%doc AUTHORS
%doc ChangeLog
%doc COPYING
%doc DISCLAIMER
%doc FAQ
%doc NEWS
%config(noreplace) %{_sysconfdir}/conman.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/conman
%{_sysconfdir}/init.d/conman
%{_bindir}/*
%{_sbindir}/*
%{_prefix}/lib/*
%{_mandir}/*/*
