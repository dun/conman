# ConMan configure.ac

AC_PREREQ([2.61])
AC_INIT([ConMan], [0.3.0])
AC_SUBST([DATE], [2018-09-15])
AC_SUBST([CONMAN_CONF], [\${sysconfdir}/conman.conf])
AC_SUBST([CONMAN_HOST], [127.0.0.1])
AC_SUBST([CONMAN_PORT], [7890])

AC_CONFIG_AUX_DIR([build-aux])
m4_ifdef([dist-xz],
  [AM_INIT_AUTOMAKE([1.10 foreign dist-xz])],
  [AM_INIT_AUTOMAKE([1.10 foreign])])
AM_MAINTAINER_MODE
m4_ifdef([AM_SILENT_RULES],
  [AM_SILENT_RULES([yes])])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])
AC_CONFIG_SRCDIR([server.c])

# checks for programs
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MKDIR_P
AC_PROG_SED
X_AC_ENABLE_DEBUG

# checks for libraries
AC_CHECK_LIB([socket], [socket])
AS_IF([test "x${ac_cv_lib_socket_socket}" = xyes],
  [AC_SEARCH_LIBS([inet_addr], [nsl])])
X_AC_CHECK_PTHREADS
X_AC_WITH_FREEIPMI
X_AC_WITH_TCP_WRAPPERS

# checks for header files
AC_CHECK_HEADERS([ \
  paths.h \
  sys/inotify.h \
])
X_AC_CHECK_STDBOOL

# checks for types
AC_CHECK_TYPES([socklen_t], [], [],
[[#include <sys/types.h>
#include <sys/socket.h>]])

# checks for structures

# checks for compiler characteristics

# checks for library functions
AC_CHECK_FUNCS([ \
  inet_aton \
  inet_ntop \
  inet_pton \
  localtime_r \
  strcasecmp \
  strncasecmp \
  toint \
])
AC_REPLACE_FUNCS([ \
  strlcat \
  strlcpy \
])

# checks for system services

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
