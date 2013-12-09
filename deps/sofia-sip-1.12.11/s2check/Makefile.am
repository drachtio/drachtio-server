#
# Makefile.am for "Sofia-SIP 2 Check", module and unit testing library
#
# Copyright (C) 2009 Nokia Corporation
# Contact: Pekka Pessi <pekka.pessi@nokia.com>
# Licensed under LGPL. See file COPYING.

# ----------------------------------------------------------------------
# Header paths

INCLUDES = 		$(INTERNAL_INCLUDES)

# ----------------------------------------------------------------------
# Build targets

if HAVE_CHECK
noinst_LIBRARIES = 	libs2.a
endif

# ----------------------------------------------------------------------
# Rules for building the targets

libs2_a_SOURCES = 	s2check.h s2tcase.c \
			s2base.h s2base.c \
			s2sip.c s2sip.h \
			s2_localinfo.h s2_localinfo.c \
			s2dns.h s2dns.c \
			s2util.h s2time.c

LDADD =

# ----------------------------------------------------------------------
# Install and distribution rules

EXTRA_DIST =		exit77.c $(libs2_a_SOURCES)

# ----------------------------------------------------------------------
# Tests

TESTS =

# ----------------------------------------------------------------------
# Sofia specific rules

include $(top_srcdir)/rules/sofia.am
