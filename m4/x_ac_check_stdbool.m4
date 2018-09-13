###############################################################################
# SYNOPSIS:
#   X_AC_CHECK_STDBOOL
#
# DESCRIPTION:
#   Check whether <stdbool.h> is broken.
###############################################################################

AC_DEFUN_ONCE([X_AC_CHECK_STDBOOL],
  [AC_MSG_CHECKING([whether stdbool.h is broken])
  AC_COMPILE_IFELSE(
    [AC_LANG_PROGRAM([[#include <stdbool.h>]])],
    [x_ac_check_stdbool=no],
    [x_ac_check_stdbool=yes
      AC_DEFINE([BROKEN_STDBOOL], [1],
        [Define to 1 if the <stdbool.h> header file generates an error.])])
  AC_MSG_RESULT([${x_ac_check_stdbool=no}])
])
