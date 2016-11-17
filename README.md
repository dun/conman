[![Build Status](https://travis-ci.org/dun/conman.svg?branch=master)](https://travis-ci.org/dun/conman)
[![Coverity Scan](https://scan.coverity.com/projects/dun-conman/badge.svg)](https://scan.coverity.com/projects/dun-conman)

### ConMan: The Console Manager

ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.

It supports:
- local serial devices
- remote terminal servers (via the telnet protocol)
- IPMI Serial-Over-LAN (via [FreeIPMI](https://www.gnu.org/software/freeipmi/))
- Unix domain sockets
- external processes (e.g., using Expect for telnet/ssh/ipmi-sol connections)

Its features include:
- logging (and optionally timestamping) console device output to file
- connecting to consoles in monitor (R/O) or interactive (R/W) mode
- allowing clients to share or steal console write privileges
- broadcasting client output to multiple consoles

Links:
- [Man Pages](../../wiki/Man-Pages)
- [License Information](../../wiki/License-Info)
- [Latest Release](../../releases/latest)
