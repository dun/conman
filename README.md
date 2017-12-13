[![GitHub Release](https://img.shields.io/github/release/dun/conman.svg)](https://github.com/dun/conman/releases/latest)
[![Packaging status](https://repology.org/badge/tiny-repos/conman.svg)](https://repology.org/metapackage/conman)
[![Build Status](https://travis-ci.org/dun/conman.svg?branch=master)](https://travis-ci.org/dun/conman)
[![Coverity Scan](https://scan.coverity.com/projects/dun-conman/badge.svg)](https://scan.coverity.com/projects/dun-conman)

### ConMan: The Console Manager

ConMan is a serial console management program designed to support a large
number of console devices and simultaneous users.

Supported console types:
- Local serial devices
- Remote terminal servers (via the telnet protocol)
- IPMI Serial-Over-LAN (via [FreeIPMI's](https://www.gnu.org/software/freeipmi/) libipmiconsole)
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

Links:
- [Man Pages](../../wiki/Man-Pages)
- [License Information](../../wiki/License-Info)
- [Verifying Releases](../../wiki/Verifying-Releases)
- [Latest Release](../../releases/latest)
