Name:		conman
Version:	0.3.0
Release:	1%{?dist}

Summary:	ConMan: The Console Manager
Group:		Applications/System
License:	GPLv3+
URL:		https://dun.github.io/conman/
Source0:	https://github.com/dun/conman/releases/download/%{name}-%{version}/%{name}-%{version}.tar.xz

BuildRequires:	freeipmi-devel >= 1.0.4
BuildRequires:	systemd
Requires:	expect
Requires(post): systemd
Requires(preun): systemd
Requires(postun): systemd

%description
ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.

Supported console types:
- Local serial devices
- Remote terminal servers (via the telnet protocol)
- IPMI Serial-Over-LAN (via FreeIPMI's libipmiconsole)
- External processes (e.g., Expect)
- Unix domain sockets

Features:
- Mapping symbolic names to console devices
- Logging (and optionally timestamping) console output to file
- Connecting to a console in monitor (R/O) or interactive (R/W) mode
- Connecting to multiple consoles for broadcasting (W/O) client output
- Sharing a console session amongst multiple simultaneous clients
- Allowing clients to share or steal console "write" privileges
- Executing Expect scripts across multiple consoles in parallel

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
%{_datadir}/*
%{_mandir}/*/*
%{_unitdir}/conman.service
