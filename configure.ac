AC_INIT([drachtio], [1.2], [daveh@drachtio.org])
AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

AC_LANG(C++)

AX_CXX_COMPILE_STDCXX_11()
AX_CXX_COMPILE_STDCXX_17(noext, optional)
if test "x$HAVE_CXX17" != "xyes"; then
  AX_CXX_COMPILE_STDCXX_14(noext, optional)
fi

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

# Optional linking with tcmalloc
AC_ARG_ENABLE([tcmalloc],
[  --enable-tcmalloc    Link with tcmalloc (requires libgoogle-perftools-dev)],
[case "${enableval}" in
  yes) tcmalloc=true ;;
  no)  tcmalloc=false ;;
  *) AC_MSG_ERROR([bad value ${enableval} for --enable-tcmalloc]) ;;
esac],[tcmalloc=false])
AM_CONDITIONAL([TCMALLOC], [test x$tcmalloc = xtrue])

# Debug or optimized build
AC_ARG_ENABLE([debug],
              [AS_HELP_STRING([--enable-debug],
                              [Enable debug build (default is no)])],
              [case "${enableval}" in
                yes) debug=true ;;
                no)  debug=false ;;
                *) AC_MSG_ERROR([bad value ${enableval} for --enable-debug]) ;;
               esac],
              [debug=false])

if test "$debug" = true; then
    CXXFLAGS="-O0 -g"
    CFLAGS="-O0 -g"
else
    CXXFLAGS="-O3 -g"
    CFLAGS="-O3 -g"
fi

AC_SUBST([CXXFLAGS])
AC_SUBST([CFLAGS])

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
