dnl Process this file with autoconf to produce a configure script.
dnl
dnl ConMan AutoConf Config Jazz
dnl   by Chris Dunlap <cdunlap@llnl.gov>
dnl
dnl $Id: configure.ac,v 1.2 2001/09/21 21:14:21 dun Exp $


dnl Prologue.
dnl
AC_INIT(common.h)
AC_CONFIG_AUX_DIR(auxdir)
AC_CONFIG_HEADER(config.h)
AC_PREFIX_DEFAULT(/usr)


dnl Read package and version info from VERSION.
dnl
PACKAGE="`sed -ne 's/^\(.*\)-\(.*\)/\1/p' $srcdir/VERSION`"
AC_DEFINE_UNQUOTED(PACKAGE, "$PACKAGE", [Define the package name.])
AC_SUBST(PACKAGE)
VERSION="`sed -ne 's/^\(.*\)-\(.*\)/\2/p' $srcdir/VERSION`"
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the package version.])
AC_SUBST(VERSION)


dnl Check for debug vs. production compilation.
dnl This must be done before AC_PROG_CC defines its own defaults.
dnl
AC_MSG_CHECKING(whether debugging is enabled)
AC_ARG_ENABLE(debug,
  AC_HELP_STRING([--enable-debug], [enable debugging for development work]),
  [case "${enableval}" in
    yes) debug=yes ;;
    no)  debug=no ;;
    *)   AC_MSG_RESULT(doh!)
         AC_MSG_ERROR([bad value "${enableval}" for --enable-debug]) ;;
   esac]
)
if test "$debug" = yes; then
  CFLAGS="${CFLAGS--ggdb -Wall}"
else
  CFLAGS="${CFLAGS--O3}"
  LDFLAGS="${LDFLAGS--s}"
  AC_DEFINE(NDEBUG, 1, [Define if you are building a production release.])
fi
AC_MSG_RESULT(${debug-no})


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
AC_SUBST(host_alias)
AC_DEFINE_UNQUOTED(HOST_ALIAS, "$host_alias",
  [Define the canonical host alias.]
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
)
AC_REPLACE_FUNCS( \
  strlcat \
  strlcpy \
)


dnl Check for system services.
dnl


dnl Check for system-specific stuff.
dnl


dnl Check for Gray Watson's Debug Malloc Library <http://dmalloc.com/>.
dnl
AC_MSG_CHECKING(whether to use the Debug Malloc Library)
AC_ARG_WITH(dmalloc,
  AC_HELP_STRING([--with-dmalloc], [use Gray Watson's dmalloc library]),
  [case "${withval}" in
    yes) dmalloc=yes ;;
    no)  dmalloc=no ;;
    *)   AC_MSG_RESULT(doh!)
         AC_MSG_ERROR([bad value "${withval}" for --with-dmalloc]) ;;
   esac]
)
AC_MSG_RESULT(${dmalloc-no})
if test "$dmalloc" = yes; then
  AC_CHECK_LIB(dmalloc, main,
    [AC_DEFINE_UNQUOTED(WITH_DMALLOC, 1,
      [Define if using the debug malloc library.])
    LIBS="$LIBS -ldmalloc"],
    [dmalloc=no]
  )
fi
AH_BOTTOM(
[#ifdef WITH_DMALLOC
#  include <dmalloc.h>
#endif /* WITH_DMALLOC */]
)


dnl Check for random silliness.
dnl
AC_CACHE_CHECK([for a sense of humor], ac_cv_humor, ac_cv_humor=yes)


dnl Add defs to config.h.
dnl
AC_DEFINE(USE_PTHREADS, 1, [Define if using Pthreads.])


dnl Epilogue.
dnl
AC_OUTPUT_COMMANDS(echo "creating dependencies"; make depend >/dev/null)
AC_OUTPUT([
  Makefile
  conman.spec
])
