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
 *****************************************************************************
 *  Refer to "util-str.h" for documentation on public functions.
 *****************************************************************************/


#if HAVE_CONFIG_H
#  include <config.h>
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
#include "util.h"
#include "wrapper.h"


#define MAX_STR_SIZE 1024


char * create_string(const char *str)
{
    char *p;

    if (!str) {
        return(NULL);
    }
    if (!(p = strdup(str))) {
        out_of_memory();
    }
    return(p);
}


char * create_format_string(const char *fmt, ...)
{
    char buf[MAX_STR_SIZE];
    va_list vargs;
    char *p;

    if (!fmt) {
        return(NULL);
    }
    va_start(vargs, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vargs);
    va_end(vargs);

    buf[sizeof(buf) - 1] = '\0';        /* ensure buf is NUL-terminated */

    if (!(p = strdup(buf))) {
        out_of_memory();
    }
    return(p);
}


int replace_string(char **dst, const char *src)
{
    if (!dst) {
        return(-1);
    }
    if (*dst) {
        free(*dst);
    }
    if (!(*dst = strdup(src))) {
        out_of_memory();
    }
    return(0);
}


void destroy_string(char *str)
{
    if (str) {
        free(str);
    }
    return;
}


int is_empty_string(const char *str)
{
    if (!str) {
        return(-1);
    }
    for (; *str; str++) {
        if (!isspace((int) *str)) {
            return(0);
        }
    }
    return(1);
}


int parse_string(char *src, char **dst_p, char **ptr_p, char *quote_p)
{
    char *p;
    char *q;
    char c = 0;

    if (!dst_p) {
        errno = EINVAL;
        return(-1);
    }
    if (!src || !ptr_p) {
        *dst_p = NULL;
        errno = EINVAL;
        return(-1);
    }
    if (*ptr_p == NULL) {
        *ptr_p = src;
    }
    for (p = *ptr_p; *p && isspace((int) *p); p++) {
        ;
    }
    if (*p == '\0') {
        *dst_p = *ptr_p = p;
        return(0);
    }
    for (q = p+1; *q; q++) {
        if ((*p == '"') || (*p == '\'')) {
            if ((*q == *p) && (isspace((int) *(q+1)) || (*(q+1) == '\0'))) {
                c = *p++;
                *q++ = '\0';
                break;
            }
            else if (*(q+1) == '\0') {
                errno = EIO;
                *dst_p = p;
                *ptr_p = q + 1;
                return(-1);
            }
        }
        else if (isspace((int) *q)) {
            *q++ = '\0';
            break;
        }
    }
    *dst_p = p;
    *ptr_p = q;
    if (quote_p) {
        *quote_p = c;
    }
    return(1);
}


int append_format_string(char *dst, size_t size, const char *fmt, ...)
{
    char *p;
    size_t num_left;
    size_t num_orig;
    va_list vargs;
    int n;

    if ((dst == NULL) || (fmt == NULL)) {
        errno = EINVAL;
        return(-1);
    }
    if (size == 0) {
        return(0);
    }
    p = dst;
    num_left = size;
    while ((*p != '\0') && (num_left > 0)) {
        p++;
        num_left--;
    }
    /*  If (num_left == 0), dst[] is full but requires null-termination.
     *  If (num_left == 1), dst[] is full but is already null-terminated.
     */
    if (num_left <= 1) {
        dst[size - 1] = '\0';
        return(-1);
    }
    num_orig = p - dst;
    assert(num_left + num_orig == size);

    va_start(vargs, fmt);
    n = vsnprintf(p, num_left, fmt, vargs);
    va_end(vargs);

    if ((n < 0) || ((size_t) n >= num_left)) {
        return(-1);
    }
    return((int) num_orig + n);
}


int substitute_string(char *dst, size_t dstlen, const char *src,
    char c, const char *sub)
{
    const char *p;
    char *q;
    int len;
    int n;

    if (!dst || (dstlen <= 0) || !src || !c) {
        errno = EINVAL;
        return (-1);
    }
    p = src;
    q = dst;
    len = dstlen;

    while (*p && (len > 0)) {
        if ((*p == '%') && (*(p+1) == c)) {
            if (sub) {
                n = strlcpy(q, sub, len);
                q += n;
                len -= n;
            }
            p += 2;
        }
        else {
            *q++ = *p++;
            len--;
        }
    }
    if (len > 0) {
        *q = '\0';
        return(dstlen - len);
    }
    else {
        dst[dstlen - 1] = '\0';
        return (-1);
    }
}


char * create_long_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 32;

    if (!(p = malloc(len))) {
        out_of_memory();
    }
    get_localtime(&t, &tm);

    if (strftime(p, len, "%Y-%m-%d %H:%M:%S %Z", &tm) == 0) {
        log_err(0, "strftime() failed");
    }
    return(p);
}


char * create_short_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 12;                 /* MM-DD HH:MM + NUL */

    if (!(p = malloc(len))) {
        out_of_memory();
    }
    get_localtime(&t, &tm);

    if (strftime(p, len, "%m-%d %H:%M", &tm) == 0) {
        log_err(0, "strftime() failed");
    }
    return(p);
}


char * create_time_delta_string(time_t t0, time_t t1)
{
    long n;
    int years, weeks, days, hours, minutes, seconds;
    char buf[25];

    if (t1 == (time_t) -1) {
        if (time(&t1) == (time_t) -1) {
            log_err(errno, "time() failed");
        }
    }
    n = difftime(t1, t0);
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

    if (years > 0) {
        n = snprintf(buf, sizeof(buf), "%dy%dw%dd%dh%dm%ds",
            years, weeks, days, hours, minutes, seconds);
    }
    else if (weeks > 0) {
        n = snprintf(buf, sizeof(buf), "%dw%dd%dh%dm%ds",
            weeks, days, hours, minutes, seconds);
    }
    else if (days > 0) {
        n = snprintf(buf, sizeof(buf), "%dd%dh%dm%ds",
            days, hours, minutes, seconds);
    }
    else if (hours > 0) {
        n = snprintf(buf, sizeof(buf), "%dh%dm%ds", hours, minutes, seconds);
    }
    else if (minutes > 0) {
        n = snprintf(buf, sizeof(buf), "%dm%ds", minutes, seconds);
    }
    else {
        n = snprintf(buf, sizeof(buf), "%ds", seconds);
    }
    assert((n >= 0) && ((size_t) n < sizeof(buf)));
    return(create_string(buf));
}


int write_time_string(time_t t, char *dst, size_t dstlen)
{
    struct tm tm;
    int n;

    if (dstlen <= 20) {                 /* "YYYY-MM-DD HH:MM:SS " + NUL */
        return(0);
    }
    get_localtime(&t, &tm);

    if (!(n = strftime(dst, dstlen, "%Y-%m-%d %H:%M:%S ", &tm))) {
        return(0);
    }
    assert(n == 20);
    return(n);
}


struct tm * get_localtime(time_t *tPtr, struct tm *tmPtr)
{
#if ! HAVE_LOCALTIME_R

    static pthread_mutex_t localtimeLock = PTHREAD_MUTEX_INITIALIZER;
    struct tm *tmTmpPtr;

#endif /* !HAVE_LOCALTIME_R */

    assert(tPtr != NULL);
    assert(tmPtr != NULL);

    if (*tPtr == 0) {
        if (time(tPtr) == (time_t) -1) {
            log_err(errno, "time() failed");
        }
    }

#if ! HAVE_LOCALTIME_R

    /*  localtime() is not thread-safe, so it is protected by a mutex.
     */
    x_pthread_mutex_lock(&localtimeLock);
    if (!(tmTmpPtr = localtime(tPtr))) {
        log_err(errno, "localtime() failed");
    }
    *tmPtr = *tmTmpPtr;
    x_pthread_mutex_unlock(&localtimeLock);

#else /* HAVE_LOCALTIME_R */

    if (!localtime_r(tPtr, tmPtr)) {
        log_err(errno, "localtime_r() failed");
    }

#endif /* !HAVE_LOCALTIME_R */

    return(tmPtr);
}


#if ! HAVE_STRCASECMP
int strcasecmp(const char *s1, const char *s2)
{
    const char *p, *q;

    p = s1;
    q = s2;
    while (*p && toupper((int) *p) == toupper((int) *q)) {
        p++, q++;
    }
    return(toupper((int) *p) - toupper((int) *q));
}
#endif /* !HAVE_STRCASECMP */


#if ! HAVE_STRNCASECMP
int strncasecmp(const char *s1, const char *s2, size_t n)
{
    const char *p, *q;

    if (!n) {
        return(0);
    }
    p = s1;
    q = s2;
    while (--n && *p && toupper((int) *p) == toupper((int) *q)) {
        p++, q++;
    }
    return(toupper((int) *p) - toupper((int) *q));
}
#endif /* !HAVE_STRNCASECMP */


#if ! HAVE_TOINT
int toint(int c)
{
/*  Returns the "weight" (0-15) of a hexadecimal digit 'c'.
 *
 *  Implementation from "C: A Reference Manual, 5e" by Harbison & Steele.
 */
    if (c >= '0' && c <= '9') {
        return(c - '0');
    }
    if (c >= 'A' && c <= 'F') {
        return(c - 'A' + 10);
    }
    if (c >= 'a' && c <= 'f') {
        return(c - 'a' + 10);
    }
    return(0);
}
#endif /* !HAVE_TOINT */
