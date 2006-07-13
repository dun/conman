#******************************************************************************
# ConMan AutoConf Configuration
#   Process this file with autoconf to produce a configure script.
#******************************************************************************
# $Id$
#******************************************************************************
# Copyright (C) 2001-2006 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory.
# Written by Chris Dunlap <cdunlap@llnl.gov>.
# UCRL-CODE-2002-009.
#
# This file is part of ConMan: The Console Manager.
# For details, see <http://home.gna.org/conman/>.
#
# This is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

# This is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.

# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
#******************************************************************************


dnl Prologue.
dnl
AC_INIT(common.h)
AC_CONFIG_AUX_DIR(auxdir)
AC_CONFIG_HEADER(config.h)


dnl Read package and version info from VERSION.
dnl
PROJECT="`perl -ne 'print,exit if s/^\s*NAME:\s*(\S*).*/\1/i' $srcdir/META`"
AC_DEFINE_UNQUOTED(PROJECT, "$PROJECT", [Define the project's name.])
AC_SUBST(PROJECT)
VERSION="`perl -ne 'print,exit if s/^\s*VERSION:\s*(\S*).*/\1/i' $srcdir/META`"
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
AC_SUBST(VERSION)
DATE="`perl -ne 'print,exit if s/^\s*DATE:\s*(\S*).*/\1/i' $srcdir/META`"
test -z "$DATE" && DATE="`date +%Y-%m-%d`"
AC_DEFINE_UNQUOTED(DATE, "$DATE", [Define the build date.])
AC_SUBST(DATE)


dnl Determine the system type.
dnl
AC_CANONICAL_HOST
AC_SUBST(host_cpu)
AC_DEFINE_UNQUOTED(HOST_CPU, "$host_cpu",
  [Define the canonical host CPU type.]
)
AC_SUBST(host_os)
AC_DEFINE_UNQUOTED(HOST_OS, "$host_os",
  [Define the canonical host OS type.]
)
AC_SUBST(host_vendor)
AC_DEFINE_UNQUOTED(HOST_VENDOR, "$host_vendor",
  [Define the canonical host vendor type.]
)


dnl Check for programs.
dnl
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
AC_PROG_RANLIB


dnl Check for libraries.
dnl
AC_CHECK_LIB(socket, socket)
dnl
dnl libnsl is only needed if libsocket is required; this test prevents it
dnl   from being linked into the Linux executable when it is not needed.
dnl
if test "$ac_cv_lib_socket_socket" = yes; then
  AC_CHECK_LIB(nsl, inet_addr)
fi


dnl Check for header files.
dnl


dnl Check for typedefs.
dnl
AC_CHECK_TYPES(socklen_t, [], [], [#include <sys/socket.h>])


dnl Check for structures.
dnl


dnl Check for compiler characteristics.
dnl
AC_C_BIGENDIAN


dnl Check for library functions.
dnl
AC_CHECK_FUNCS( \
  inet_aton \
  inet_ntop \
  inet_pton \
  localtime_r \
  strcasecmp \
)
AC_REPLACE_FUNCS( \
  strlcat \
  strlcpy \
)


dnl Check for system services.
dnl


dnl Check for system-specific stuff.
dnl


dnl Check for debug vs. production compilation.
dnl
AC_MSG_CHECKING(whether debugging is enabled)
AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--enable-debug], [enable debugging for development work]),
  [ case "$enableval" in
      yes) debug=yes ;;
      no)  debug=no ;;
      *)   AC_MSG_RESULT(doh!)
           AC_MSG_ERROR([bad value "$enableval" for --enable-debug]) ;;
    esac
  ]
)
if test "$debug" = yes; then
  if test -z "$ac_save_CFLAGS"; then
    test "$ac_cv_prog_cc_g" = yes && CFLAGS="-g"
    test "$GCC" = yes && CFLAGS="$CFLAGS -Wall"
  fi
else
  if test -z "$ac_save_CFLAGS"; then
    test "$GCC" = yes && CFLAGS="-O2 -Wall" || CFLAGS="-O"
  fi
  AC_DEFINE(NDEBUG, 1, [Define if you are building a production release.])
fi
AC_MSG_RESULT(${debug=no})


dnl Check for Gray Watson's Debug Malloc Library <http://dmalloc.com/>.
dnl
AC_MSG_CHECKING(whether to use the Debug Malloc Library)
AC_ARG_WITH(dmalloc,
  AC_HELP_STRING([--with-dmalloc], [use Gray Watson's dmalloc library]),
  [ case "$withval" in
      yes) dmalloc=yes ;;
      no)  dmalloc=no ;;
      *)   AC_MSG_RESULT(doh!)
           AC_MSG_ERROR([bad value "$withval" for --with-dmalloc]) ;;
    esac
  ]
)
AC_MSG_RESULT(${dmalloc=no})
if test "$dmalloc" = yes; then
  AC_HAVE_LIBRARY(dmalloc,
    [ AC_DEFINE_UNQUOTED(WITH_DMALLOC, 1,
        [Define if using the debug malloc library.]
      )
      LIBS="-ldmalloc $LIBS"
    ], [dmalloc=no]
  )
fi
dnl
dnl Note that <stdlib.h> must be included before <dmalloc.h>.
dnl Since it's ok to include it more than once, we do so here.
dnl
AH_BOTTOM(
[#ifdef WITH_DMALLOC
#  include <stdlib.h>
#  include <dmalloc.h>
#endif /* WITH_DMALLOC */]
)


dnl Check for TCP Wrappers.
dnl
AC_ARG_WITH(tcp-wrappers,
  AC_HELP_STRING([--with-tcp-wrappers], [use Wietse Venema's TCP Wrappers]),
  [ case "$withval" in
      yes) tcp_wrappers=yes ;;
      no)  tcp_wrappers=no ;;
      *)   AC_MSG_RESULT(doh!)
           AC_MSG_ERROR([bad value "$withval" for --with-tcp-wrappers]) ;;
    esac
  ]
)
if test "$tcp_wrappers" != no; then
  AC_CHECK_HEADER(tcpd.h, ac_have_tcpd_h=yes)
  AC_CHECK_LIB(wrap, main, ac_have_libwrap=yes)
  if test "$ac_have_tcpd_h" = yes -a "$ac_have_libwrap" = yes; then
    AC_DEFINE_UNQUOTED(WITH_TCP_WRAPPERS, 1, [Define if using TCP Wrappers.])
    LIBS="-lwrap $LIBS"
    tcp_wrappers=yes
  else
    tcp_wrappers=no
  fi
  dnl
  dnl If libnsl is already getting linked, there's no reason to do it again.
  dnl
  if test "$tcp_wrappers" = yes -a "$ac_cv_lib_nsl_inet_addr" != yes; then
    AC_CHECK_LIB(nsl, yp_get_default_domain)
  fi
fi
AC_MSG_CHECKING(whether to use TCP Wrappers)
AC_MSG_RESULT(${tcp_wrappers=no})


dnl Check for ConMan daemon conf file.
dnl Force a double shell-expansion of the CONF var.
dnl
CONMAN_CONF_TMP1="`eval echo ${sysconfdir}/conman.conf`"
CONMAN_CONF_TMP2="`echo $CONMAN_CONF_TMP1 | sed 's/^NONE/$ac_default_prefix/'`"
CONMAN_CONF="`eval echo $CONMAN_CONF_TMP2`"
AC_DEFINE_UNQUOTED(CONMAN_CONF, ["$CONMAN_CONF"],
  [Define the default configuration file of the ConMan daemon])
AC_SUBST(CONMAN_CONF)


dnl Check for ConMan daemon host name.
dnl
AC_MSG_CHECKING(for ConMan daemon host name)
AC_ARG_WITH(conman-host,
  AC_HELP_STRING([--with-conman-host=HOST],
    [default host name of daemon [[127.0.0.1]]]),
  [ if test -n "$withval" -a "$withval" != yes -a "$withval" != no; then
      CONMAN_HOST="$withval"
    fi
  ]
)
AC_MSG_RESULT(${CONMAN_HOST=127.0.0.1})
AC_DEFINE_UNQUOTED(CONMAN_HOST, "$CONMAN_HOST",
  [Define the default host name or IP address of the ConMan daemon])
AC_SUBST(CONMAN_HOST)


dnl Check for ConMan daemon port number.
dnl
AC_MSG_CHECKING(for ConMan daemon port number)
AC_ARG_WITH(conman-port,
  AC_HELP_STRING([--with-conman-port=PORT],
    [default port number of daemon [[7890]]]),
  [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
      CONMAN_PORT="$withval"
    fi
  ]
)
AC_MSG_RESULT(${CONMAN_PORT=7890})
AC_DEFINE_UNQUOTED(CONMAN_PORT, "$CONMAN_PORT",
  [Define the default port number of the ConMan daemon])
AC_SUBST(CONMAN_PORT)


dnl Check for random silliness.
dnl
AC_CACHE_CHECK([for a sense of humor], ac_cv_humor, ac_cv_humor=yes)


dnl Add defs to config.h.
dnl
AC_DEFINE(WITH_PTHREADS, 1, [Define if using Pthreads.])

AH_BOTTOM(
[
#ifdef WITH_PTHREADS
#  ifndef _REENTRANT
#    define _REENTRANT
#  endif /* _REENTRANT */
#endif /* WITH_PTHREADS */

/* Override default out_of_memory() macro definition. */
#include "log.h"
#define out_of_memory() (log_err(0, "Out of memory"), (void *) NULL)]
)


dnl Epilogue.
dnl
AC_OUTPUT_COMMANDS(echo "creating dependencies"; make depend >/dev/null)
AC_OUTPUT([
  Makefile
  etc/conman.init
  man/conman.1
  man/conman.conf.5
  man/conmand.8
])
