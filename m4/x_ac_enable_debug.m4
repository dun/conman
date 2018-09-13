###############################################################################
# SYNOPSIS:
#   X_AC_ENABLE_DEBUG
#
# DESCRIPTION:
#   Add the "--enable-debug" option.  If enabled, DEBUGCFLAGS will be set and
#     appended to AM_CFLAGS.
#   The NDEBUG macro (used by assert) will be set accordingly.
#
# NOTES:
#   This macro must be placed after AC_PROG_CC or equivalent.
###############################################################################

AC_DEFUN_ONCE([X_AC_ENABLE_DEBUG],
  [AC_ARG_ENABLE([debug],
    [AS_HELP_STRING([--enable-debug],
      [enable debugging for code development])])
  AS_IF(
    [test "x${enable_debug}" = xyes],
    [AC_REQUIRE([AC_PROG_CC])
      # Clear configure's default CFLAGS when not explicitly set by user.
      AS_IF(
        [test "x${ac_env_CFLAGS_set}" = x],
        [CFLAGS=])
      [DEBUGCFLAGS="${DEBUGCFLAGS} -O0"]
      AS_IF(
        [test "x${ac_cv_prog_cc_g}" = xyes],
        [DEBUGCFLAGS="${DEBUGCFLAGS} -g"])
      AS_IF(
        [test "x${GCC}" = xyes],
        [DEBUGCFLAGS="${DEBUGCFLAGS} -Wall -Wsign-compare -pedantic"])
      AM_CFLAGS="${AM_CFLAGS} \$(DEBUGCFLAGS)"
      AC_SUBST([AM_CFLAGS])
      AC_SUBST([DEBUGCFLAGS])],
    [enable_debug=no
      AC_DEFINE([NDEBUG], [1],
        [Define to 1 to disable debug code for a production build.])])
  AC_MSG_CHECKING([whether debugging is enabled])
  AC_MSG_RESULT([${enable_debug}])
])
