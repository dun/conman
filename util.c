/******************************************************************************\
 *  $Id: util.c,v 1.14 2001/09/06 21:19:26 dun Exp $
 *    by Chris Dunlap <cdunlap@llnl.gov>
 ******************************************************************************
 *  Refer to "util.h" for documentation on public functions.
\******************************************************************************/


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif /* HAVE_CONFIG_H */

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include "errors.h"
#include "util.h"


#define MAX_STR_SIZE 1024

#ifndef INET_ADDRSTRLEN
#  define INET_ADDRSTRLEN 16
#endif /* !INET_ADDRSTRLEN */


static int get_file_lock(int fd, int cmd, int type);
static pid_t test_file_lock(int fd, int type);
static int copy_hostent(const struct hostent *src, char *dst, int len);
static int verify_hostent(const struct hostent *src, const struct hostent *dst);


char * create_string(const char *str)
{
    char *p;

    if (!str)
        return(NULL);
    if (!(p = strdup(str)))
        err_msg(0, "Out of memory");
    return(p);
}


char * create_format_string(const char *fmt, ...)
{
    va_list vargs;
    int n, len;
    char *p;

    if (!fmt)
        return(NULL);

    va_start(vargs, fmt);
    if ((len = vsnprintf(p, 0, fmt, vargs)) < 0)
        len = MAX_STR_SIZE;
    else				/* C99 standard returns needed strlen */
        len++;				/* reserve space for terminating NUL */
    if (!(p = (char *) malloc(len)))
        err_msg(0, "Out of memory");
    n = vsnprintf(p, len, fmt, vargs);
    va_end(vargs);

    if (n < 0 || n >= len)
        p[len - 1] = '\0';		/* ensure str is NUL-terminated */
    return(p);
}


size_t append_format_string(char *dst, size_t size, const char *fmt, ...)
{
    char *p;
    int nAvail;
    int lenOrig;
    va_list vargs;
    int n;

    assert(dst);
    if (!fmt || !size)
        return(0);

    p = dst;
    nAvail = size;
    while (*p && (nAvail > 0))
        p++, nAvail--;

    /*  Assert (dst) was NUL-terminated.  If (nAvail == 0), no NUL was found.
     */
    assert(nAvail != 0);
    if (nAvail <= 1)			/* dst is full, only room for NUL */
        return(-1);
    lenOrig = p - dst;

    va_start(vargs, fmt);
    n = vsnprintf(p, nAvail, fmt, vargs);
    va_end(vargs);

    if ((n < 0) || (n >= nAvail)) {
        dst[size - 1] = '\0';		/* ensure dst is NUL-terminated */
        return(-1);
    }
    return(lenOrig + n);
}


int substitute_string(char *dst, size_t dstlen, char *src, char c, char *sub)
{
    char *p, *q;
    int n, m;

    assert(dst);
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


void destroy_string(char *str)
{
    if (str)
        free(str);
    return;
}


char * create_long_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 25;			/* MM/DD/YYYY HH:MM:SS ZONE + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%m/%d/%Y %H:%M:%S %Z", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_short_time_string(time_t t)
{
    char *p;
    struct tm tm;
    const int len = 6;			/* HH:MM + NUL */

    if (!(p = malloc(len)))
        err_msg(0, "Out of memory");

    get_localtime(&t, &tm);

    if (strftime(p, len, "%H:%M", &tm) == 0)
        err_msg(0, "strftime() failed");

    return(p);
}


char * create_time_delta_string(time_t t)
{
    time_t now;
    long n;
    int years, weeks, days, hours, minutes, seconds;
    char buf[25];

    if (time(&now) == ((time_t) -1))
        err_msg(errno, "time() failed -- What time is it?");
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


struct tm * get_localtime(time_t *t, struct tm *tm)
{
#ifndef HAVE_LOCALTIME_R

    int rc;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct tm *ptm;

#endif /* !HAVE_LOCALTIME_R */

    assert(t);
    assert(tm);

    if (*t == 0) {
        if (time(t) == ((time_t) -1))
            err_msg(errno, "time() failed -- What time is it?");
    }

#ifndef HAVE_LOCALTIME_R

    /*  localtime() is not thread-safe, so it is protected by a mutex.
     */
    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");
    if (!(ptm = localtime(t)))
        err_msg(errno, "localtime() failed");
    *tm = *ptm;
    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

#else /* HAVE_LOCALTIME_R */

    if (!localtime_r(t, tm))
        err_msg(errno, "localtime_r() failed");

#endif /* !HAVE_LOCALTIME_R */

    return(tm);
}


void set_descriptor_nonblocking(int fd)
{
    int fval;

    if ((fval = fcntl(fd, F_GETFL, 0)) < 0)
        err_msg(errno, "fcntl(F_GETFL) failed");
    if (fcntl(fd, F_SETFL, fval | O_NONBLOCK) < 0)
        err_msg(errno, "fcntl(F_SETFL) failed");
    return;
}


int get_read_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_RDLCK));
}


int get_readw_lock(int fd)
{
    return(get_file_lock(fd, F_SETLKW, F_RDLCK));
}


int get_write_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_WRLCK));
}


int get_writew_lock(int fd)
{
    return(get_file_lock(fd, F_SETLKW, F_WRLCK));
}


int release_lock(int fd)
{
    return(get_file_lock(fd, F_SETLK, F_UNLCK));
}


pid_t is_read_lock_blocked(int fd)
{
    return(test_file_lock(fd, F_RDLCK));
}


pid_t is_write_lock_blocked(int fd)
{
    return(test_file_lock(fd, F_WRLCK));
}


static int get_file_lock(int fd, int cmd, int type)
{
    struct flock lock;

    assert(fd >= 0);

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;

    return(fcntl(fd, cmd, &lock));
}


static pid_t test_file_lock(int fd, int type)
{
    struct flock lock;

    assert(fd >= 0);

    lock.l_type = type;
    lock.l_start = 0;
    lock.l_whence = SEEK_SET;
    lock.l_len = 0;

    if (fcntl(fd, F_GETLK, &lock) < 0)
        err_msg(errno, "Unable to test for file lock");
    if (lock.l_type == F_UNLCK)
        return(0);
    return(lock.l_pid);
}


ssize_t read_n(int fd, void *buf, size_t n)
{
    size_t nleft;
    ssize_t nread;
    unsigned char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nread = read(fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return(-1);
        }
        else if (nread == 0) {		/* EOF */
            break;
        }
        nleft -= nread;
        p += nread;
    }
    return(n - nleft);
}


ssize_t write_n(int fd, void *buf, size_t n)
{
    size_t nleft;
    ssize_t nwritten;
    unsigned char *p;

    p = buf;
    nleft = n;
    while (nleft > 0) {
        if ((nwritten = write(fd, p, nleft)) < 0) {
            if (errno == EINTR)
                continue;
            else
                return(-1);
        }
        nleft -= nwritten;
        p += nwritten;
    }
    return(n);
}


ssize_t read_line(int fd, void *buf, size_t maxlen)
{
    ssize_t n, rc;
    unsigned char c, *p;

    n = 0;
    p = buf;
    while (n < maxlen - 1) {		/* reserve space for NUL-termination */

        if ((rc = read(fd, &c, 1)) == 1) {
            n++;
            *p++ = c;
            if (c == '\n')
                break;			/* store newline, like fgets() */
        }
        else if (rc == 0) {
            if (n == 0)			/* EOF, no data read */
                return(0);
            else			/* EOF, some data read */
                break;
        }
        else {
            if (errno == EINTR)
                continue;
            return(-1);
        }
    }

    *p = '\0';				/* NUL-terminate, like fgets() */
    return(n);
}


struct hostent * get_host_by_name(const char *name,
    char *buf, int buflen, int *h_err)
{
/*  gethostbyname() is not thread-safe, and there is no frelling standard
 *    for gethostbyname_r() -- the arg list varies from system to system!
 */
    int rc;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct hostent *hptr;
    int n = 0;

    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");

    if ((hptr = gethostbyname(name)))
        n = copy_hostent(hptr, buf, buflen);
    else if (h_err)
        *h_err = h_errno;

    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

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
    int rc;
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    struct hostent *hptr;
    int n = 0;

    if ((rc = pthread_mutex_lock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_lock() failed");

    if ((hptr = gethostbyaddr(addr, len, type)))
        n = copy_hostent(hptr, buf, buflen);
    else if (h_err)
        *h_err = h_errno;

    if ((rc = pthread_mutex_unlock(&lock)) != 0)
        err_msg(rc, "pthread_mutex_unlock() failed");

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
    else if ((h_err == NO_ADDRESS) || (h_errno == NO_DATA))
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


char * host_addr4_to_name(const struct in_addr *addr, char *name, int len)
{
    struct hostent *hptr;
    unsigned char buf[HOSTENT_SIZE];

    assert(addr);
    assert(name);

    if (!(hptr = get_host_by_addr((char *) addr, 4, AF_INET,
      buf, sizeof(buf), NULL)))
        return(NULL);
    if (strlen(hptr->h_name) >= len) {
        errno = ERANGE;
        return(NULL);
    }
    strcpy(name, hptr->h_name);
    return(name);
}


char * host_name_to_cname(const char *src, char *dst, int len)
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
    if (strlen(hptr->h_name) >= len) {
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


SigFunc * posix_signal(int signum, SigFunc *f)
{
/*  A wrapper for the historical signal() function to do things the Posix way.
 *  cf. Stevens UNPv1 figure 5.6.
 */
    struct sigaction act0, act1;

    act1.sa_handler = f;
    sigemptyset(&act1.sa_mask);
    act1.sa_flags = 0;
    if (signum == SIGALRM) {
#ifdef SA_INTERRUPT
        act1.sa_flags |= SA_INTERRUPT;	/* SunOS 4.x */
#endif /* SA_INTERRUPT */
    }
    else {
#ifdef SA_RESTART
        act1.sa_flags |= SA_RESTART;	/* SVR4, 4.4BSD */
#endif /* SA_RESTART */
    }

    /*  Technically, this routine should return SIG_ERR if sigaction()
     *    fails here.  But the caller would just err_msg() away, anways.
     */
    if (sigaction(signum, &act1, &act0) < 0)
        err_msg(errno, "signal(%d) failed", signum);
    return(act0.sa_handler);
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
