##*****************************************************************************
## $Id$
##*****************************************************************************
## Process this file with autoconf to produce a configure script.
##*****************************************************************************
## This file is part of ConMan, a remote console management program.
## For details, see <http://www.llnl.gov/linux/conman/>.
##
## Copyright (C) 2001-2006 The Regents of the University of California.
## Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
## Written by Chris Dunlap <cdunlap@llnl.gov>.
## UCRL-CODE-2002-009.
##
## This is free software; you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 2 of the License, or
## (at your option) any later version.
##
## This is distributed in the hope that it will be useful, but WITHOUT
## ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
## FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
## for more details.
##
## You should have received a copy of the GNU General Public License along
## with this program; if not, write to the Free Software Foundation, Inc.,
## 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
##*****************************************************************************

##
# Prologue.
##
AC_INIT
X_AC_META
AC_CONFIG_AUX_DIR([config])
AC_CONFIG_SRCDIR([src/conmand/conmand.c])
X_AC_CANONICAL
X_AC_HUMOR

##
# Automake support.
##
AM_INIT_AUTOMAKE([$PACKAGE], [$VERSION], [gnu no-define])
AC_CONFIG_HEADERS([config/config.h])
AM_MAINTAINER_MODE

##
# Checks for programs.
##
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_MAKE_SET
X_AC_DEBUG

##
# Epilogue.
##
AC_CONFIG_FILES( \
  Makefile \
  src/Makefile \
  src/conman/Makefile \
  src/conmand/Makefile \
)
AC_OUTPUT
