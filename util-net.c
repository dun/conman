/******************************************************************************\
 *  $Id: util-net.c,v 1.7 2001/09/20 23:12:32 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "util-net.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include "errors.h"
#include "util-str.h"
#include "util-net.h"
#include "wrapper.h"


#ifndef INET_ADDRSTRLEN
#  define INET_ADDRSTRLEN 16
#endif /* !INET_ADDRSTRLEN */


static pthread_mutex_t hostentLock = PTHREAD_MUTEX_INITIALIZER;


static int copy_hostent(const struct hostent *src, char *dst, int len);
static int verify_hostent(const struct hostent *src, const struct hostent *dst);


struct hostent * get_host_by_name(const char *name,
    char *buf, int buflen, int *h_err)
{
/*  gethostbyname() is not thread-safe, and there is no frelling standard
 *    for gethostbyname_r() -- the arg list varies from system to system!
 */
    struct hostent *hptr;
    int n = 0;

    assert(name);
    assert(buf);

    x_pthread_mutex_lock(&hostentLock);
    if ((hptr = gethostbyname(name)))
        n = copy_hostent(hptr, buf, buflen);
    if (h_err)
        *h_err = h_errno;
    x_pthread_mutex_unlock(&hostentLock);

    if (n < 0) {
        errno = ERANGE;
        return(NULL);
    }
    return(hptr ? (struct hostent *) buf : NULL);
}


struct hostent * get_host_by_addr(const char *addr, int len, int type,
    char *buf, int buflen, int *h_err)
{
/*  gethostbyaddr() is not thread-safe, and there is no frelling standard
 *    for gethostbyaddr_r() -- the arg list varies from system to system!
 */
    struct hostent *hptr;
    int n = 0;

    assert(addr);
    assert(buf);

    x_pthread_mutex_unlock(&hostentLock);
    if ((hptr = gethostbyaddr(addr, len, type)))
        n = copy_hostent(hptr, buf, buflen);
    if (h_err)
        *h_err = h_errno;
    x_pthread_mutex_lock(&hostentLock);

    if (n < 0) {
        errno = ERANGE;
        return(NULL);
    }
    return(hptr ? (struct hostent *) buf : NULL);
}


const char * host_strerror(int h_err)
{
    if (h_err == HOST_NOT_FOUND)
        return("Unknown host");
    else if (h_err == TRY_AGAIN)
        return("Transient host name lookup failure");
    else if (h_err == NO_RECOVERY)
        return("Unknown server error");
    else if ((h_err == NO_ADDRESS) || (h_err == NO_DATA))
        return("No address associated with name");
    return("Unknown error");
}


int host_name_to_addr4(const char *name, struct in_addr *addr)
{
    struct hostent *hptr;
    unsigned char buf[HOSTENT_SIZE];

    assert(name);
    assert(addr);

    if (!(hptr = get_host_by_name(name, buf, sizeof(buf), NULL)))
        return(-1);
    if (hptr->h_length > 4) {
        errno = ERANGE;
        return(-1);
    }
    memcpy(addr, hptr->h_addr_list[0], hptr->h_length);
    return(0);
}


char * host_addr4_to_name(const struct in_addr *addr, char *dst, int dstlen)
{
    struct hostent *hptr;
    unsigned char buf[HOSTENT_SIZE];

    assert(addr);
    assert(dst);

    if (!(hptr = get_host_by_addr((char *) addr, 4, AF_INET,
      buf, sizeof(buf), NULL)))
        return(NULL);
    if (strlen(hptr->h_name) >= dstlen) {
        errno = ERANGE;
        return(NULL);
    }
    strcpy(dst, hptr->h_name);
    return(dst);
}


char * host_name_to_cname(const char *src, char *dst, int dstlen)
{
    struct hostent *hptr;
    unsigned char buf[HOSTENT_SIZE];
    struct in_addr addr;

    assert(src);
    assert(dst);

    if (!(hptr = get_host_by_name(src, buf, sizeof(buf), NULL)))
        return(NULL);
    /*
     *  If 'src' is an ip-addr string, it will simply be copied to h_name.
     *    So, we need to perform a reverse query based on the in_addr
     *    in order to obtain the canonical name of the host.
     *  Besides, this additional query helps protect against DNS spoofing.
     */
    memcpy(&addr, hptr->h_addr_list[0], hptr->h_length);
    if (!(hptr = get_host_by_addr((char *) &addr, 4, AF_INET,
      buf, sizeof(buf), NULL)))
        return(NULL);
    if (strlen(hptr->h_name) >= dstlen) {
        errno = ERANGE;
        return(NULL);
    }
    strcpy(dst, hptr->h_name);
    return(dst);
}


static int copy_hostent(const struct hostent *src, char *dst, int len)
{
/*  Copies the (src) hostent struct (and all of its associated data)
 *    into the buffer (dst) of length (len).
 *  Returns 0 if the copy is successful, or -1 if the length of the buffer
 *    is not large enough to hold the result.
 */
    struct hostent *hptr;
    char *buf;
    int n;
    char **p, **q;

    assert(src);
    assert(dst);

    hptr = (struct hostent *) dst;
    buf = dst + sizeof(struct hostent);
    if ((len -= sizeof(struct hostent)) < 0)
        return(-1);
    hptr->h_addrtype = src->h_addrtype;
    hptr->h_length = src->h_length;

    /*  Copy h_name string.
     */
    hptr->h_name = buf;
    n = strlcpy(buf, src->h_name, len);
    buf += n + 1;			/* allow for trailing NUL char */
    if ((len -= n + 1) < 0)
        return(-1);

    /*  Reserve space for h_aliases[].
     */
    hptr->h_aliases = (char **) buf;
    for (p=src->h_aliases, q=hptr->h_aliases; *p; p++, q++) {;}
    buf = (char *) (q + 1);		/* allow for terminating NULL */
    if ((len -= buf - (char *) hptr->h_aliases) < 0)
        return(-1);

    /*  Copy h_aliases[] strings.
     */
    for (p=src->h_aliases, q=hptr->h_aliases; *p; p++, q++) {
        n = strlcpy(buf, *p, len);
        *q = buf;
        buf += n + 1;			/* allow for trailing NUL char */
        if ((len -= n + 1) < 0)
            return(-1);
    }
    *q = NULL;

    /*  Reserve space for h_addr_list[].
     */
    hptr->h_addr_list = (char **) buf;
    for (p=src->h_addr_list, q=hptr->h_addr_list; *p; p++, q++) {;}
    buf = (char *) (q + 1);		/* allow for terminating NULL */
    if ((len -= buf - (char *) hptr->h_addr_list) < 0)
        return(-1);

    /*  Copy h_addr_list[] in_addr structs.
     */
    for (p=src->h_addr_list, q=hptr->h_addr_list; *p; p++, q++) {
        if ((len -= src->h_length) < 0)
            return(-1);
        memcpy(buf, *p, src->h_length);
        *q = buf;
        buf += src->h_length;
    }
    *q = NULL;

    assert(verify_hostent(src, (struct hostent *) dst) >= 0);
    return(0);
}


static int verify_hostent(const struct hostent *src, const struct hostent *dst)
{
/*  Verifies that the src hostent struct has been successfully copied into dst.
 *  Returns 0 if the copy is good; o/w, returns -1.
 */
    char **p, **q;

    assert(src);
    assert(dst);

    if (!dst->h_name)
        return(-1);
    if (src->h_name == dst->h_name)
        return(-1);
    if (strcmp(src->h_name, dst->h_name))
        return(-1);
    if (src->h_addrtype != dst->h_addrtype)
        return(-1);
    if (src->h_length != dst->h_length)
        return(-1);
    for (p=src->h_aliases, q=dst->h_aliases; *p; p++, q++)
        if ((!q) || (p == q) || (strcmp(*p, *q)))
            return(-1);
    for (p=src->h_addr_list, q=dst->h_addr_list; *p; p++, q++)
        if ((!q) || (p == q) || (memcmp(*p, *q, src->h_length)))
            return(-1);
    return(0);
}


#ifndef HAVE_INET_PTON
int inet_pton(int family, const char *str, void *addr)
{
/*  cf. Stevens UNPv1 p72.
 */
    struct in_addr tmpaddr;

    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return(-1);
    }
#ifdef HAVE_INET_ATON
    if (!inet_aton(str, &tmpaddr))
        return(0);
#else /* !HAVE_INET_ATON */
    if ((tmpaddr.s_addr = inet_addr(str)) == -1)
        return(0);
#endif /* !HAVE_INET_ATON */

    memcpy(addr, &tmpaddr, sizeof(struct in_addr));
    return(1);
}
#endif /* !HAVE_INET_PTON */


#ifndef HAVE_INET_NTOP
const char * inet_ntop(int family, const void *addr, char *str, size_t len)
{
/*  cf. Stevens UNPv1 p72.
 */
    const unsigned char *p = (const unsigned char *) addr;
    char tmpstr[INET_ADDRSTRLEN];

    assert(str);

    if (family != AF_INET) {
        errno = EAFNOSUPPORT;
        return(NULL);
    }
    snprintf(tmpstr, sizeof(tmpstr), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
    if (strlen(tmpstr) >= len) {
        errno = ENOSPC;
        return(NULL);
    }
    strcpy(str, tmpstr);
    return(str);
}
#endif /* !HAVE_INET_NTOP */
