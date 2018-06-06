#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.59)
AC_INIT(drachtio, 1.0, daveh@beachdognet.com)
AM_INIT_AUTOMAKE([subdir-objects -Wno-portability])
AC_CONFIG_SRCDIR([src/main.cpp])
AC_CONFIG_HEADER([config.h])

m4_include([libcurl.m4])

AM_CONDITIONAL(OSX, false)
AM_CONDITIONAL(LINUX, false)

AC_CANONICAL_HOST

case $host_os in
*darwin* | *rhapsody* | *macosx*)
     AM_CONDITIONAL(OSX, true)
		
		 # thanks to bitcoin configure.ac
		 AC_CHECK_PROG([BREW],brew, brew)
		 if test x$BREW = xbrew; then
			 dnl These Homebrew packages may be keg-only, meaning that they won't be found
			 dnl in expected paths because they may conflict with system files. Ask
			 dnl Homebrew where each one is located, then adjust paths accordingly.
			 dnl It's safe to add these paths even if the functionality is disabled by
			 dnl the user (--without-wallet or --without-gui for example).

			 openssl_prefix=`$BREW --prefix openssl 2>/dev/null`
			 if test x$openssl_prefix != x; then
				 PKG_CONFIG_PATH="$openssl_prefix/lib/pkgconfig:$PKG_CONFIG_PATH"
				 export PKG_CONFIG_PATH
			 fi
		 fi
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
