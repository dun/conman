/******************************************************************************\
 *  $Id: util.h,v 1.10 2001/09/06 21:19:26 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_H
#define _UTIL_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <netinet/in.h>
#include <sys/types.h>
#include <time.h>


#ifndef MAX
#  define MAX(x,y) (((x) >= (y)) ? (x) : (y))
#endif /* !MAX */
#ifndef MIN
#  define MIN(x,y) (((x) <= (y)) ? (x) : (y))
#endif /* !MIN */


#define HOSTENT_SIZE 8192		/* cf. Stevens UNPv1e2 11.15 p304 */


typedef void * (*PthreadFunc)(void *);

typedef void SigFunc(int);


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

void destroy_string(char *str);
/*
 *  Destroys the string (str).
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

struct tm * get_localtime(time_t *t, struct tm *tm);
/*
 *  Gets the local time in a thread-safe manner.
 *  The time struct (*tm) is filled-in with the local time based on (*t);
 *    if (*t == 0), it is set with the current time.
 *  Returns the ptr to the time struct arg (tm).
 */

void set_descriptor_nonblocking(int fd);
/*
 *  Sets the file descriptor (fd) for non-blocking I/O.
 */

int get_read_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
 */

int get_readw_lock(int fd);
/*
 *  Obtain a read lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
 */

int get_write_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd).
 *  Returns 0 on success, or -1 if prevented from obtaining the lock.
 */

int get_writew_lock(int fd);
/*
 *  Obtain a write lock on the file specified by (fd),
 *    blocking until one becomes available.
 *  Returns 0 on success, or -1 on error.
 */

int release_lock(int fd);
/*
 *  Release a lock held on the file specified by (fd).
 *  Returns 0 on success, or -1 on error.
 */

pid_t is_read_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a read-lock
 *    (ie, if a write-lock is already being held on the file),
 *    returns the pid of the process holding the lock; o/w, returns 0.
 */

pid_t is_write_lock_blocked(int fd);
/*
 *  If a lock exists the would block a request for a write-lock
 *    (ie, if any lock is already being held on the file),
 *    returns the pid of a process holding the lock; o/w, returns 0.
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

SigFunc * posix_signal(int signum, SigFunc *f);
/*
 *  A wrapper for the historical signal() function to do things the Posix way.
 */

struct hostent * get_host_by_name(const char *name,
    char *buf, int buflen, int *h_err);
/*
 *  A portable thread-safe alternative to be used in place of gethostbyname().
 *  The result is stored in the buffer (buf) of length (buflen); if the buffer
 *    is too small to hold the result, NULL is returned with errno = ERANGE.
 *  Returns a ptr into (buf) on success; returns NULL on error, setting the
 *    (h_err) variable reference (if not NULL) to indicate the h_error.
 */

struct hostent * get_host_by_addr(const char *addr, int len, int type,
    char *buf, int buflen, int *h_err);
/*
 *  A portable thread-safe alternative to be used in place of gethostbyaddr().
 *  The result is stored in the buffer (buf) of length (buflen); if the buffer
 *    is too small to hold the result, NULL is returned with errno = ERANGE.
 *  Returns a ptr into (buf) on success; returns NULL on error, setting the
 *    (h_err) variable reference (if not NULL) to indicate the h_error.
 */

const char * host_strerror(int h_err);
/*
 *  Returns a string describing the error code (h_err) returned by
 *    get_host_by_name() or get_host_by_addr().
 */

int host_name_to_addr4(const char *name, struct in_addr *addr);
/*
 *  Converts the string (name) to an IPv4 address (addr).
 *  Returns 0 on success, or -1 on error.
 *  Note that this routine is thread-safe.
 */

char * host_addr4_to_name(const struct in_addr *addr, char *name, int len);
/*
 *  Converts an IPv4 address (addr) to a string residing in
 *    buffer (name) of length (len).
 *  Returns a ptr to the NULL-terminated string (name) on success,
 *    or NULL on error.
 *  Note that this routine is thread-safe.
 */

char * host_name_to_cname(const char *src, char *dst, int len);
/*
 *  Converts the hostname or IP address string (src) to the
 *    canonical name of the host residing in buffer (dst) of length (len).
 *  Returns a ptr to the NULL-terminated string (dst) on success,
 *    or NULL on error.
 *  Note that this routine is thread-safe.
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
