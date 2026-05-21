AC_INIT([drachtio],[1.2],[daveh@beachdognet.com])
AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

AC_LANG(C++)

AX_CXX_COMPILE_STDCXX_11()
AX_CXX_COMPILE_STDCXX_17(noext, optional)

AM_CONDITIONAL(OSX, false)
AM_CONDITIONAL(LINUX, false)

AC_CANONICAL_HOST

case $host_os in
*darwin* | *rhapsody* | *macosx*)
     AM_CONDITIONAL(OSX, true)
     ;;
*)
     AM_CONDITIONAL(LINUX, true)
     ;;
esac

# optional linking with tcmalloc
AC_ARG_ENABLE([tcmalloc],
[  --enable-tcmalloc    Link with tcmalloc (requires libgoogle-perftools-dev)],
[case "${enableval}" in
  yes) tcmalloc=true ;;
  no)  tcmalloc=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-tcmalloc]) ;;
esac],[tcmalloc=false])
AM_CONDITIONAL([TCMALLOC], [test x$tcmalloc = xtrue])

# optional AddressSanitizer + UndefinedBehaviorSanitizer build
AC_ARG_ENABLE([asan],
[  --enable-asan        Build with -fsanitize=address,undefined (debugging only; incompatible with tcmalloc)],
[case "${enableval}" in
  yes) asan=true ;;
  no)  asan=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-asan]) ;;
esac],[asan=false])
AM_CONDITIONAL([ASAN], [test x$asan = xtrue])

if test x$asan = xtrue && test x$tcmalloc = xtrue; then
  AC_MSG_ERROR([--enable-asan and --enable-tcmalloc are mutually exclusive (both intercept malloc)])
fi

# Checks for programs.
AC_PROG_CXX
AC_PROG_CC

LIBCURL_CHECK_CONFIG

# Checks for libraries.

# Checks for header files.

# Checks for typedefs, structures, and compiler characteristics.

# Checks for library functions.
AC_CONFIG_FILES([Makefile])
AC_OUTPUT