Name:		conman
Version:	0.2.8
Release:	1%{?dist}

Summary:	ConMan: The Console Manager
Group:		Applications/System
License:	GPLv3+
URL:		https://dun.github.io/conman/
Source0:	https://github.com/dun/conman/releases/download/%{name}-%{version}/%{name}-%{version}.tar.xz

BuildRequires:	freeipmi-devel >= 1.0.4
BuildRequires:	tcp_wrappers-devel
BuildRequires:	systemd
Requires:	expect
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

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
%setup -q

%build
%configure
make %{?_smp_mflags}

%install
rm -rf "%{buildroot}"
mkdir -p "%{buildroot}"
make install DESTDIR="%{buildroot}"
rm -f %{buildroot}/%{_sysconfdir}/init.d/conman
rm -f %{buildroot}/%{_sysconfdir}/default/conman
rm -f %{buildroot}/%{_sysconfdir}/sysconfig/conman

%clean
rm -rf "%{buildroot}"

%post
%systemd_post conman.service

%preun
%systemd_preun conman.service

%postun
%systemd_postun_with_restart conman.service

%files
%{!?_licensedir:%global license %doc}
%license COPYING
%doc AUTHORS
%doc DISCLAIMER*
%doc FAQ
%doc KEYS
%doc NEWS
%doc PLATFORMS
%doc README
%doc THANKS
%config(noreplace) %{_sysconfdir}/conman.conf
%config(noreplace) %{_sysconfdir}/logrotate.d/conman
%{_bindir}/*
%{_sbindir}/*
%{_prefix}/lib/*
%{_mandir}/*/*
%{_unitdir}/conman.service
