AC_INIT([drachtio],[1.2],[daveh@beachdognet.com])
AC_CONFIG_AUX_DIR([.])
AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADERS([config.h])

AC_CONFIG_MACRO_DIR([m4])

AC_LANG(C++)

AX_CXX_COMPILE_STDCXX_11()
AX_CXX_COMPILE_STDCXX_17(noext, optional)

AC_CANONICAL_HOST
project_root=`cd "$srcdir"; pwd`
AC_SUBST(project_root)

AC_CHECK_FILE([$project_root/deps/boost/libs], 
              [have_dep_boost="yes"], 
              [have_dep_boost="no"])
AM_CONDITIONAL([DEP_BOOST], [test "x$have_dep_boost" = "xyes"])

if test "x$have_dep_boost" = "xyes"; then
  AC_MSG_NOTICE([Boost libraries found locally])
else
  AC_MSG_NOTICE([Boost libraries not found locally, building with system Boost])
fi

AM_CONDITIONAL(OSX, false)
AM_CONDITIONAL(LINUX, false)

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
