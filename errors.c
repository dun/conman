/*****************************************************************************\
 *  $Id: errors.c,v 1.9 2002/03/14 03:37:00 dun Exp $
 *****************************************************************************
 *  Copyright (C) 2001-2002 The Regents of the University of California.
 *  Written by Chris Dunlap <cdunlap@llnl.gov>.
 *  UCRL-CODE-2002-009.
 *  
 *  This file is part of ConMan, a remote console management program.
 *  For details, see <http://www.llnl.gov/linux/conman.html>.
 *  
 *  ConMan was produced at the University of California, Lawrence Livermore
 *  National Laboratory (UC LLNL) under contract no. W-7405-ENG-48
 *  (Contract 48) between the U.S. Department of Energy (DOE) and The Regents
 *  of the University of California (University) for the operation of UC LLNL.
 *  The rights of the Federal Government are reserved under Contract 48
 *  subject to the restrictions agreed upon by the DOE and University as
 *  allowed under DOE Acquisition Letter 97-1.
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
 *****************************************************************************
 *  Refer to "errors.h" for documentation on public functions.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"


#ifndef MAX_LINE
#  define MAX_LINE 1024
#endif /* !MAX_LINE */


int open_msg_log(char *filename)
{
    /*  FIXME: NOT_IMPLEMENTED_YET
     */
    return(0);
}


void close_msg_log(void)
{
    /*  FIXME: NOT_IMPLEMENTED_YET
     */
    return;
}


#undef log_msg
void log_msg(int level, const char *fmt, ...)
{
    /*  FIXME: NOT_IMPLEMENTED_YET
     */
    return;
}


#undef err_msg
void err_msg(int errnum, const char *fmt, ...)
{
/*  FIXME: Replace snprintf()'s with append_format_string().
 */
    char buf[MAX_LINE];
    char *ptr;
    int len;
    int n;
    int overflow = 0;
    va_list vargs;

    /* Reserve chars in buf for terminating with \n and \0.
     */
    ptr = buf;
    len = sizeof(buf) - 1;

    if (len > 0) {
        n = snprintf(ptr, len, "ERROR: ");
        if (n < 0 || n >= len)
            overflow = 1;
        else
            ptr += n, len -= n;
    }

    if (!overflow) {
        va_start(vargs, fmt);
        n = vsnprintf(ptr, len, fmt, vargs);
        va_end(vargs);
        if (n < 0 || n >= len)
            overflow = 1;
        else
            ptr += n, len -= n;
    }

    if (errnum && !overflow) {
        n = snprintf(ptr, len, ": %s", strerror(errnum));
        if (n < 0 || n >= len)
            overflow = 1;
        else
            ptr += n;
    }

    if (overflow)
        ptr = buf + sizeof(buf) - 2;
    *ptr++ = '\n';
    *ptr++ = '\0';

    fflush(stdout);
    fputs(buf, stderr);
    fflush(stderr);

#ifndef NDEBUG
    abort();
#endif /* !NDEBUG */
    exit(1);
}
