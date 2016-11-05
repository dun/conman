###############################################################################
# ConMan AutoConf Configuration
#   Process this file with autoconf to produce a configure script.
###############################################################################
# Written by Chris Dunlap <cdunlap@llnl.gov>.
# Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.
# Copyright (C) 2001-2007 The Regents of the University of California.
# UCRL-CODE-2002-009.
#
# This file is part of ConMan: The Console Manager.
# For details, see <https://dun.github.io/conman/>.
#
# ConMan is free software: you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option)
# any later version.
#
# ConMan is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# for more details.
#
# You should have received a copy of the GNU General Public License along
# with ConMan.  If not, see <http://www.gnu.org/licenses/>.
###############################################################################


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
AC_CHECK_HEADERS( \
  paths.h \
  sys/inotify.h \
)


dnl Check for typedefs.
dnl
AC_CHECK_TYPES(socklen_t, [], [], [
#include <sys/types.h>
#include <sys/socket.h>])


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
  strncasecmp \
  toint \
)
AC_REPLACE_FUNCS( \
  strlcat \
  strlcpy \
)


dnl Check for system services.
dnl


dnl Check for system-specific stuff.
dnl


dnl Check for broken <stdbool.h>.
dnl
AC_MSG_CHECKING(whether stdbool.h is broken)
AC_COMPILE_IFELSE(AC_LANG_PROGRAM([#include <stdbool.h>]),
  [ broken_stdbool=no ],
  [ broken_stdbool=yes
    AC_DEFINE(BROKEN_STDBOOL, 1,
      [Define if your <stdbool.h> header file generates an error directive.])
  ]
)
AC_MSG_RESULT(${broken_stdbool=no})


dnl Check for debug vs. production compilation.
dnl
AC_MSG_CHECKING(whether debugging is enabled)
AC_ARG_ENABLE(debug,
  AS_HELP_STRING([--enable-debug], [enable debugging for development work]),
  [ case "$enableval" in
      yes) debug=yes ;;
      no)  debug=no ;;
      *)   AC_MSG_RESULT(doh!)
           AC_MSG_ERROR([bad value "$enableval" for --enable-debug]) ;;
    esac
  ]
)
if test "$debug" = yes; then
  test -z "$ac_test_CFLAGS" && CFLAGS=
  test "$ac_cv_prog_cc_g" = yes && DEBUG_CFLAGS="-g"
  test "$GCC" = yes && DEBUG_CFLAGS="$DEBUG_CFLAGS -Wall -Wsign-compare"
else
  AC_DEFINE(NDEBUG, 1, [Define if you are building a production release.])
fi
AC_SUBST(DEBUG_CFLAGS)
AC_MSG_RESULT(${debug=no})


dnl Check for Gray Watson's Debug Malloc Library <http://dmalloc.com/>.
dnl
AC_MSG_CHECKING(whether to use the Debug Malloc Library)
AC_ARG_WITH(dmalloc,
  AS_HELP_STRING([--with-dmalloc], [use Gray Watson's dmalloc library]),
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
  AS_HELP_STRING([--with-tcp-wrappers], [use Wietse Venema's TCP Wrappers]),
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


dnl Check for FreeIPMI libraries
dnl
AC_ARG_WITH(freeipmi,
  AS_HELP_STRING([--with-freeipmi], [use FreeIPMI's Serial-Over-LAN console]),
  [ case "$withval" in
      yes) freeipmi=req ;;
      no)  freeipmi=no ;;
      *)   AC_MSG_RESULT(doh!)
           AC_MSG_ERROR([bad value "$withval" for --with-freeipmi]) ;;
    esac
  ]
)
dnl Require FreeIPMI 1.0.4 or later for fixed behavior in
dnl ipmiconsole_engine_submit() as well as the various
dnl ipmiconsole_FOO_is_valid() functions.
dnl
dnl Note that ipmiconsole_workaround_flags_is_valid() was added in 1.0.4.
dnl
if test "$freeipmi" != no; then
  ac_save_LIBS="$LIBS"
  AC_CHECK_HEADERS(ipmiconsole.h)
  AC_CHECK_LIB(ipmiconsole, ipmiconsole_workaround_flags_is_valid)
  LIBS="$ac_save_LIBS"
  if   test "$ac_cv_header_ipmiconsole_h" != yes; then : ;
  elif test "$ac_cv_lib_ipmiconsole_ipmiconsole_workaround_flags_is_valid" != yes; then : ;
  else
    AC_DEFINE_UNQUOTED(WITH_FREEIPMI, 1,
      [Define if using FreeIPMI's libipmiconsole.])
    freeipmi=yes
    IPMI_OBJS="server-ipmi.o"
    IPMI_LIBS="-lipmiconsole"
  fi
  test "$freeipmi" = req && freeipmi=failed
fi
AC_MSG_CHECKING(whether to use FreeIPMI's libipmiconsole)
AC_MSG_RESULT(${freeipmi=no})
if test "$freeipmi" = failed; then
  AC_MSG_ERROR([unable to locate FreeIPMI 1.0.4 or later])
fi
AC_SUBST(IPMI_OBJS)
AC_SUBST(IPMI_LIBS)


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
  AS_HELP_STRING([--with-conman-host=HOST],
    [default host name of daemon [127.0.0.1]]),
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
  AS_HELP_STRING([--with-conman-port=PORT],
    [default port number of daemon [7890]]),
  [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
      CONMAN_PORT="$withval"
    fi
  ]
)
AC_MSG_RESULT(${CONMAN_PORT=7890})
AC_DEFINE_UNQUOTED(CONMAN_PORT, "$CONMAN_PORT",
  [Define the default port number of the ConMan daemon])
AC_SUBST(CONMAN_PORT)


dnl Check how to link against Pthreads.
dnl
AC_MSG_CHECKING([how to link against pthreads])
LIBPTHREAD=""
ac_check_pthreads_libs_save="$LIBS"
for flag in -lpthread -pthread; do
  LIBS="$flag"
  AC_LINK_IFELSE([
    AC_LANG_PROGRAM(
      [[#include <pthread.h>]],
      [[pthread_join (0, 0);]]
    )],
    [ac_check_pthreads_libs="$flag"; break],
    [ac_check_pthreads_libs=FAILED]
  )
done
LIBS="$ac_check_pthreads_libs_save"
AC_MSG_RESULT($ac_check_pthreads_libs)
if test "$ac_check_pthreads_libs" = "FAILED"; then
  AC_MSG_FAILURE([cannot link against pthreads])
fi
LIBPTHREAD="$ac_check_pthreads_libs"
AC_SUBST(LIBPTHREAD)
dnl
AC_DEFINE([_REENTRANT], [1],
  [Define to 1 if you plan to link against multithreaded code.]
)
AC_DEFINE([_THREAD_SAFE], [1],
  [Define to 1 if you plan to link against multithreaded code.]
)
AC_DEFINE([WITH_PTHREADS], [1],
  [Define to 1 if using Pthreads.]
)


dnl Check for random silliness.
dnl
AC_CACHE_CHECK([for a sense of humor], ac_cv_humor, ac_cv_humor=yes)


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
