/*****************************************************************************\
 *  $Id: util-str.c,v 1.15.2.1 2003/09/26 18:05:29 dun Exp $
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
 *****************************************************************************
 *  Refer to "util-str.h" for documentation on public functions.
\*****************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "log.h"
#include "util-str.h"
#include "wrapper.h"


#define MAX_STR_SIZE 1024


char * create_string(const char *str)
{
    char *p;

    if (!str)
        return(NULL);
    if (!(p = strdup(str)))
        out_of_memory();
    return(p);
}


char * create_format_string(const char *fmt, ...)
{
    char buf[MAX_STR_SIZE];
    va_list vargs;
    char *p;

    if (!fmt)
        return(NULL);

    va_start(vargs, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vargs);
    va_end(vargs);

    buf[sizeof(buf) - 1] = '\0';        /* ensure buf is NUL-terminated */

    if (!(p = strdup(buf)))
        out_of_memory();
    return(p);
}


void destroy_string(char *str)
{
    if (str)
        free(str);
    return;
}


size_t append_format_string(char *dst, size_t size, const char *fmt, ...)
{
    char *p;
    int nAvail;
    int lenOrig;
    va_list vargs;
    int n;

    assert(dst != NULL);
    if (!fmt || !size)
        return(0);

    p = dst;
    nAvail = size;
    while (*p && (nAvail > 0))
        p++, nAvail--;

    /*  Assert (dst) was NUL-terminated.  If (nAvail == 0), no NUL was found.
     */
    assert(nAvail != 0);
    if (nAvail <= 1)                    /* dst is full, only room for NUL */
        return(-1);
    lenOrig = p - dst;

    va_start(vargs, fmt);
    n = vsnprintf(p, nAvail, fmt, vargs);
    va_end(vargs);

    if ((n < 0) || (n >= nAvail)) {
        dst[size - 1] = '\0';           /* ensure dst is NUL-terminated */
        return(-1);
    }
    return(lenOrig + n);
}


int substitute_string(char *dst, size_t dstlen, const char *src,
    char c, char *sub)
{
    const char *p;
    char *q;
    int n, m;

    assert(dst != NULL);
    if (!dstlen || !src)
        return(0);

    for (p=src, q=dst, n=dstlen; n>0 && p && *p; p++) {
        if (*p != c) {
            *q++ = *p;
            n--;
        }
        else if (sub) {
            m = strlcpy(q, sub, n);
            q += m;
            n -= m;
        }
    }

    if (n > 0) {
        *q = '\0';
        return(dstlen - n);
    }
    else {
        dst[dstlen - 1] = '\0';
        return(-1);
    }
}


char * create_long_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 25;                 /* YYYY-MM-DD HH:MM:SS ZONE + NUL */

    if (!(p = malloc(len)))
        out_of_memory();

    get_localtime(&t, &tm);

    if (strftime(p, len, "%Y-%m-%d %H:%M:%S %Z", &tm) == 0)
        log_err(0, "strftime() failed");

    return(p);
}


char * create_short_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 12;                 /* MM-DD HH:MM + NUL */

    if (!(p = malloc(len)))
        out_of_memory();

    get_localtime(&t, &tm);

    if (strftime(p, len, "%m-%d %H:%M", &tm) == 0)
        log_err(0, "strftime() failed");

    return(p);
}


char * create_time_delta_string(time_t t)
{
    time_t now;
    long n;
    int years, weeks, days, hours, minutes, seconds;
    char buf[25];

    if (time(&now) == (time_t) -1)
        log_err(errno, "time() failed");
    n = difftime(now, t);
    assert(n >= 0);

    seconds = n % 60;
    n /= 60;
    minutes = n % 60;
    n /= 60;
    hours = n % 24;
    n /= 24;
    days = n % 7;
    n /= 7;
    weeks = n % 52;
    n /= 52;
    years = n;

    if (years > 0)
        n = snprintf(buf, sizeof(buf), "%dy%dw%dd%dh%dm%ds",
            years, weeks, days, hours, minutes, seconds);
    else if (weeks > 0)
        n = snprintf(buf, sizeof(buf), "%dw%dd%dh%dm%ds",
            weeks, days, hours, minutes, seconds);
    else if (days > 0)
        n = snprintf(buf, sizeof(buf), "%dd%dh%dm%ds",
            days, hours, minutes, seconds);
    else if (hours > 0)
        n = snprintf(buf, sizeof(buf), "%dh%dm%ds", hours, minutes, seconds);
    else if (minutes > 0)
        n = snprintf(buf, sizeof(buf), "%dm%ds", minutes, seconds);
    else
        n = snprintf(buf, sizeof(buf), "%ds", seconds);

    assert((n >= 0) && (n < sizeof(buf)));
    return(create_string(buf));
}


int write_time_string(time_t t, char *dst, size_t dstlen)
{
    struct tm tm;
    int n;

    if (dstlen <= 20)                   /* "YYYY-MM-DD HH:MM:SS " + NUL */
        return(0);

    get_localtime(&t, &tm);

    if (!(n = strftime(dst, dstlen, "%Y-%m-%d %H:%M:%S ", &tm)))
        return(0);

    assert(n == 20);
    return(n);
}


struct tm * get_localtime(time_t *tPtr, struct tm *tmPtr)
{
#ifndef HAVE_LOCALTIME_R

    static pthread_mutex_t localtimeLock = PTHREAD_MUTEX_INITIALIZER;
    struct tm *tmTmpPtr;

#endif /* !HAVE_LOCALTIME_R */

    assert(tPtr != NULL);
    assert(tmPtr != NULL);

    if (*tPtr == 0) {
        if (time(tPtr) == (time_t) -1)
            log_err(errno, "time() failed");
    }

#ifndef HAVE_LOCALTIME_R

    /*  localtime() is not thread-safe, so it is protected by a mutex.
     */
    x_pthread_mutex_lock(&localtimeLock);
    if (!(tmTmpPtr = localtime(tPtr)))
        log_err(errno, "localtime() failed");
    *tmPtr = *tmTmpPtr;
    x_pthread_mutex_unlock(&localtimeLock);

#else /* HAVE_LOCALTIME_R */

    if (!localtime_r(tPtr, tmPtr))
        log_err(errno, "localtime_r() failed");

#endif /* !HAVE_LOCALTIME_R */

    return(tmPtr);
}


#ifndef HAVE_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
    const char *p, *q;

    p = s1;
    q = s2;
    while (*p && toupper((int) *p) == toupper((int) *q))
        p++, q++;
    return(toupper((int) *p) - toupper((int) *q));
}
#endif /* !HAVE_STRCASECMP */
