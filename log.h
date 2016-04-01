/*****************************************************************************
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  Copyright (C) 2007-2016 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2001-2007 The Regents of the University of California.
 *  UCRL-CODE-2002-009.
 *
 *  This file is part of ConMan: The Console Manager.
 *  For details, see <https://dun.github.io/conman/>.
 *
 *  ConMan is free software: you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation, either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  ConMan is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with ConMan.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************/


#ifndef _LOG_H
#define _LOG_H

#if HAVE_CONFIG_H
#  include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <syslog.h>


/*  Global used by the daemonize routines to allow the grandchild process to
 *    return status back to the parent process.  This is set to a valid fd in
 *    begin_daemonize() and then cleared in end_daemonize().
 */
extern int log_daemonize_fd;


/*  DPRINTF((level, format, ...))
 *    A wrapper for debug_printf() so it can be removed from production code.
 */
#ifndef NDEBUG
#  define DPRINTF(args) debug_printf args
#else /* NDEBUG */
#  define DPRINTF(args)
#endif /* !NDEBUG */


void debug_printf(int level, const char *format, ...);
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

void log_set_err_pipe(int fd);
/*
 *  Sets the file-descriptor for the write-half of the daemonize pipe
 *    connecting the original parent process to the forked grandchild process
 *    under which the daemon will continue running.
 *  If set (ie, fd >= 0), log_err() will return an error status back to the
 *    original parent process.
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
