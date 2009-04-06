# $Id$

Name:		conman
Version:	0
Release:	0

Summary:	ConMan: The Console Manager
Group:		Applications/System
License:	GPLv2+
URL:		http://home.gna.org/conman/

Requires:	expect
BuildRoot:	%{_tmppath}/%{name}-%{version}

%if 0%{?chaos} >= 4 || 0%{?rhel} >= 6 || 0%{?fedora} >= 9
BuildRequires:  freeipmi-devel
%endif

%if 0%{?rhel} >= 6 || 0%{?fedora} >= 7
BuildRequires:  tcp_wrappers-devel
%else
%if 0%{?rhel} < 6 || 0%{?fedora} < 7 || 0%{?rhl}
BuildRequires:  tcp_wrappers
%else
%if "%{_vendor}" == "suse"
BuildRequires:  tcpd-devel
%endif
%endif
%endif

Source0:	%{name}-%{version}.tar

%description
ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.  It supports:
  - local serial devices
  - remote terminal servers (via the telnet protocol)
  - IPMI Serial-Over-LAN (via FreeIPMI)
  - Unix domain sockets
  - external processes (eg, using Expect for telnet/ssh/ipmi-sol connections)

Its features include:
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
#
%if 0%{?_initrddir:1}
if [ "%{_sysconfdir}/init.d" != "%{_initrddir}" ]; then
  mkdir -p "$RPM_BUILD_ROOT%{_initrddir}"
  mv "$RPM_BUILD_ROOT%{_sysconfdir}/init.d"/* "$RPM_BUILD_ROOT%{_initrddir}/"
fi
%endif

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
%doc THANKS
%config(noreplace) %{_sysconfdir}/conman.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/conman
%config(noreplace) %{_sysconfdir}/sysconfig/conman
%{?_initrddir:%{_initrddir}}%{!?_initrddir:%{_sysconfdir}/init.d}/conman
%{_bindir}/*
%{_sbindir}/*
%{_prefix}/lib/*
%{_mandir}/*/*
