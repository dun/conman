Name:		conman
Version:	0.2.7
Release:	1%{?dist}

Summary:	ConMan: The Console Manager
Group:		Applications/System
License:	GPLv3+
URL:		http://conman.googlecode.com/

Requires:	expect
BuildRoot:	%{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

%if 0%{?chaos} >= 4 || 0%{?rhel} >= 6 || 0%{?fedora} >= 9
BuildRequires:	freeipmi-devel >= 1.0.4
%endif

%if 0%{?rhel} >= 6 || 0%{?fedora} >= 7
BuildRequires:	tcp_wrappers-devel
%else
%if "%{_vendor}" == "redhat"
BuildRequires:	tcp_wrappers
%else
%if "%{_vendor}" == "suse"
BuildRequires:	tcpd-devel
%endif
%endif
%endif

Source0:	%{name}-%{version}.tar.bz2

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
make %{?_smp_mflags}

%install
rm -rf "%{buildroot}"
mkdir -p "%{buildroot}"
make install DESTDIR="%{buildroot}"
#
%if 0%{?_initrddir:1}
if [ "%{_sysconfdir}/init.d" != "%{_initrddir}" ]; then
  mkdir -p "%{buildroot}%{_initrddir}"
  mv "%{buildroot}%{_sysconfdir}/init.d"/* "%{buildroot}%{_initrddir}/"
fi
%endif

%clean
rm -rf "%{buildroot}"

%post
if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --add conman; fi

%preun
if [ "$1" = 0 ]; then
  INITRDDIR=%{?_initrddir:%{_initrddir}}%{!?_initrddir:%{_sysconfdir}/init.d}
  $INITRDDIR/conman stop >/dev/null 2>&1 || :
  if [ -x /sbin/chkconfig ]; then /sbin/chkconfig --del conman; fi
fi

%postun
if [ "$1" -ge 1 ]; then
  INITRDDIR=%{?_initrddir:%{_initrddir}}%{!?_initrddir:%{_sysconfdir}/init.d}
  $INITRDDIR/conman condrestart >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%doc AUTHORS
%doc COPYING
%doc DISCLAIMER*
%doc FAQ
%doc NEWS
%doc README
%doc THANKS
%config(noreplace) %{_sysconfdir}/conman.conf
%config(noreplace) %{_sysconfdir}/[dls]*/conman
%{?_initrddir:%{_initrddir}}%{!?_initrddir:%{_sysconfdir}/init.d}/conman
%{_bindir}/*
%{_sbindir}/*
%{_prefix}/lib/*
%{_mandir}/*/*
