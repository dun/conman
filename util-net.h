/******************************************************************************\
 *  $Id: util-net.h,v 1.2 2001/09/11 12:31:24 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
\******************************************************************************/


#ifndef _UTIL_NET_H
#define _UTIL_NET_H


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <netdb.h>
#include <netinet/in.h>
#include <unistd.h>


#define HOSTENT_SIZE 8192		/* cf. Stevens UNPv1 11.15 p304 */


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


#endif /* !_UTIL_NET_H */
