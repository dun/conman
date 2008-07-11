# $Id$

Name:		conman
Version:	0
Release:	0

Summary:	ConMan: The Console Manager
Group:		Applications/System
License:	GPL
URL:		http://home.gna.org/conman/

Requires:	expect
BuildRequires:	tcp_wrappers
BuildRoot:	%{_tmppath}/%{name}-%{version}

Source0:	%{name}-%{version}.tar

%description
ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.  It supports local serial
devices, remote terminal servers (via the telnet protocol), unix domain
sockets, and external processes (e.g., using Expect to control connections
over telnet, ssh, or ipmi-sol).  Its features include:

  - logging (and optionally timestamping) console device output to file
  - connecting to consoles in monitor (R/O) or interactive (R/W) mode
  - allowing clients to share or steal console write privileges
  - broadcasting client output to multiple consoles

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
if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --add conman; fi

%preun
if [ "$1" = 0 ]; then
  %{_sysconfdir}/init.d/conman stop >/dev/null 2>&1 || :
  if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --del conman; fi
fi

%postun
if [ "$1" -ge 1 ]; then
  %{_sysconfdir}/init.d/conman condrestart >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,0755)
%doc AUTHORS
%doc ChangeLog
%doc COPYING
%doc DISCLAIMER*
%doc FAQ
%doc NEWS
%doc README
%config(noreplace) %{_sysconfdir}/conman.conf
%config(noreplace) %{_sysconfdir}/*/*
%{_bindir}/*
%{_sbindir}/*
%{_prefix}/lib/*
%{_mandir}/*/*
