###############################################################################
# SYNOPSIS:
#   X_AC_WITH_TCP_WRAPPERS
#
# DESCRIPTION:
#   Check if TCP Wrappers can/should be used.
#   Define TCPWRAPPERSLIBS accordingly.
###############################################################################

AC_DEFUN_ONCE([X_AC_WITH_TCP_WRAPPERS],
  [AC_ARG_WITH([tcp-wrappers],
    [AS_HELP_STRING([--with-tcp-wrappers],
      [use Wietse Venema's TCP Wrappers @{:@libwrap@:}@])])
  AS_IF(
    [test "x${with_tcp_wrappers}" != xno],
    [AC_CHECK_HEADER([tcpd.h], [have_tcpd_h=yes])
      AC_CHECK_LIB([wrap], [hosts_ctl], [have_libwrap=yes])
      AS_IF(
        [test "x${have_tcpd_h}" = xyes && test "x${have_libwrap}" = xyes],
        [have_tcp_wrappers=yes])])
  AS_IF(
    [test "x${have_tcp_wrappers}" = xyes],
    [TCPWRAPPERSLIBS="-lwrap"
      _x_ac_with_tcp_wrappers_libs_save="${LIBS}"
      LIBS=
      AC_SEARCH_LIBS([inet_addr], [nsl])
      if test "x${LIBS}" != x; then
        TCPWRAPPERSLIBS="${TCPWRAPPERSLIBS} ${LIBS}"
      fi
      LIBS="${_x_ac_with_tcp_wrappers_libs_save}"
      AC_SUBST([TCPWRAPPERSLIBS], [${TCPWRAPPERSLIBS}])
      AC_DEFINE([HAVE_TCPD_H], [1],
        [Define to 1 if you have the <tcpd.h> header file.])
      AC_DEFINE([HAVE_LIBWRAP], [1],
        [Define to 1 if you have the `wrap' library @{:@-lwrap@:}@.])
      AC_DEFINE([WITH_TCP_WRAPPERS], [1],
        [Define to 1 if using TCP Wrappers.])],
    [test "x${with_tcp_wrappers}" = xyes],
    [AC_MSG_FAILURE([failed check for --with-tcp-wrappers])])
  AC_MSG_CHECKING([whether to use TCP Wrappers])
  AC_MSG_RESULT([${have_tcp_wrappers=no}])
])
