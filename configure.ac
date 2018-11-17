AC_INIT(drachtio, 1.2, daveh@beachdognet.com)
AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADER([config.h])

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
