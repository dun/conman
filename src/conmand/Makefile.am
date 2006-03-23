##*****************************************************************************
## $Id$
##*****************************************************************************
## Process this file with automake to produce Makefile.in.
##*****************************************************************************

sbin_PROGRAMS = \
	conmand

conmand_SOURCES = \
	conmand.c \
	conf.c \
	conf.h \
	$(COMMON) \
	$(MISSING)

conmand_CPPFLAGS = \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/missing

COMMON = \
	$(top_srcdir)/src/common/log.c \
	$(top_srcdir)/src/common/log.h

MISSING = \
	$(top_srcdir)/src/missing/getopt.c \
	$(top_srcdir)/src/missing/getopt.h \
	$(top_srcdir)/src/missing/getopt1.c
