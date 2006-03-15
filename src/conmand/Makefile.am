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
	$(MISSING)

conmand_CPPFLAGS = \
	-I$(top_srcdir)/src/missing

MISSING = \
	$(top_srcdir)/src/missing/getopt.c \
	$(top_srcdir)/src/missing/getopt.h \
	$(top_srcdir)/src/missing/getopt1.c
