###############################################################################
#  SYNOPSIS:
#    X_AC_RUNSTATEDIR
#
#  DESCRIPTION:
#    If "runstatedir" is not set, default to "${localstatedir}/run".
###############################################################################

AC_DEFUN([X_AC_RUNSTATEDIR],
[
  AS_IF(
    [test "x${runstatedir}" = x],
    [AC_SUBST([runstatedir], ['${localstatedir}/run'])])
])
