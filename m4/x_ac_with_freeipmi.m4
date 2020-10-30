###############################################################################
# SYNOPSIS:
#   X_AC_WITH_FREEIPMI
#
# REQUIRES:
#   FreeIPMI v1.0.4 or later
#
# DESCRIPTION:
#   Check if FreeIPMI can/should be used.
#   Define FREEIPMIOBJS and FREEIPMILIBS accordingly.
###############################################################################

AC_DEFUN_ONCE([X_AC_WITH_FREEIPMI],
  [AC_ARG_WITH([freeipmi],
    [AS_HELP_STRING([--with-freeipmi],
      [use FreeIPMI's Serial-Over-LAN @{:@libipmiconsole@:}@])])
  AS_IF(
    [test "x${with_freeipmi}" != xno],
    [AC_CHECK_HEADER([ipmiconsole.h], [have_ipmiconsole_h=yes])
      AC_CHECK_LIB([ipmiconsole], [ipmiconsole_workaround_flags_is_valid],
        [have_libipmiconsole=yes])
      AS_IF(
        [test "x${have_ipmiconsole_h}" = xyes &&
          test "x${have_libipmiconsole}" = xyes],
        [have_freeipmi=yes])])
  AS_IF(
    [test "x${have_freeipmi}" = xyes],
    [AC_SUBST([FREEIPMIOBJS], [conmand-server-ipmi.\$\(OBJEXT\)])
      AC_SUBST([FREEIPMILIBS], [-lipmiconsole])
      AC_DEFINE([HAVE_IPMICONSOLE_H], [1],
        [Define to 1 if you have the <ipmiconsole.h> header file.])
      AC_DEFINE([HAVE_LIBIPMICONSOLE], [1],
        [Define to 1 if you have the `ipmiconsole' library @{:@-lipmiconsole@:}@.])
      AC_DEFINE([WITH_FREEIPMI], [1],
        [Define to 1 if using FreeIPMI's libipmiconsole.])],
    [test "x${with_freeipmi}" = xyes],
    [AC_MSG_FAILURE(
      [failed to locate FreeIPMI v1.0.4 or later for --with-freeipmi])])
  AC_MSG_CHECKING([whether to use FreeIPMI])
  AC_MSG_RESULT([${have_freeipmi=no}])
])
