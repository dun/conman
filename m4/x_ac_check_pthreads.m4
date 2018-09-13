###############################################################################
# SYNOPSIS:
#   X_AC_CHECK_PTHREADS
#
# DESCRIPTION:
#   Check how to link with Pthreads.  Define PTHREADLIBS accordingly.
#   Also define both _REENTRANT and _THREAD_SAFE which may be needed when
#     linking multithreaded code.
###############################################################################

AC_DEFUN_ONCE([X_AC_CHECK_PTHREADS],
  [AC_CACHE_CHECK(
    [how to link with pthreads],
    [x_ac_cv_check_pthreads],
    [PTHREADLIBS=""
      _x_ac_check_pthreads_libs_save="${LIBS}"
      for _x_ac_check_pthreads_flag in -lpthread -pthread; do
        LIBS="${_x_ac_check_pthreads_flag}"
        AC_LINK_IFELSE(
          [AC_LANG_PROGRAM(
            [[#include <pthread.h>]],
            [[pthread_join (0, 0);]])],
          [x_ac_cv_check_pthreads="${_x_ac_check_pthreads_flag}"; break],
          [x_ac_cv_check_pthreads=failed])
      done
      LIBS="${_x_ac_check_pthreads_libs_save}"])
  AS_IF(
    [test "x${x_ac_cv_check_pthreads}" = xfailed],
    AC_MSG_FAILURE([failed to link with pthreads]))
  PTHREADLIBS="${x_ac_cv_check_pthreads}"
  AC_SUBST([PTHREADLIBS])
  AC_DEFINE([_REENTRANT], [1], [Define to 1 if linking multithreaded code.])
  AC_DEFINE([_THREAD_SAFE], [1], [Define to 1 if linking multithreaded code.])
])
