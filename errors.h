/*****************************************************************************\
 *  $Id: errors.h,v 1.9 2002/03/29 05:39:52 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
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


#ifndef _ERRORS_H
#define _ERRORS_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>


/*  DPRINTF(fmt, args...)
 *    A debugging-printf that prints the format string.
 *
 *  LDPRINTF(fmt, args...)
 *    Another debugging-printf that prints the format string
 *      preceded with the file name and line number.
 */
#ifndef NDEBUG
#  define DPRINTF(fmt, args...) do \
     { fprintf(stderr, fmt , ## args); } while (0)
#  define LDPRINTF(fmt, args...) do \
     { fprintf(stderr, "%s:%d: " fmt, __FILE__, __LINE__ , ## args); } \
       while (0)
#else /* NDEBUG */
#  define DPRINTF(fmt, args...)
#  define LDPRINTF(fmt, args...)
#endif /* !NDEBUG */


int open_msg_log(char *filename);
/*
 *  DOCUMENT_ME
 */

void close_msg_log(void);
/*
 *  DOCUMENT_ME
 */

#define log_msg(level, fmt, args...) \
  do { fprintf(stderr, fmt, ##args); fprintf(stderr, "\n"); } while (0)
/*
 *  FIXME: Remove kludge macro once function is implemented.
 *
void log_msg(int priority, const char *fmt, ...);
 *
 *  DOCUMENT_ME
 */

void err_msg(int errnum, const char *fmt, ...);
/*
 *  Display fatal error message and exit.
 *  If the error is related to a failing system call,
 *    'errnum' specifies the non-zero return code (eg, errno).
 */

#ifndef NDEBUG
#  define err_msg(num, fmt, args...) \
     err_msg(num, "%s:%d: " fmt, __FILE__, __LINE__ , ## args)
#endif /* !NDEBUG */


#endif /* !_ERRORS_H */
