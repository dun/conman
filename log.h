/*****************************************************************************\
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman/>.
 *  
 *  ConMan is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/


#ifndef _LOG_H
#define _LOG_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <syslog.h>


/*  DPRINTF((level, format, ...))
 *    A wrapper for dprintf() allowing it to be removed from production code.
 */
#ifndef NDEBUG
#  define DPRINTF(args) dprintf args
#else /* NDEBUG */
#  define DPRINTF(args)
#endif /* !NDEBUG */


void dprintf(int level, const char *format, ...);
/*
 *  Similar to printf, except output is always to stderr and only done
 *    when 'level' is less than or equal to the "DEBUG" env var.
 */

void log_set_file(FILE *fp, int priority, int timestamp);
/*
 *  If (fp) is non-null, logging of messages at the (priority) level
 *    and higher to the specified file is enabled; o/w, logging to
 *    the previously-specified file is disabled.  If (timestamp) is
 *    non-zero, timestamps will be prepended to each message.
 */

void log_set_syslog(char *ident, int facility);
/*
 *  If (ident) is non-null, logging via syslog is enabled to (facility)
 *    using (ident) as the string which will be prepended to each message;
 *    o/w, logging via syslog is disabled.
 *  Note that only the trailing "filename" component of (ident) is used.
 */

void log_err(int errnum, const char *format, ...);
/*
 *  Generates a fatal-error message according to the printf-style (format)
 *    string and terminates program execution; if a non-zero (errnum) is
 *    specified, a string describing the error code will be appended to
 *    the message.
 *  During debug, messages are written to stderr; o/w, they are written
 *    to the syslog facility.
 */

void log_msg(int priority, const char *format, ...);
/*
 *  Generates a non-fatal message according to the printf-style (format)
 *    string.  The (priority) level is passed to syslog.
 *  During debug, messages are written to stderr; o/w, they are written
 *    to the syslog facility.
 */


#endif /* !_LOG_H */
