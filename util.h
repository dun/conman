/******************************************************************************\
 *  $Id: util.h,v 1.4 2001/05/31 18:18:40 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <netinet/in.h>
#include <sys/types.h>


#ifndef MAX
#define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */
#ifndef MIN
#define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


char * create_string(const char *str);
/*
 *  Duplicates string (str) and returns a new string
 *    (or throws a fatal error if insufficient memory is available).
 *  Note that the caller is responsible for freeing this string.
 */

char * create_fmt_string(const char *fmt, ...);
/*
 *  Creates and returns a new string specified by the format-string (fmt)
 *    (or throws a fatal error if insufficient memory is available).
 *  Note that the caller is responsible for freeing this string.
 */

char * create_date_time_string(time_t t);
/*
 *  Creates and returns a new string with the specified date & time
 *    in the format "MM/DD/YYYY HH/MM/SS" in a thread-safe manner
 *    (or throws a fatal error if something strange happens).
 *  If no time is given (t=0), the current date & time is used.
 *  Note that the caller is responsible for freeing this string.
 */

char * create_time_string(time_t t);
/*
 *  Creates and returns a new string with the specified
 *    time in the format "HH/MM" in a thread-safe manner
 *    (or throws a fatal error if something strange happens).
 *  If no time is given (t=0), the current date & time is used.
 *  Note that the caller is responsible for freeing this string.
 */

char * create_time_delta_string(time_t t);
/*
 *  Creates and returns a new string indicating the time delta
 *    between time (t) and the current time, or NULL on error.
 *  The difference is broken-down into years, weeks, days, hours,
 *    minutes, and seconds.
 *  Note that the caller is responsible for freeing this string.
 */

void destroy_string(char *str);
/*
 *  Destroys the string (str).
 */

void set_descriptor_nonblocking(int fd);
/*
 *  Sets the file descriptor (fd) for non-blocking I/O.
 */

ssize_t read_n(int fd, void *buf, size_t n);
/*
 *  Reads up to (n) bytes from (fd) into (buf).
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */

ssize_t write_n(int fd, void *buf, size_t n);
/*
 *  Writes (n) bytes from (buf) to (fd).
 *  Returns the number of bytes written, or -1 on error.
 */

ssize_t read_line(int fd, void *buf, size_t maxlen);
/*
 *  Reads at most (maxlen-1) bytes up to a newline from (fd) into (buf).
 *  The (buf) is guaranteed to be NUL-terminated and will contain the
 *    newline if it is encountered within (maxlen-1) bytes.
 *  Returns the number of bytes read, 0 on EOF, or -1 on error.
 */

SigFunc * Signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */

char * get_hostname_via_addr(void *addr, char *buf, int len);
/*
 *  A thread-safe alternative to be used in place of gethostbyaddr().
 *  Resolves the socket address structure (struct in_addr *) (addr),
 *    placing the canonical hostname in the buffer of length (len)
 *    starting at (buf).
 *  Returns ptr to NUL-terminated result buffer if OK, or NULL on error.
 */

#ifndef HAVE_INET_PTON
int inet_pton(int family, const char *str, void *addr);
/*
 *  Convert from presentation format of an internet number in (str)
 *    to the binary network format, storing the result for interface
 *    type (family) in the socket address structure specified by (addr).
 *  Returns 1 if OK, 0 if input not a valid presentation format, -1 on error.
 */
#endif /* !HAVE_INET_PTON */

#ifndef HAVE_INET_NTOP
const char * inet_ntop(int family, const void *addr, char *str, size_t len);
/*   
 *  Convert an Internet address in binary network format for interface
 *    type (family) in the socket address structure specified by (addr),
 *    storing the result in the buffer (str) of length (len).
 *  Returns ptr to result buffer if OK, or NULL on error.
 */
#endif /* !HAVE_INET_NTOP */

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


#endif /* !_UTIL_H */
