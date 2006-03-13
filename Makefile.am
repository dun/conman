##*****************************************************************************
## $Id$
##*****************************************************************************
## Process this file with automake to produce Makefile.in.
##*****************************************************************************

ACLOCAL_AMFLAGS =               -I config
CONFIG_STATUS_DEPENDENCIES =    META
EXTRA_DIST =                    AUTHORS COPYING ChangeLog DISCLAIMER \
                                INSTALL NEWS README conman.spec
SUBDIRS =                       src

distclean-local::
	-rm -fr autom4te*.cache
	-rm -fr autoscan.* configure.scan
