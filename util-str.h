/******************************************************************************\
 *  $Id: util-str.h,v 1.2 2001/09/13 20:36:31 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_STR_H
#define _UTIL_STR_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <time.h>
#include <unistd.h>


char * create_string(const char *str);
/*
 *  Duplicates string (str) and returns a new string
 *    (or throws a fatal error if insufficient memory is available).
 *  Note that the caller is responsible for freeing this string.
 */

char * create_format_string(const char *fmt, ...);
/*
 *  Creates and returns a new string specified by the format-string (fmt)
 *    (or throws a fatal error if insufficient memory is available).
 *  Note that the caller is responsible for freeing this string.
 */

void destroy_string(char *str);
/*
 *  Destroys the string (str).
 */

size_t append_format_string(char *dst, size_t size, const char *fmt, ...);
/*
 *  Appends the string specified by the format-string (fmt) to a
 *    NUL-terminated string (dst) within a buffer of size (size).
 *  Note that (size) is the full size of (dst), not the space remaining.
 *  Returns the new length of the NUL-terminated string (dst),
 *    or -1 if truncation occurred.
 */

int substitute_string(char *dst, size_t dstlen, char *src, char c, char *sub);
/*
 *  Copies the (src) string into the (dst) buffer of size (dstlen),
 *    substituting all occurrences of char (c) with the string (sub).
 *  Returns the length of the resulting string in (dst),
 *    or -1 if truncation occurred.
 *  Always NUL terminates the dst string (unless dstlen == 0).
 */

char * create_long_time_string(time_t t);
/*
 *  Creates and returns a new string with the specified date & time
 *    in the format "MM/DD/YYYY HH:MM:SS ZONE" in a thread-safe manner
 *    (or throws a fatal error if something strange happens).
 *  If no time is given (t=0), the current date & time is used.
 *  Note that the caller is responsible for freeing this string.
 */

char * create_short_time_string(time_t t);
/*
 *  Creates and returns a new string with the specified
 *    time in the format "HH:MM" in a thread-safe manner
 *    (or throws a fatal error if something strange happens).
 *  If no time is given (t=0), the current time is used.
 *  Note that the caller is responsible for freeing this string.
 */

char * create_time_delta_string(time_t t);
/*
 *  Creates and returns a new string indicating the time delta
 *    between time (t) and the current time.
 *  The difference is broken-down into years, weeks, days, hours,
 *    minutes, and seconds.
 *  Note that the caller is responsible for freeing this string.
 */

struct tm * get_localtime(time_t *tPtr, struct tm *tmPtr);
/*
 *  Gets the local time in a thread-safe manner.
 *  The time struct (*tmPtr) is filled-in with the local time based on (*tPtr);
 *    if (*tPtr == 0), it is set with the current time.
 *  Returns the ptr to the time struct arg (tmPtr).
 */

#ifndef HAVE_STRLCAT
size_t strlcat(char *dst, const char *src, size_t siz);
/*
 *  Appends src to string dst of size siz (unlike strncat, siz is the
 *    full size of dst, not space left).  At most siz-1 characters
 *    will be copied.  Always NUL terminates (unless siz == 0).
 *  Returns strlen(src); if retval >= siz, truncation occurred.
 */
#endif /* !HAVE_STRLCAT */

#ifndef HAVE_STRLCPY
size_t strlcpy(char *dst, const char *src, size_t siz);
/*
 *  Copy src to string dst of size siz.  At most siz-1 characters
 *    will be copied.  Always NUL terminates (unless siz == 0).
 *  Returns strlen(src); if retval >= siz, truncation occurred.
 */
#endif /* !HAVE_STRLCPY */


#endif /* !_UTIL_STR_H */
