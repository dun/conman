/******************************************************************************\
 *  $Id: errors.c,v 1.6 2001/08/14 23:16:47 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "errors.h" for documentation on public functions.
\******************************************************************************/


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
    /*  FIX_ME: NOT_IMPLEMENTED_YET
     */
    return(0);
}


void close_msg_log(void)
{
    /*  FIX_ME: NOT_IMPLEMENTED_YET
     */
    return;
}


void log_msg(int level, const char *fmt, ...)
{
    /*  FIX_ME: NOT_IMPLEMENTED_YET
     */
    return;
}


void err_msg(int errnum, const char *fmt, ...)
{
/*  FIX_ME: Replace snprintf()'s with append_format_string().
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
    exit(1);
}
